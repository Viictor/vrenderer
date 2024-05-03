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
#include "editor/ImGuizmo.h"

#include "nvrhi/utils.h"

using namespace vRenderer;
using namespace donut;

Renderer::Renderer(donut::app::DeviceManager* deviceManager, tf::Executor& executor)
	: ApplicationBase(deviceManager)
	, m_Executor(executor)
	, m_CameraMoveSpeed(20.0f)
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
	m_TextureCache = std::make_shared<engine::TextureCache>(GetDevice(), m_RootFs, nullptr);
	m_CommonPasses = std::make_shared<engine::CommonRenderPasses>(GetDevice(), m_ShaderFactory);
	m_BindingCache = std::make_unique<engine::BindingCache>(GetDevice());
	m_OpaqueDrawStrategy = std::make_shared<render::InstancedOpaqueDrawStrategy>();

	m_CommandList = GetDevice()->createCommandList();

	// To remove
	const std::filesystem::path textureFileName = "/media/terrain_heightmap.png";
	std::shared_ptr<engine::LoadedTexture> heightmapTexture = m_TextureCache->LoadTextureFromFileDeferred(textureFileName, false);

	const std::filesystem::path colorTextureFileName = "/media/terrain_albedo.png";
	std::shared_ptr<engine::LoadedTexture> colorTexture = m_TextureCache->LoadTextureFromFileDeferred(colorTextureFileName, true);

	engine::TextureData* textureData = (engine::TextureData*)heightmapTexture.get();
	if (!textureData->data)
	{
		log::warning("Couldn't load %s", textureFileName.generic_string().c_str());
        heightmapTexture.reset();
		colorTexture.reset();
	}

	m_TerrainPass = std::make_unique<TerrainPass>(GetDevice(), m_CommonPasses);
	m_TerrainPass->Init(*m_ShaderFactory, TerrainPass::CreateParameters(), m_CommandList, heightmapTexture, colorTexture, m_Executor);

	// Shadows
	{
		const nvrhi::Format shadowMapFormats[] = {
			nvrhi::Format::D24S8,
			nvrhi::Format::D32,
			nvrhi::Format::D16,
			nvrhi::Format::D32S8 };

		const nvrhi::FormatSupport shadowMapFeatures =
			nvrhi::FormatSupport::Texture |
			nvrhi::FormatSupport::DepthStencil |
			nvrhi::FormatSupport::ShaderLoad;

		nvrhi::Format shadowMapFormat = nvrhi::utils::ChooseFormat(GetDevice(), shadowMapFeatures, shadowMapFormats, std::size(shadowMapFormats));

		m_ShadowMap = std::make_shared<CascadedShadowMap>(GetDevice(), 2048, 4, 0, shadowMapFormat);
		m_ShadowMap->SetupProxyViews();

		m_ShadowFramebuffer = std::make_shared<engine::FramebufferFactory>(GetDevice());
		m_ShadowFramebuffer->DepthTarget = m_ShadowMap->GetTexture();

		DepthPass::CreateParameters shadowDepthParams;
		shadowDepthParams.slopeScaledDepthBias = 4.f;
		shadowDepthParams.depthBias = 100;
		m_ShadowDepthPass = std::make_shared<DepthPass>(GetDevice(), m_CommonPasses);
		m_ShadowDepthPass->Init(*m_ShaderFactory, shadowDepthParams);
	}
	CreateRenderPasses();

	m_FirstPersonCamera.LookAt(float3(0.0f, 205.0f, 227.4f), float3(1.0f, 1.8f, .0f));
	m_FirstPersonCamera.SetMoveSpeed(m_CameraMoveSpeed);
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
		m_DirectionalLight = std::make_shared<engine::DirectionalLight>();
		m_DirectionalLight->angularSize = .53f;
		m_DirectionalLight->irradiance = 1.0f;

		auto node = std::make_shared<engine::SceneGraphNode>();
		node->SetLeaf(m_DirectionalLight);

		m_DirectionalLight->SetDirection({ -0.9f ,-0.25f , 0.35f });
		m_DirectionalLight->SetName("Sun");

		m_Scene->GetSceneGraph()->Attach(m_Scene->GetSceneGraph()->GetRootNode(), node);
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

