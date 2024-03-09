#include <donut/app/ApplicationBase.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/Camera.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/Scene.h>
#include <donut/engine/SceneGraph.h>
#include <nvrhi/utils.h>
#include <taskflow/taskflow.hpp>

#include <donut/render/DeferredLightingPass.h>
#include <donut/render/GBuffer.h>
#include <donut/render/GBufferFillPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/GeometryPasses.h>
#include <donut/render/SkyPass.h>
#include <donut/render/ToneMappingPasses.h>

#include "UIRenderer.h"
#include "terrain/TerrainPass.h"
#include "terrain/QuadTree.h"

#include "profiler/Profiler.h"

using namespace donut;
using namespace donut::math;

static const char* g_WindowTitle = "vRenderer";

class RenderTargets : public render::GBufferRenderTargets
{
public:
    nvrhi::TextureHandle LdrColor;
    nvrhi::TextureHandle HdrColor;

    std::shared_ptr<engine::FramebufferFactory> HdrFramebuffer;
    std::shared_ptr<engine::FramebufferFactory> LdrFramebuffer;

    void Init(
        nvrhi::IDevice* device,
        dm::uint2 size,
        dm::uint sampleCount,
        bool enableMotionVectors,
        bool useReverseProjection) override
    {
        GBufferRenderTargets::Init(device, size, sampleCount, enableMotionVectors, useReverseProjection);

        nvrhi::TextureDesc textureDesc;
        textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
        textureDesc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        textureDesc.keepInitialState = true;
        textureDesc.debugName = "HdrColor";
        textureDesc.isUAV = true;
        textureDesc.format = nvrhi::Format::RGBA16_FLOAT;
        textureDesc.width = size.x;
        textureDesc.height = size.y;
        textureDesc.sampleCount = sampleCount;
        HdrColor = device->createTexture(textureDesc);

        textureDesc = {};
        textureDesc.dimension = nvrhi::TextureDimension::Texture2D;
        textureDesc.initialState = nvrhi::ResourceStates::RenderTarget;
        textureDesc.keepInitialState = true;
        textureDesc.debugName = "LdrColor";
        textureDesc.isUAV = false;
        textureDesc.isRenderTarget = true;
        textureDesc.format = nvrhi::Format::SRGBA8_UNORM;
        textureDesc.width = size.x;
        textureDesc.height = size.y;
        textureDesc.sampleCount = sampleCount;
        LdrColor = device->createTexture(textureDesc);

        LdrFramebuffer = std::make_shared<engine::FramebufferFactory>(device);
        LdrFramebuffer->RenderTargets = { LdrColor };
        LdrFramebuffer->DepthTarget = Depth;

        HdrFramebuffer = std::make_shared<engine::FramebufferFactory>(device);
        HdrFramebuffer->RenderTargets = { HdrColor };
    
    }

    [[nodiscard]] bool IsUpdateRequired(uint2 size, uint sampleCount) const
    {
        if (any(m_Size != size) || m_SampleCount != sampleCount)
            return true;

        return false;
    }
};

class VRenderer : public app::ApplicationBase
{
	typedef ApplicationBase Super;

private:

	std::shared_ptr<vfs::RootFileSystem> m_RootFs;
	std::shared_ptr<engine::ShaderFactory> m_ShaderFactory;
    std::unique_ptr<engine::BindingCache> m_BindingCache;   

    std::vector<std::string> m_SceneFilesAvailable;
    std::string m_CurrentSceneName;
    std::shared_ptr<engine::Scene> m_Scene;

    nvrhi::CommandListHandle m_CommandList;
    app::FirstPersonCamera m_FirstPersonCamera;

    // GBuffer Geometry Passes
    std::shared_ptr<RenderTargets> m_RenderTargets;
    std::unique_ptr<render::GBufferFillPass> m_GBufferPass;
    std::unique_ptr<render::DeferredLightingPass> m_DeferredLightingPass;
    std::shared_ptr<render::InstancedOpaqueDrawStrategy> m_OpaqueDrawStrategy;

