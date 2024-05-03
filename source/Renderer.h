#pragma once

#include "donut/app/ApplicationBase.h"
#include "donut/app/Camera.h"
#include "donut/engine/Scene.h"
#include <donut/engine/FramebufferFactory.h>

#include "donut/engine/View.h"
#include "donut/render/DeferredLightingPass.h"
#include "donut/render/DrawStrategy.h"
#include "donut/render/GBuffer.h"
#include "donut/render/GBufferFillPass.h"
#include "donut/render/SkyPass.h"
#include "donut/render/ToneMappingPasses.h"
#include "donut/render/CascadedShadowMap.h"
#include "donut/render/DepthPass.h"

#include "terrain/TerrainPass.h"

namespace tf
{
	class Executor;
}

namespace donut::engine
{
	class Scene;
	class BindingCache;
	class ShaderFactory;
}

namespace vRenderer
{
	struct EditorParams
	{
		// Terrain params
		bool m_RenderTerrain = true;
		bool m_Wireframe = false;
		bool m_LockView = false;
		float m_MaxHeight = 400.0f;
		uint32_t m_NumChunks = 0;

		float m_AmbientIntensity = 0.01f;

		bool m_ShaderReoladRequested = false;

		bool m_DebugQuadTree = true;
	};

	class RenderTargets : public donut::render::GBufferRenderTargets
	{
	public:

		nvrhi::TextureHandle LdrColor;
		nvrhi::TextureHandle HdrColor;

		std::shared_ptr<donut::engine::FramebufferFactory> HdrFramebuffer;
		std::shared_ptr<donut::engine::FramebufferFactory> LdrFramebuffer;

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

			LdrFramebuffer = std::make_shared<donut::engine::FramebufferFactory>(device);
			LdrFramebuffer->RenderTargets = { LdrColor };
			LdrFramebuffer->DepthTarget = Depth;

			HdrFramebuffer = std::make_shared<donut::engine::FramebufferFactory>(device);
			HdrFramebuffer->RenderTargets = { HdrColor };

		}

		[[nodiscard]] bool IsUpdateRequired(donut::math::uint2 size, donut::math::uint sampleCount) const
		{
			if (any(m_Size != size) || m_SampleCount != sampleCount)
				return true;

			return false;
		}
	};

	class Renderer : public donut::app::ApplicationBase
	{
	public:
		Renderer(donut::app::DeviceManager* deviceManager, tf::Executor& executor);

		std::shared_ptr<donut::vfs::IFileSystem> GetRootFs() const
		{
			return m_RootFs;
		}

		std::shared_ptr<donut::engine::ShaderFactory> GetShaderFactory() const
		{
			return m_ShaderFactory;
		}

		bool LoadScene(std::shared_ptr<donut::vfs::IFileSystem> fs, const std::filesystem::path& fileName) override;
		void SceneLoaded() override;

		bool KeyboardUpdate(int key, int scancode, int action, int mods) override;
		bool MousePosUpdate(double xpos, double ypos) override;
		bool MouseButtonUpdate(int button, int action, int mods) override;
		bool MouseScrollUpdate(double xoffset, double yoffset) override;
		void Animate(float seconds) override;
		void RenderScene(nvrhi::IFramebuffer* framebuffer) override;

		void RenderUI();

	private:

		const char* m_WindowTitle = "vRenderer";
		bool m_WaitGPUEvent = false;

		std::shared_ptr<donut::vfs::RootFileSystem> m_RootFs;
		std::shared_ptr<donut::engine::ShaderFactory> m_ShaderFactory;

		std::shared_ptr<donut::engine::Scene> m_Scene;

		donut::app::FirstPersonCamera m_FirstPersonCamera;
		float m_CameraMoveSpeed;

		tf::Executor& m_Executor;

		std::unique_ptr<donut::engine::BindingCache> m_BindingCache;
		nvrhi::CommandListHandle m_CommandList;

		// GBuffer Geometry Passes
		std::shared_ptr<RenderTargets> m_RenderTargets;
		std::unique_ptr<donut::render::GBufferFillPass> m_GBufferPass;
		std::unique_ptr<donut::render::DeferredLightingPass> m_DeferredLightingPass;
		std::shared_ptr<donut::render::InstancedOpaqueDrawStrategy> m_OpaqueDrawStrategy;

		std::unique_ptr<donut::render::SkyPass> m_SkyPass;
		std::shared_ptr<donut::engine::DirectionalLight> m_DirectionalLight;

		//Shadows
		std::shared_ptr<CascadedShadowMap> m_ShadowMap;
		std::shared_ptr<engine::FramebufferFactory> m_ShadowFramebuffer;
		std::shared_ptr<DepthPass> m_ShadowDepthPass; // remove

		std::unique_ptr<donut::render::ToneMappingPass> m_ToneMappingPass;

		// Terrain Geometry Pass
		std::unique_ptr<TerrainPass> m_TerrainPass;

		// Basic 2D projection
		donut::engine::PlanarView m_View;

		void CreateRenderPasses();

		void SetupProfilingEvents(donut::app::DeviceManager* deviceManager);
		void UpdateView();
		void RecordCommand(nvrhi::IFramebuffer* framebuffer);
		void Submit();

		EditorParams m_EditorParams;
	};
}