bool Renderer::MouseScrollUpdate(double xoffset, double yoffset)
{
	constexpr float speedFactor = 1.5f;

	m_CameraMoveSpeed += static_cast<float>(yoffset) * speedFactor;
	m_CameraMoveSpeed = m_CameraMoveSpeed < 0.0f ? 0.0f : m_CameraMoveSpeed; 
	m_FirstPersonCamera.SetMoveSpeed(m_CameraMoveSpeed);

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

	if (createRenderTargets || m_EditorParams.m_ShaderReoladRequested)
	{
		m_ShaderFactory->ClearCache();
		CreateRenderPasses();
		m_EditorParams.m_ShaderReoladRequested = false;
	}

	RecordCommand(framebuffer);
	Submit();
}

void Renderer::CreateRenderPasses()
{
	m_DeferredLightingPass = std::make_unique<render::DeferredLightingPass>(GetDevice(), m_CommonPasses);
	m_DeferredLightingPass->Init(m_ShaderFactory);

	render::GBufferFillPass::CreateParameters gbufferParams;
	m_GBufferPass = std::make_unique<render::GBufferFillPass>(GetDevice(), m_CommonPasses);
	m_GBufferPass->Init(*m_ShaderFactory, gbufferParams);

	//m_TerrainPass = std::make_unique<TerrainPass>(GetDevice(), m_CommonPasses, m_Editor->GetUIData());
	if (m_TerrainPass)
		m_TerrainPass->CreateShaders(*m_ShaderFactory, TerrainPass::CreateParameters());
	else
		log::warning("Terrain Pass not initialized");

	if (m_RenderTargets)
	{
		m_SkyPass = std::make_unique<render::SkyPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, m_View);

		ToneMappingPass::CreateParameters toneMappingParams;
		m_ToneMappingPass = std::make_unique<render::ToneMappingPass>(GetDevice(), m_ShaderFactory, m_CommonPasses, m_RenderTargets->LdrFramebuffer, m_View, toneMappingParams);
	}
}