    std::unique_ptr<render::SkyPass> m_SkyPass;
    std::shared_ptr<engine::DirectionalLight> m_DirectionalLight;

    std::unique_ptr<render::ToneMappingPass> m_ToneMappingPass;

    // Terrain Geometry Pass
    std::unique_ptr<vRenderer::TerrainPass> m_TerrainPass;

    // Basic 2D projection
    engine::PlanarView m_View;

    float m_Rotation = .0f;

    UIData& m_UIData;

    tf::Executor& m_Executor;

public:

    VRenderer(app::DeviceManager* deviceManager, const std::string& sceneName, UIData& uiData, tf::Executor& executor)
        : Super(deviceManager)
        , m_UIData(uiData)
        , m_Executor(executor)
	{
        std::filesystem::path mediaPath = app::GetDirectoryWithExecutable().parent_path() / "media";
        std::filesystem::path frameworkShaderPath = app::GetDirectoryWithExecutable() / "shaders/framework" / app::GetShaderTypeName(deviceManager->GetDevice()->getGraphicsAPI());
        std::filesystem::path appShaderPath = app::GetDirectoryWithExecutable() / "shaders/vRenderer" / app::GetShaderTypeName(deviceManager->GetDevice()->getGraphicsAPI());

        std::shared_ptr<donut::vfs::NativeFileSystem> nativeFS = std::make_shared<donut::vfs::NativeFileSystem>();
        m_RootFs = std::make_shared<donut::vfs::RootFileSystem>();
        m_RootFs->mount("/media", mediaPath);
        m_RootFs->mount("/shaders/donut", frameworkShaderPath);
        m_RootFs->mount("/shaders/vrenderer", appShaderPath);
        m_RootFs->mount("/native", nativeFS);

        m_ShaderFactory = std::make_shared<engine::ShaderFactory>(GetDevice(), m_RootFs, "/shaders");
        m_TextureCache = std::make_shared<engine::TextureCache>(GetDevice(), m_RootFs, nullptr);
        m_CommonPasses = std::make_shared<engine::CommonRenderPasses>(GetDevice(), m_ShaderFactory);
        m_BindingCache = std::make_unique<engine::BindingCache>(GetDevice());
        m_OpaqueDrawStrategy = std::make_shared<render::InstancedOpaqueDrawStrategy>();

        m_DeferredLightingPass = std::make_unique<render::DeferredLightingPass>(GetDevice(), m_CommonPasses);
        m_DeferredLightingPass->Init(m_ShaderFactory);

        m_DirectionalLight = std::make_shared<engine::DirectionalLight>();

        m_CommandList = GetDevice()->createCommandList();


        m_UIData.m_MaxHeight = 120.0f;
        std::filesystem::path textureFileName ="/media/Heightmap_01_Mountains4k.png";
        std::shared_ptr<engine::LoadedTexture> heightmapTexture = m_TextureCache->LoadTextureFromFileDeferred(textureFileName, false);

        m_TerrainPass = std::make_unique<vRenderer::TerrainPass>(GetDevice(), m_CommonPasses, m_UIData);
        m_TerrainPass->Init(*m_ShaderFactory, vRenderer::TerrainPass::CreateParameters(), m_CommandList, heightmapTexture, m_Executor);

        std::filesystem::path scenePath = "/media/glTF-Sample-Models/2.0";
        m_SceneFilesAvailable = app::FindScenes(*m_RootFs, scenePath);

        if (sceneName.empty() && m_SceneFilesAvailable.empty())
        {
            log::fatal("No scene file found in media folder '%s'\n"
                "Please make sure that folder contains valid scene files.", scenePath.generic_string().c_str());
        }


        if (sceneName.empty())
            SetCurrentSceneName(app::FindPreferredScene(m_SceneFilesAvailable, "emptyScene.gltf"));
        else
            SetCurrentSceneName(app::FindPreferredScene(m_SceneFilesAvailable, sceneName));

        {
            //m_Scene = std::make_unique<engine::Scene>(deviceManager->GetDevice(), *m_ShaderFactory, m_RootFs, m_TextureCache, nullptr, nullptr);
            //SceneLoaded();
            
            //auto node = std::make_shared<engine::SceneGraphNode>();
            //m_Scene->GetSceneGraph()->SetRootNode(node);
        }

        m_FirstPersonCamera.LookAt(float3(.0f, 120.8f, .0f), float3(1.0f, 1.8f, .0f));
        m_FirstPersonCamera.SetMoveSpeed(10.0f);
	}

