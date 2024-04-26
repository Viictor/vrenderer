#include "Renderer.h"

#include "donut/app/UserInterfaceUtils.h"
#include "donut/engine/BindingCache.h"
#include "donut/engine/CommonRenderPasses.h"
#include "donut/engine/ShaderFactory.h"
#include "donut/engine/TextureCache.h"
#include "donut/render/DeferredLightingPass.h"
#include "donut/render/DrawStrategy.h"
#include "donut/render/GBufferFillPass.h"
#include "donut/render/SkyPass.h"
#include "donut/render/ToneMappingPasses.h"

#include "profiler/Profiler.h"

using namespace vRenderer;
using namespace donut;

Renderer::Renderer(donut::app::DeviceManager* deviceManager, tf::Executor& executor)
	: ApplicationBase(deviceManager)
	, m_Executor(executor)
{
	SetupProfilingEvents(deviceManager);

	const std::shared_ptr<donut::vfs::NativeFileSystem> nativeFS = std::make_shared<donut::vfs::NativeFileSystem>();

	const std::filesystem::path executablePath = app::GetDirectoryWithExecutable();
	const std::filesystem::path mediaPath = executablePath.parent_path() / "media";
	const std::filesystem::path frameworkShaderPath = executablePath / "_shaders/donut" / app::GetShaderTypeName(deviceManager->GetDevice()->getGraphicsAPI());
	const std::filesystem::path appShaderPath = executablePath / "_shaders/vRenderer" / app::GetShaderTypeName(deviceManager->GetDevice()->getGraphicsAPI());

	m_RootFs = std::make_shared<donut::vfs::RootFileSystem>();
	m_RootFs->mount("/media", mediaPath);
	m_RootFs->mount("/shaders/donut", frameworkShaderPath);
	m_RootFs->mount("/shaders/vrenderer", appShaderPath);
	m_RootFs->mount("/native", nativeFS);

	m_ShaderFactory = std::make_shared<engine::ShaderFactory>(GetDevice(), m_RootFs, "/shaders");

	m_Editor = std::make_unique<Editor>(deviceManager, m_RootFs);
	m_Editor->Init(m_ShaderFactory);

	m_TextureCache = std::make_shared<engine::TextureCache>(GetDevice(), m_RootFs, nullptr);
	m_CommonPasses = std::make_shared<engine::CommonRenderPasses>(GetDevice(), m_ShaderFactory);
	m_BindingCache = std::make_unique<engine::BindingCache>(GetDevice());
	m_OpaqueDrawStrategy = std::make_shared<render::InstancedOpaqueDrawStrategy>();

	m_DeferredLightingPass = std::make_unique<render::DeferredLightingPass>(GetDevice(), m_CommonPasses);
	m_DeferredLightingPass->Init(m_ShaderFactory);

	m_CommandList = GetDevice()->createCommandList();

	// To remove
	m_Editor->GetUIData().m_MaxHeight = 120.0f;
	const std::filesystem::path textureFileName = "/media/Ridge Through Terrain Height Map.png";
	const std::shared_ptr<engine::LoadedTexture> heightmapTexture = m_TextureCache->LoadTextureFromFileDeferred(textureFileName, false);

	engine::TextureData* textureData = (engine::TextureData*)heightmapTexture.get();
	if (!textureData->data)
	{
		log::fatal("Couldn't load %s", textureFileName.generic_string().c_str());
	}

	m_TerrainPass = std::make_unique<TerrainPass>(GetDevice(), m_CommonPasses, m_Editor->GetUIData());
	m_TerrainPass->Init(*m_ShaderFactory, TerrainPass::CreateParameters(), m_CommandList, heightmapTexture, m_Executor);

	m_FirstPersonCamera.LookAt(float3(.0f, 120.8f, .0f), float3(1.0f, 1.8f, .0f));
	m_FirstPersonCamera.SetMoveSpeed(20.0f);

	

	m_Editor->AddEditorWindow(new EditorWindowCallback([this](Editor& editor)
	{
		RenderUI(editor);
	}));
	deviceManager->AddRenderPassToBack(m_Editor.get());


	/*std::filesystem::path scenePath = "/media/gltfScenes";
	m_SceneFilesAvailable = app::FindScenes(*m_RootFs, scenePath);
	std::string sceneName = app::FindPreferredScene(m_SceneFilesAvailable, "Cube.gltf");

	m_CurrentSceneName = sceneName;
	ApplicationBase::BeginLoadingScene(m_RootFs, sceneName);*/

	ApplicationBase::SceneLoaded();
}

bool Renderer::LoadScene(std::shared_ptr<vfs::IFileSystem> fs, const std::filesystem::path& fileName)
{
	using namespace std::chrono;

	engine::Scene* scene = new engine::Scene(
		GetDevice(), *m_ShaderFactory, fs, m_TextureCache, nullptr, nullptr);

	const auto startTime = high_resolution_clock::now();

	if (scene->Load(fileName))
	{
		m_Scene = std::unique_ptr<engine::Scene>(scene);

		const auto endTime = high_resolution_clock::now();
		const auto duration = duration_cast<milliseconds>(endTime - startTime).count();
		log::info("Scene loading time: %llu ms", duration);

		return true;
	}

	return false;
}

