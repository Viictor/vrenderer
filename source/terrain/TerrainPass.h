#pragma once

#include <donut/engine/View.h>
#include <donut/render/GeometryPasses.h>

#include <mutex>
#include "QuadTree.h"

namespace donut::engine
{
	class ShaderFactory;
	struct LoadedTexture;
	class CommonRenderPasses;
}

using namespace donut;
using namespace donut::render;

namespace vRenderer
{
	struct UIData;

	class TerrainPass : public IGeometryPass
	{
	public:
		union PipelineKey
		{
			struct
			{
				nvrhi::RasterCullMode cullMode : 2;
				nvrhi::RasterFillMode fillMode : 2;
				bool frontCounterClockwise : 1;
				bool reverseDepth : 1;
			} bits;
			uint32_t value;

			static constexpr size_t Count = 1 << 6;
		};

		class Context : public GeometryPassContext
		{
		public:
			nvrhi::BindingSetHandle lightBindingSet;
			PipelineKey keyTemplate;

			Context()
			{
				keyTemplate.value = 0;
			}
		};

		struct CreateParameters
		{
			bool trackLiveness = true;
			uint32_t numConstantBufferVersions = 16;
		};

		struct RenderParams
		{
			bool wireframe = false;
			bool lockView = false;
		};

	protected:
		nvrhi::DeviceHandle m_Device;
		nvrhi::InputLayoutHandle m_InputLayout;

		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;

		nvrhi::BindingLayoutHandle m_ViewBindingLayout;
		nvrhi::BindingSetHandle m_ViewBindingSet;

		nvrhi::BindingLayoutHandle m_HeightmapBindingLayout;
		nvrhi::BindingSetHandle m_HeightmapBindingSet;
		size_t m_HeightmapBindingSetHash = 0;

		nvrhi::BindingLayoutHandle m_LightBindingLayout;

		engine::ViewType::Enum m_SupportedViewTypes = engine::ViewType::PLANAR;

		nvrhi::BufferHandle m_TerrainViewPassCB;
		nvrhi::BufferHandle m_TerrainLightPassCB;
		nvrhi::BufferHandle m_TerrainParamsPassCB;

		nvrhi::GraphicsPipelineHandle m_Pipelines[PipelineKey::Count];
		bool m_TrackLiveness = true;
		std::mutex m_Mutex;
		RenderParams m_RenderParams;
		UIData& m_UIData;

		std::vector<std::shared_ptr<QuadTree>> m_QuadTrees;

		// Terrain Geometry
		std::shared_ptr<engine::BufferGroup> m_Buffers;
		std::shared_ptr<engine::MeshInfo> m_MeshInfo;
		
		struct Resources;
		std::shared_ptr<Resources> m_Resources;
		std::shared_ptr<engine::CommonRenderPasses> m_CommonPasses;

		nvrhi::InputLayoutHandle CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params) const;
		static nvrhi::ShaderHandle CreateVertexShader(engine::ShaderFactory& shaderFactory, const CreateParameters& params);
		static nvrhi::ShaderHandle CreatePixelShader(engine::ShaderFactory& shaderFactory, const CreateParameters& params);

		nvrhi::BindingLayoutHandle CreateViewBindingLayout() const;
		nvrhi::BindingSetHandle CreateViewBindingSet() const;

		nvrhi::BindingLayoutHandle CreateHeightmapBindingLayout() const;
		nvrhi::BindingSetHandle GetOrCreateHeightmapBindingSet();

		nvrhi::BindingLayoutHandle CreateLightBindingLayout() const;
		nvrhi::BindingSetHandle CreateLightBindingSet(nvrhi::ITexture* shadowMapTexture, nvrhi::ITexture* diffuse, nvrhi::ITexture* specular, nvrhi::ITexture* environmentBrdf) const;
		
		nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(PipelineKey key, nvrhi::IFramebuffer* framebuffer);

		static nvrhi::BufferHandle CreateGeometryBuffer(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const char* debugName, const void* data, uint64_t dataSize, bool isVertexBuffer);

		nvrhi::BufferHandle CreateInstanceBuffer(nvrhi::IDevice* device) const;

	public:
		TerrainPass(nvrhi::IDevice* device, std::shared_ptr<engine::CommonRenderPasses> commonPasses, UIData& uiData);
		void Init(engine::ShaderFactory& shaderFactory, 
			const CreateParameters& params, 
			nvrhi::ICommandList* commandList, 
			const std::shared_ptr<engine::LoadedTexture>& heightmapTexture,
			const std::shared_ptr<engine::LoadedTexture>& colorTexture,
			tf::Executor& executor);
		void Render(
			nvrhi::ICommandList* commandList, 
			const engine::ICompositeView* compositeView,
			const engine::ICompositeView* compositeViewPrev,
			engine::FramebufferFactory& framebufferFactory,
			const RenderParams& renderParams
		);

		void UpdateTransforms(const std::shared_ptr<QuadTree>& quadTree, const int instanceDataOffset) const;

		// IGeometryPass implementation
		[[nodiscard]] engine::ViewType::Enum GetSupportedViewTypes() const override;
		void SetupView(GeometryPassContext& context, nvrhi::ICommandList* commandList, const engine::IView* view, const engine::IView* viewPrev) override;
		bool SetupMaterial(GeometryPassContext& context, const engine::Material* material, nvrhi::RasterCullMode cullMode, nvrhi::GraphicsState& state) override;
		void SetupInputBuffers(GeometryPassContext& context, const engine::BufferGroup* buffers, nvrhi::GraphicsState& state) override;
		void SetPushConstants(GeometryPassContext& context, nvrhi::ICommandList* commandList, nvrhi::GraphicsState& state, nvrhi::DrawArguments& args) override { }
	};
}