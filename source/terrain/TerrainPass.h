#pragma once

#include <donut/engine/View.h>
#include <donut/render/GeometryPasses.h>
#include <mutex>

namespace donut::engine
{
	class ShaderFactory;
	struct LoadedTexture;
}

using namespace donut;
using namespace donut::render;

namespace vRenderer
{
	class TerrainPass : public IGeometryPass
	{
	public:
		union PipelineKey
		{
			struct
			{
				nvrhi::RasterCullMode cullMode : 2;
				bool frontCounterClockwise : 1;
				bool reverseDepth : 1;
			} bits;
			uint32_t value;

			static constexpr size_t Count = 1 << 4;
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

	protected:
		nvrhi::DeviceHandle m_Device;
		nvrhi::InputLayoutHandle m_InputLayout;

		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;

		nvrhi::BindingLayoutHandle m_ViewBindingLayout;
		nvrhi::BindingSetHandle m_ViewBindingSet;

		nvrhi::BindingLayoutHandle m_LightBindingLayout;

		engine::ViewType::Enum m_SupportedViewTypes = engine::ViewType::PLANAR;

		nvrhi::BufferHandle m_TerrainViewPassCB;
		nvrhi::BufferHandle m_TerrainLightPassCB;

		nvrhi::GraphicsPipelineHandle m_Pipelines[PipelineKey::Count];
		bool m_TrackLiveness = true;
		std::mutex m_Mutex;

		// Terrain Geometry
		std::shared_ptr<engine::BufferGroup> m_Buffers;
		std::shared_ptr<engine::MeshInfo> m_MeshInfo;
		std::shared_ptr<engine::MeshInstance> m_MeshInstance;

		nvrhi::InputLayoutHandle CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params);
		nvrhi::ShaderHandle CreateVertexShader(engine::ShaderFactory& shaderFactory, const CreateParameters& params);
		nvrhi::ShaderHandle CreatePixelShader(engine::ShaderFactory& shaderFactory, const CreateParameters& params);

		nvrhi::BindingLayoutHandle CreateViewBindingLayout();
		nvrhi::BindingSetHandle CreateViewBindingSet();

		nvrhi::BindingLayoutHandle CreateLightBindingLayout();
		nvrhi::BindingSetHandle CreateLightBindingSet(nvrhi::ITexture* shadowMapTexture, nvrhi::ITexture* diffuse, nvrhi::ITexture* specular, nvrhi::ITexture* environmentBrdf);
		
		nvrhi::GraphicsPipelineHandle CreateGraphicsPipeline(PipelineKey key, nvrhi::IFramebuffer* framebuffer);

		nvrhi::BufferHandle CreateGeometryBuffer(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const char* debugName, const void* data, uint64_t dataSize, bool isVertexBuffer);

	public:
		TerrainPass(nvrhi::IDevice* device);

		void Init(engine::ShaderFactory& shaderFactory, const CreateParameters& params, nvrhi::ICommandList* commandList, std::shared_ptr<engine::LoadedTexture> heightmapTexture);
		void Render(
			nvrhi::ICommandList* commandList, 
			const engine::ICompositeView* compositeView,
			const engine::ICompositeView* compositeViewPrev,
			engine::FramebufferFactory& framebufferFactory
		);

		// IGeometryPass implementation
		[[nodiscard]] engine::ViewType::Enum GetSupportedViewTypes() const override;
		void SetupView(GeometryPassContext& context, nvrhi::ICommandList* commandList, const engine::IView* view, const engine::IView* viewPrev) override;
		bool SetupMaterial(GeometryPassContext& context, const engine::Material* material, nvrhi::RasterCullMode cullMode, nvrhi::GraphicsState& state) override;
		void SetupInputBuffers(GeometryPassContext& context, const engine::BufferGroup* buffers, nvrhi::GraphicsState& state) override;
		void SetPushConstants(GeometryPassContext& context, nvrhi::ICommandList* commandList, nvrhi::GraphicsState& state, nvrhi::DrawArguments& args) override { }

		const engine::MeshInstance* GetMeshInstance() { return m_MeshInstance.get(); };
	};
}