void Renderer::SceneLoaded()
{
	ApplicationBase::SceneLoaded();

	if (!m_Scene->GetSceneGraph())
		return;

	m_Scene->FinishedLoading(GetFrameIndex()); // This creates the mesh buffers after loading

	if (m_Scene->GetSceneGraph()->GetLights().empty())
	{
		std::shared_ptr<engine::SceneGraphNode> node = std::make_shared<engine::SceneGraphNode>();
		m_Scene->GetSceneGraph()->Attach(m_Scene->GetSceneGraph()->GetRootNode(), node);

		m_DirectionalLight = std::make_shared<engine::DirectionalLight>();
		node->SetLeaf(m_DirectionalLight);
		m_DirectionalLight->SetName("Sun");
		m_DirectionalLight->SetDirection({ 0.1f ,-0.4f , 0.1f });
		m_DirectionalLight->angularSize = .53f;
		m_DirectionalLight->irradiance = 1.0f;
	}
}

bool Renderer::KeyboardUpdate(int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE)
	{
		glfwSetWindowShouldClose(GetDeviceManager()->GetWindow(), true);
	}
	m_FirstPersonCamera.KeyboardUpdate(key, scancode, action, mods);

	return true;
}

bool Renderer::MousePosUpdate(double xpos, double ypos)
{
	m_FirstPersonCamera.MousePosUpdate(xpos, ypos);
	return true;
}

bool Renderer::MouseButtonUpdate(int button, int action, int mods)
{
	m_FirstPersonCamera.MouseButtonUpdate(button, action, mods);
	return true;
}

void Renderer::Animate(float seconds)
{
	PROFILE_CPU_SCOPE();

	m_FirstPersonCamera.Animate(seconds);
	if (m_ToneMappingPass)
		m_ToneMappingPass->AdvanceFrame(seconds);
	GetDeviceManager()->SetInformativeWindowTitle(m_WindowTitle);
}

void Renderer::RenderScene(nvrhi::IFramebuffer* framebuffer)
{
	if (m_WaitGPUEvent)
	{
		m_WaitGPUEvent = false;
		PROFILE_CPU_END(); // WAIT GPU EVENT
	}
	PROFILE_CPU_SCOPE();

	// Update transforms and states of scene graph nodes
	if (m_Scene && m_Scene->GetSceneGraph())
		m_Scene->RefreshSceneGraph(GetFrameIndex());

	// Handle resizing render targets
	const nvrhi::FramebufferInfoEx& fbinfo = framebuffer->getFramebufferInfo();
	constexpr math::uint sampleCount = 1;
	const bool createRenderTargets = !m_RenderTargets || m_RenderTargets->IsUpdateRequired(math::uint2(fbinfo.width, fbinfo.height), sampleCount);

	if (createRenderTargets)
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

	UpdateView();

	if (createRenderTargets)
	{
		m_SkyPass = std::make_unique<render::SkyPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, m_View);

		ToneMappingPass::CreateParameters toneMappingParams;
		m_ToneMappingPass = std::make_unique<render::ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, m_View, toneMappingParams);

		render::GBufferFillPass::CreateParameters gbufferParams;
		m_GBufferPass = std::make_unique<render::GBufferFillPass>(GetDevice(), m_CommonPasses);
		m_GBufferPass->Init(*m_ShaderFactory, gbufferParams);
	}

	RecordCommand(framebuffer);
	Submit();
}

void Renderer::SetupProfilingEvents(donut::app::DeviceManager* deviceManager)
{
	deviceManager->m_callbacks.beforeFrame = [](app::DeviceManager&)
		{
			PROFILE_FRAME();
			PROFILE_FRAME_GPU();
		};
	deviceManager->m_callbacks.beforeAnimate = [](app::DeviceManager&)
		{
			PROFILE_CPU_BEGIN("Animate");
		};
	deviceManager->m_callbacks.afterAnimate = [](app::DeviceManager&)
		{
			PROFILE_CPU_END();
		};
	deviceManager->m_callbacks.beforeRender = [this](app::DeviceManager&)
		{
			PROFILE_CPU_BEGIN("Render");

			if (!m_WaitGPUEvent)
			{
				m_WaitGPUEvent = true;
				PROFILE_CPU_BEGIN("Wait GPU");
			}
		};
	deviceManager->m_callbacks.afterRender = [](app::DeviceManager&)
		{
			PROFILE_CPU_END();
		};
	deviceManager->m_callbacks.beforePresent = [](app::DeviceManager&)
		{
			PROFILE_CPU_BEGIN("Present");
		};
	deviceManager->m_callbacks.afterPresent = [](app::DeviceManager&)
		{
			PROFILE_CPU_END();
		};
}