    void SetupView()
    {
        float2 renderTargetSize = float2(m_RenderTargets->GetSize());

        float4x4 projection = math::perspProjD3DStyle(math::radians(60.f), renderTargetSize.x / renderTargetSize.y, 0.1f, 10000.f);

        m_View.SetViewport(nvrhi::Viewport(renderTargetSize.x, renderTargetSize.y));
        m_View.SetMatrices(m_FirstPersonCamera.GetWorldToViewMatrix(), projection);
        m_View.UpdateCache();
    }

    void SetCurrentSceneName(const std::string& sceneName)
    {
        if (m_CurrentSceneName == sceneName)
            return;

        m_CurrentSceneName = sceneName;
        BeginLoadingScene(m_RootFs, sceneName);
    }

    std::shared_ptr<vfs::IFileSystem> GetRootFs() const
    {
        return m_RootFs;
    }

    std::string GetCurrentSceneName() const
    {
        return m_CurrentSceneName;
    }

    std::shared_ptr<engine::ShaderFactory> GetShaderFactory() const
    {
        return m_ShaderFactory;
    }

    virtual bool LoadScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& fileName) override
    {
        using namespace std::chrono;

        engine::Scene* scene = new engine::Scene(GetDevice(), *m_ShaderFactory, fs, m_TextureCache, nullptr, nullptr);

        auto startTime = high_resolution_clock::now();

        if (scene->Load(fileName))
        {
            m_Scene = std::unique_ptr<engine::Scene>(scene);

            auto endTime = high_resolution_clock::now();
            auto duration = duration_cast<milliseconds>(endTime - startTime).count();
            log::info("Scene loading time: %llu ms", duration);

            return true;
        }

        return false;
    }

    virtual void SceneLoaded() override
    {
        Super::SceneLoaded();
        if (!m_Scene->GetSceneGraph().get())
            return;

        m_Scene->FinishedLoading(GetFrameIndex()); // This creates the mesh buffers after loading

        if (m_Scene->GetSceneGraph()->GetLights().empty())
        {
            std::shared_ptr<engine::SceneGraphNode> node = std::make_shared<engine::SceneGraphNode>();
            m_Scene->GetSceneGraph()->Attach(m_Scene->GetSceneGraph()->GetRootNode(), node);
            node->SetLeaf(m_DirectionalLight);
            m_DirectionalLight->SetName("Sun");
            m_DirectionalLight->SetDirection(dm::normalize(dm::double3(m_UIData.m_SunDir[0], m_UIData.m_SunDir[1], m_UIData.m_SunDir[2])));
            m_DirectionalLight->angularSize = .53f;
            m_DirectionalLight->irradiance = 1.0f;
        }
    }

    bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE)
        {
            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), true);
        }
        m_FirstPersonCamera.KeyboardUpdate(key, scancode, action, mods);

        return true;
    }

    bool MousePosUpdate(double xpos, double ypos) override
    {
        m_FirstPersonCamera.MousePosUpdate(xpos, ypos);
        return true;
    }

    bool MouseButtonUpdate(int button, int action, int mods) override
    {
        m_FirstPersonCamera.MouseButtonUpdate(button, action, mods);
        return true;
    }

    void Animate(float seconds) override
    {
        PROFILE_CPU_SCOPE();

        m_FirstPersonCamera.Animate(seconds);
        if (m_ToneMappingPass)
            m_ToneMappingPass->AdvanceFrame(seconds);
        GetDeviceManager()->SetInformativeWindowTitle(g_WindowTitle);
    }

    virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {
        PROFILE_CPU_END(); // WAIT GPU EVENT
        PROFILE_CPU_SCOPE();

        if (m_Scene->GetSceneGraph().get())
            m_Scene->RefreshSceneGraph(GetFrameIndex()); // Updates transforms and states of scene graph nodes

        const nvrhi::FramebufferInfoEx& fbinfo = framebuffer->getFramebufferInfo();

        math::uint sampleCount = 1;

        if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(math::uint2(fbinfo.width, fbinfo.height), sampleCount))
        {
            m_RenderTargets = nullptr;
            m_BindingCache->Clear();
            m_GBufferPass.reset();
            m_DeferredLightingPass->ResetBindingCache();
            m_SkyPass.reset();
            m_ToneMappingPass.reset();

            m_RenderTargets = std::make_unique<RenderTargets>();
            m_RenderTargets->Init(GetDevice(), math::uint2(fbinfo.width, fbinfo.height), sampleCount, false, false);
        }

        SetupView();

        if (!m_SkyPass)
            m_SkyPass = std::make_unique<render::SkyPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, m_View);

        ToneMappingPass::CreateParameters toneMappingParams;

        if (!m_ToneMappingPass)
        {
            m_ToneMappingPass = std::make_unique<render::ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, m_View, toneMappingParams);
        }
        if (!m_GBufferPass)
        {
            render::GBufferFillPass::CreateParameters gbufferParams;
            m_GBufferPass = std::make_unique<render::GBufferFillPass>(GetDevice(), m_CommonPasses);
            m_GBufferPass->Init(*m_ShaderFactory, gbufferParams);
        }

        m_CommandList->open();
        {
            PROFILE_GPU_SCOPE(m_CommandList, "GPU Frame");

            PROFILE_GPU_BEGIN(m_CommandList, "Scene Refresh");
            if (m_Scene->GetSceneGraph().get())
                m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex()); // Updates any geometry, material, etc buffer changes
            PROFILE_GPU_END(m_CommandList);

            m_RenderTargets->Clear(m_CommandList);

            m_DirectionalLight->SetDirection(dm::normalize(dm::double3(m_UIData.m_SunDir[0], m_UIData.m_SunDir[1], m_UIData.m_SunDir[2])));

            render::GBufferFillPass::Context context;

            if (m_Scene->GetSceneGraph().get())
            {
                PROFILE_GPU_SCOPE(m_CommandList, "GBuffer fill");
                render::RenderCompositeView(
                    m_CommandList,
                    &m_View,
                    &m_View,
                    *m_RenderTargets->GBufferFramebuffer,
                    m_Scene->GetSceneGraph()->GetRootNode(),
                    *m_OpaqueDrawStrategy,
                    *m_GBufferPass,
                    context,
                    "GBufferFill",
                    true);
            }

            PROFILE_GPU_BEGIN(m_CommandList, "Terrain");
            vRenderer::TerrainPass::RenderParams renderParams;
            renderParams.wireframe = m_UIData.m_Wireframe;
            renderParams.lockView = m_UIData.m_LockView;

            m_TerrainPass->Render(
                m_CommandList,
                &m_View,
                &m_View,
                *m_RenderTargets->GBufferFramebuffer,
                renderParams);

            PROFILE_GPU_END(m_CommandList);

            
            
            if (m_Scene->GetSceneGraph().get())
            {
                PROFILE_GPU_SCOPE(m_CommandList, "Deferred Lighting");
                render::DeferredLightingPass::Inputs deferredInputs;
                deferredInputs.SetGBuffer(*m_RenderTargets);
                deferredInputs.ambientColorTop = 0.2f;
                deferredInputs.ambientColorBottom = deferredInputs.ambientColorTop * float3(0.3f, 0.4f, 0.3f);
                deferredInputs.lights = &m_Scene->GetSceneGraph()->GetLights();
                deferredInputs.output = m_RenderTargets->HdrColor;

                m_DeferredLightingPass->Render(m_CommandList, m_View, deferredInputs);
            }

            

            PROFILE_GPU_BEGIN(m_CommandList, "ToneMapping");
            m_ToneMappingPass->SimpleRender(m_CommandList, ToneMappingParameters(), m_View, m_RenderTargets->HdrColor);
            PROFILE_GPU_END(m_CommandList);

            PROFILE_GPU_BEGIN(m_CommandList, "Sky");
            m_SkyPass->Render(m_CommandList, m_View, *m_DirectionalLight, SkyParameters());
            PROFILE_GPU_END(m_CommandList);

            m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->LdrColor, m_BindingCache.get());

            //m_CommonPasses->BlitTexture(m_CommandList, framebuffer, 
            //    m_UIData.m_Wireframe ? m_RenderTargets->GBufferDiffuse : m_RenderTargets->HdrColor, m_BindingCache.get());
        }
        m_CommandList->close();

        Span<nvrhi::CommandListHandle> cmdlists((nvrhi::CommandListHandle*)&m_CommandList, 1);
        PROFILE_EXECUTE_COMMANDLISTS(cmdlists);
        GetDevice()->executeCommandList(m_CommandList);

    }
};


