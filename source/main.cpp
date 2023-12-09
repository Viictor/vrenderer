#include <donut/app/ApplicationBase.h>
#include <donut/app/DeviceManager.h>
#include <donut/app/Camera.h>
#include <donut/core/vfs/VFS.h>
#include <donut/core/log.h>
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/engine/Scene.h>
#include <nvrhi/utils.h>

#include <donut/render/DeferredLightingPass.h>
#include <donut/render/GBuffer.h>
#include <donut/render/GBufferFillPass.h>
#include <donut/render/DrawStrategy.h>
#include <donut/render/GeometryPasses.h>
#include <donut/engine/FramebufferFactory.h>

#include <donut/engine/SceneGraph.h>

#include "UIRenderer.h"

using namespace donut;
using namespace donut::math;

static const char* g_WindowTitle = "vRenderer";

class RenderTargets : public render::GBufferRenderTargets
{
public:
    nvrhi::TextureHandle ShadedColor;

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
        textureDesc.debugName = "ShadedColor";
        textureDesc.isUAV = true;
        textureDesc.format = nvrhi::Format::RGBA16_FLOAT;
        textureDesc.width = size.x;
        textureDesc.height = size.y;
        textureDesc.sampleCount = sampleCount;
        ShadedColor = device->createTexture(textureDesc);
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

    // Basic 2D projection
    engine::PlanarView m_View;

    float m_Rotation = .0f;

public:

	VRenderer(app::DeviceManager* deviceManager, const std::string& sceneName)
        : Super(deviceManager)
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


        std::filesystem::path scenePath = "/media/glTF-Sample-Models/2.0";
        m_SceneFilesAvailable = app::FindScenes(*m_RootFs, scenePath);

        if (sceneName.empty() && m_SceneFilesAvailable.empty())
        {
            log::fatal("No scene file found in media folder '%s'\n"
                "Please make sure that folder contains valid scene files.", scenePath.generic_string().c_str());
        }

        m_CommandList = GetDevice()->createCommandList();

        if (sceneName.empty())
            SetCurrentSceneName(app::FindPreferredScene(m_SceneFilesAvailable, "Sponza.gltf"));
        else
            SetCurrentSceneName("/native/" + sceneName);

        m_FirstPersonCamera.LookAt(float3(.0f, 1.8f, .0f), float3(1.0f, 1.8f, .0f));
        m_FirstPersonCamera.SetMoveSpeed(3.0f);
	}

    void SetupView()
    {
        float2 renderTargetSize = float2(m_RenderTargets->GetSize());

        float4x4 projection = math::perspProjD3DStyle(math::radians(60.f), renderTargetSize.x / renderTargetSize.y, 0.1f, 1000.f);

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

        m_Scene->FinishedLoading(GetFrameIndex()); // This creates the mesh buffers after loading

        if (m_Scene->GetSceneGraph()->GetLights().empty())
        {
            std::shared_ptr<engine::SceneGraphNode> node = std::make_shared<engine::SceneGraphNode>();
            m_Scene->GetSceneGraph()->Attach(m_Scene->GetSceneGraph()->GetRootNode(), node);

            std::shared_ptr<engine::DirectionalLight> directionalLight = std::make_shared<engine::DirectionalLight>();
            node->SetLeaf(directionalLight);

            directionalLight->SetName("Sun");
            directionalLight->SetDirection(dm::double3(.1, -.4, .1));
            directionalLight->angularSize = .53f;
            directionalLight->irradiance = 2.0f;

        }
    }

    bool KeyboardUpdate(int key, int scancode, int action, int mods) override
    {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE)
            glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), true);

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
        m_FirstPersonCamera.Animate(seconds);
        GetDeviceManager()->SetInformativeWindowTitle(g_WindowTitle);
    }

    virtual void RenderScene(nvrhi::IFramebuffer* framebuffer) override
    {
        m_Scene->RefreshSceneGraph(GetFrameIndex()); // Updates transforms and states of scene graph nodes

        const nvrhi::FramebufferInfoEx& fbinfo = framebuffer->getFramebufferInfo();

        math::uint sampleCount = 1;

        if (!m_RenderTargets || m_RenderTargets->IsUpdateRequired(math::uint2(fbinfo.width, fbinfo.height), sampleCount))
        {
            m_RenderTargets = nullptr;
            m_BindingCache->Clear();
            m_GBufferPass.reset();
            m_DeferredLightingPass->ResetBindingCache();

            m_RenderTargets = std::make_unique<RenderTargets>();
            m_RenderTargets->Init(GetDevice(), math::uint2(fbinfo.width, fbinfo.height), sampleCount, false, false);
        }

        SetupView();
        
        if (!m_GBufferPass)
        {
            render::GBufferFillPass::CreateParameters gbufferParams;
            m_GBufferPass = std::make_unique<render::GBufferFillPass>(GetDevice(), m_CommonPasses);
            m_GBufferPass->Init(*m_ShaderFactory, gbufferParams);
        }

        m_CommandList->open();

        m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex()); // Updates any geometry, material, etc buffer changes

        m_RenderTargets->Clear(m_CommandList);

        render::GBufferFillPass::Context context;

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

        render::DeferredLightingPass::Inputs deferredInputs;
        deferredInputs.SetGBuffer(*m_RenderTargets);
        deferredInputs.ambientColorTop = 0.2f;
        deferredInputs.ambientColorBottom = deferredInputs.ambientColorTop * float3(0.3f, 0.4f, 0.3f);
        deferredInputs.lights = &m_Scene->GetSceneGraph()->GetLights();
        deferredInputs.output = m_RenderTargets->ShadedColor;

        m_DeferredLightingPass->Render(m_CommandList, m_View, deferredInputs);

        m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->ShadedColor, m_BindingCache.get());

        m_CommandList->close();
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

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, g_WindowTitle))
    {
        log::fatal("Cannot initialize a graphics device with the requested parameters");
        return 1;
    }

    {

        std::shared_ptr<VRenderer> vRenderer = std::make_shared<VRenderer>(deviceManager, "");
        std::shared_ptr<UIRenderer> gui = std::make_shared<UIRenderer>(deviceManager, vRenderer->GetRootFs());

        gui->Init(vRenderer->GetShaderFactory());

        deviceManager->AddRenderPassToBack(vRenderer.get());
        deviceManager->AddRenderPassToBack(gui.get());

        deviceManager->RunMessageLoop();
    }

    deviceManager->Shutdown();

    delete deviceManager;

    return 0;
}