void Renderer::UpdateView()
{
	const float2 renderTargetSize = float2(m_RenderTargets->GetSize());
	const float4x4 projection = math::perspProjD3DStyle(math::radians(60.f), renderTargetSize.x / renderTargetSize.y, 0.1f, 10000.f);
	m_View.SetViewport(nvrhi::Viewport(renderTargetSize.x, renderTargetSize.y));
	m_View.SetMatrices(m_FirstPersonCamera.GetWorldToViewMatrix(), projection);
	m_View.UpdateCache();
}

void Renderer::RecordCommand(nvrhi::IFramebuffer* framebuffer) const
{
	m_CommandList->open();
	{
		PROFILE_GPU_SCOPE(m_CommandList, "GPU Frame");

		PROFILE_GPU_BEGIN(m_CommandList, "Scene Refresh");
		if (m_Scene && m_Scene->GetSceneGraph())
			m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex()); // Updates any geometry, material, etc buffer changes
		PROFILE_GPU_END(m_CommandList);

		m_RenderTargets->Clear(m_CommandList);

		render::GBufferFillPass::Context context;

		if (m_Scene && m_Scene->GetSceneGraph())
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
		renderParams.wireframe = m_Editor->GetUIData().m_Wireframe;
		renderParams.lockView = m_Editor->GetUIData().m_LockView;

		m_TerrainPass->Render(
			m_CommandList,
			&m_View,
			&m_View,
			*m_RenderTargets->GBufferFramebuffer,
			renderParams);

		PROFILE_GPU_END(m_CommandList);



		if (m_Scene && m_Scene->GetSceneGraph())
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

		if (m_DirectionalLight)
		{
			PROFILE_GPU_BEGIN(m_CommandList, "Sky");
			m_SkyPass->Render(m_CommandList, m_View, *m_DirectionalLight, SkyParameters());
			PROFILE_GPU_END(m_CommandList);
		}
		m_CommonPasses->BlitTexture(m_CommandList, framebuffer, m_RenderTargets->LdrColor, m_BindingCache.get());

		//m_CommonPasses->BlitTexture(m_CommandList, framebuffer, 
		//    m_UIData.m_Wireframe ? m_RenderTargets->GBufferDiffuse : m_RenderTargets->HdrColor, m_BindingCache.get());
	}
	m_CommandList->close();
}

void Renderer::Submit()
{
	const Span<nvrhi::CommandListHandle> cmdlists(reinterpret_cast<nvrhi::CommandListHandle*>(&m_CommandList), 1);
	PROFILE_EXECUTE_COMMANDLISTS(cmdlists);
	GetDevice()->executeCommandList(m_CommandList);
}

void Renderer::RenderUI(Editor& editor)
{
	ImGui::SetNextWindowPos(ImVec2(10.f, 10.f), 0);
	ImGui::Begin("Settings", 0, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_MenuBar);

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Menu"))
		{
			ImGui::MenuItem("Profiler", NULL, &editor.GetUIData().m_ProfilerOpen);
			ImGui::MenuItem("Open", 0, &editor.GetUIData().m_FileOpen);
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	ImGui::Text("Renderer: %s", editor.GetDeviceManager()->GetRendererString());
	double frameTime = editor.GetDeviceManager()->GetAverageFrameTimeSeconds();
	if (frameTime > 0.0)
		ImGui::Text("%.3f ms/frame (%.1f FPS)", frameTime * 1e3, 1.0 / frameTime);

	ImGui::Checkbox("Wireframe", &editor.GetUIData().m_Wireframe);
	ImGui::Checkbox("Lock View", &editor.GetUIData().m_LockView);

	ImGui::InputFloat("Max Height", &editor.GetUIData().m_MaxHeight, 1.0);

	ImGui::Text("Num instances : %i", editor.GetUIData().m_NumChunks);

	if (m_DirectionalLight)
		app::LightEditor(*m_DirectionalLight);

	ImGui::End();

	if (editor.GetUIData().m_ProfilerOpen)
	{

		ImVec2 viewportSize = ImGui::GetMainViewport()->WorkSize;
		ImGui::SetNextWindowPos(ImVec2(380.f, 10.f), 0);
		ImGui::SetNextWindowSize(ImVec2(viewportSize.x - 390.0f, donut::math::max(290.0f, editor.GetUIData().m_ProfilerWindowHeight)));
		ImGui::Begin("Profiler", &editor.GetUIData().m_ProfilerOpen, ImGuiWindowFlags_NoResize);
		DrawProfilerHUD(editor.GetUIData().m_ProfilerWindowHeight);
		ImGui::End();
	}
	
	if (editor.GetUIData().m_FileOpen)
	{
		std::string filename;
		bool result = app::FileDialog(true, ".gltf", filename);
		editor.GetUIData().m_FileOpen = false;

		if (result)
		{
			std::string token = filename.substr(filename.find_last_of("\\") + 1, filename.length());

			std::filesystem::path scenePath = "/media/gltfScenes";
			m_SceneFilesAvailable = app::FindScenes(*m_RootFs, scenePath);
			std::string sceneName = app::FindPreferredScene(m_SceneFilesAvailable, token);

			if (!sceneName.empty())
			{
				m_CurrentSceneName = sceneName;
				ApplicationBase::BeginLoadingScene(m_RootFs, sceneName);
			}
		}
	}
}