void Renderer::SetupProfilingEvents(donut::app::DeviceManager* deviceManager)
{
	PROFILE_REGISTER_THREAD("Main Thread");
	deviceManager->m_callbacks.beforeFrame = [](app::DeviceManager& deviceManager)
		{
			PROFILE_FRAME();
			PROFILE_FRAME_GPU();

            int width;
            int height;
            glfwGetWindowSize(deviceManager.GetWindow(), &width, &height);
            if (width == 0 || height == 0)
            {
                return;
            }

            PROFILE_CPU_BEGIN("Event Poll");
		};
	deviceManager->m_callbacks.beforeAnimate = [](app::DeviceManager&)
		{
            PROFILE_CPU_END();
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

void Renderer::RecordCommand(nvrhi::IFramebuffer* framebuffer)
{
	m_CommandList->open();
	{
		PROFILE_CPU_SCOPE();
		PROFILE_GPU_SCOPE(m_CommandList, "GPU Frame");

		PROFILE_GPU_BEGIN(m_CommandList, "Scene Refresh");
		if (m_Scene && m_Scene->GetSceneGraph())
			m_Scene->RefreshBuffers(m_CommandList, GetFrameIndex()); // Updates any geometry, material, etc buffer changes
		PROFILE_GPU_END(m_CommandList);

		if (false && m_DirectionalLight)
		{
			PROFILE_GPU_SCOPE(m_CommandList, "Cascade ShadowMap");
			m_DirectionalLight->shadowMap = m_ShadowMap;
			box3 sceneBounds = m_Scene->GetSceneGraph()->GetRootNode()->GetGlobalBoundingBox();

			frustum projectionFrustum = m_View.GetProjectionFrustum();
			constexpr float maxShadowDistance = 1024.f;

			dm::affine3 viewMatrixInv = m_View.GetChildView(engine::ViewType::PLANAR, 0)->GetInverseViewMatrix();

			float zRange = length(sceneBounds.diagonal()) * 0.5f;

			float2 maxW = { 1024, 1024 };
			float2 minW = { -1024, -1024 };
			zRange = length(maxW - minW) * 0.5f;

			m_ShadowMap->SetupForPlanarViewStable(*m_DirectionalLight, projectionFrustum, viewMatrixInv, maxShadowDistance, zRange, zRange, 4.0f);

			m_ShadowMap->Clear(m_CommandList);

			if (m_EditorParams.m_RenderTerrain)
			{
				PROFILE_GPU_SCOPE(m_CommandList, "Terrain Shadow");
				vRenderer::TerrainPass::RenderParams renderParams;
				renderParams.wireframe = m_EditorParams.m_Wireframe;
				renderParams.lockView = m_EditorParams.m_LockView;
				renderParams.depthOnly = true;

				m_TerrainPass->Render(
					m_CommandList,
					&m_ShadowMap->GetView(),
					&m_ShadowMap->GetView(),
					*m_ShadowFramebuffer,
					renderParams,
					m_EditorParams);
			}

			DepthPass::Context context;

			RenderCompositeView(m_CommandList,
				&m_ShadowMap->GetView(), nullptr,
				*m_ShadowFramebuffer,
				m_Scene->GetSceneGraph()->GetRootNode(),
				*m_OpaqueDrawStrategy,
				*m_ShadowDepthPass,
				context,
				"ShadowMap",
				true);
		}

		m_RenderTargets->Clear(m_CommandList);

		if (m_Scene && m_Scene->GetSceneGraph())
		{
			render::GBufferFillPass::Context context;
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

		if (m_EditorParams.m_RenderTerrain)
		{
			PROFILE_GPU_SCOPE(m_CommandList, "Terrain");
			vRenderer::TerrainPass::RenderParams renderParams;
			renderParams.wireframe = m_EditorParams.m_Wireframe;
			renderParams.lockView = m_EditorParams.m_LockView;

			m_TerrainPass->Render(
				m_CommandList,
				&m_View,
				&m_View,
				*m_RenderTargets->GBufferFramebuffer,
				renderParams,
				m_EditorParams);
		}

		if (m_Scene && m_Scene->GetSceneGraph())
		{
			PROFILE_GPU_SCOPE(m_CommandList, "Deferred Lighting");
			render::DeferredLightingPass::Inputs deferredInputs;
			deferredInputs.SetGBuffer(*m_RenderTargets);
			deferredInputs.ambientColorTop = m_EditorParams.m_AmbientIntensity;
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
	PROFILE_CPU_SCOPE();
	const Span<nvrhi::CommandListHandle> cmdlists(reinterpret_cast<nvrhi::CommandListHandle*>(&m_CommandList), 1);
	PROFILE_EXECUTE_COMMANDLISTS(cmdlists);
	GetDevice()->executeCommandList(m_CommandList);
}

void Renderer::RenderUI()
{
	if (ImGui::Button("Reload Shaders"))
		m_EditorParams.m_ShaderReoladRequested = true;

	ImGui::Separator();
	ImGui::Text("Terrain");
	ImGui::Separator();
	ImGui::Checkbox("Enable Terrain", &m_EditorParams.m_RenderTerrain);
	ImGui::Checkbox("Wireframe", &m_EditorParams.m_Wireframe);
	ImGui::Checkbox("Lock View", &m_EditorParams.m_LockView);
	ImGui::InputFloat("Max Height", &m_EditorParams.m_MaxHeight, 1.0);
	ImGui::Text("Num instances : %i", m_EditorParams.m_NumChunks);

	if (m_DirectionalLight)
	{
		ImGui::Separator();
		ImGui::Text("Sun Light");
		ImGui::Separator();
		ImGui::InputFloat("Ambient Intensity", &m_EditorParams.m_AmbientIntensity, 0.01f);
		app::LightEditor(*m_DirectionalLight);
	}

	ImGui::Checkbox("Debug QuadTree", &m_EditorParams.m_DebugQuadTree);

	if (m_DirectionalLight)
	{
		if (m_EditorParams.m_DebugQuadTree)
		{
			ImGuiIO& io = ImGui::GetIO();
			ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);

			auto& quadTrees = m_TerrainPass->GetQuadTrees();
			for (const auto& quadTree : quadTrees)
			{
				auto& nodes = quadTree->m_DebugDrawData.culledNodes;
				for (const Node* node : nodes)
				{
					float height = m_EditorParams.m_MaxHeight;
					float3 size = float3(node->m_Extents.x * 2.0f, node->m_Extents.y * 2.0f * height, node->m_Extents.z * 2.0f);
					float3 position = float3(node->m_Position.x, node->m_Position.y * height, node->m_Position.z);

					/*size = node->m_Extents * 2.0f;
					position = node->m_Position;*/

					const float4x4 transform = affineToHomogeneous(scaling(size) * math::translation(position));
					const float4x4 view = affineToHomogeneous(m_View.GetViewMatrix());
					const float4x4 proj = m_View.GetProjectionMatrix(true);

					box3 cube = box3(float3(-0.5f), float3(0.5f)) * homogeneousToAffine(transform);

					ImU32 color = m_View.GetViewFrustum().intersectsWith(cube) ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255);

					ImGuizmo::DrawCubes(view.m_data, proj.m_data, transform.m_data, 1, color);
				}
			}
		}
	}
}