#ifdef WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
#else
int main(int __argc, const char** __argv)
#endif
{
    nvrhi::GraphicsAPI api = app::GetGraphicsAPIFromCommandLine(__argc, __argv);
    app::DeviceManager* deviceManager = app::DeviceManager::Create(api);

    app::DeviceCreationParameters deviceParams;
#ifdef _DEBUG
    deviceParams.enableDebugRuntime = true;
    deviceParams.enableNvrhiValidationLayer = true;
#endif

    uint32_t numQueues = 1;
    //deviceParams.enableComputeQueue = true; numQueues++;
    //deviceParams.enableCopyQueue = true; numQueues++;

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, g_WindowTitle))
    {
        log::fatal("Cannot initialize a graphics device with the requested parameters");
        return 1;
    }

    const uint32_t numFramesToProfile = 10;
    gCPUProfiler.Initialize(numFramesToProfile, 1024);
    gGPUProfiler.Initialize(deviceManager->GetDevice(), numQueues, numFramesToProfile, 2, 1024, 128, 32);
    
    {
        UIData uiData;
        tf::Executor executor;
        std::shared_ptr<VRenderer> vRenderer = std::make_shared<VRenderer>(deviceManager, "", uiData, executor);
        std::shared_ptr<UIRenderer> gui = std::make_shared<UIRenderer>(deviceManager, vRenderer->GetRootFs(), uiData);

        gui->Init(vRenderer->GetShaderFactory());

        deviceManager->AddRenderPassToBack(vRenderer.get());
        deviceManager->AddRenderPassToBack(gui.get());

        deviceManager->m_callbacks.beforeFrame = [](DeviceManager&) 
            {
                PROFILE_FRAME();
                PROFILE_FRAME_GPU();
            };
        deviceManager->m_callbacks.beforeAnimate = [](DeviceManager&)
            {
                PROFILE_CPU_BEGIN("Animate");
            };
        deviceManager->m_callbacks.afterAnimate = [](DeviceManager&)
            {
                PROFILE_CPU_END();
            };
        deviceManager->m_callbacks.beforeRender = [](DeviceManager&)
            {
                PROFILE_CPU_BEGIN("Render");
                PROFILE_CPU_BEGIN("Wait GPU");
            };
        deviceManager->m_callbacks.afterRender = [](DeviceManager&)
            {
                PROFILE_CPU_END();
            };
        deviceManager->m_callbacks.beforePresent = [](DeviceManager&)
            {
                PROFILE_CPU_BEGIN("Present");
            };
        deviceManager->m_callbacks.afterPresent = [](DeviceManager&)
            {
                PROFILE_CPU_END();
            };
        deviceManager->RunMessageLoop();
        
        executor.wait_for_all();
    }

    gCPUProfiler.Shutdown();
    deviceManager->Shutdown();

    delete deviceManager;

    return 0;
}