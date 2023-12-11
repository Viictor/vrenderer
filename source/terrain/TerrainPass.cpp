#include "../terrain/TerrainPass.h"
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/core/vfs/VFS.h>
#include <nvrhi/utils.h>

using namespace donut::math;
#include "../../shaders/terrain/terrain_cb.h"

using namespace vRenderer;
using namespace donut;

TerrainPass::TerrainPass(nvrhi::IDevice* device)
	: m_Device(device)
{
}

void TerrainPass::Init(engine::ShaderFactory& shaderFactory, const CreateParameters& params, nvrhi::ICommandList* commandList, std::shared_ptr<engine::LoadedTexture> heightmapTexture)
{
	const float scale = 1.0f;

	m_SupportedViewTypes = engine::ViewType::PLANAR;

	m_VertexShader = CreateVertexShader(shaderFactory, params);
	m_PixelShader = CreatePixelShader(shaderFactory, params);
	m_InputLayout = CreateInputLayout(m_VertexShader, params);

	m_TerrainViewPassCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(TerrainViewConstants), "TerrainViewConstants", params.numConstantBufferVersions));
	m_TerrainLightPassCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(TerrainLightConstants), "TerrainLightConstants", params.numConstantBufferVersions));

	m_ViewBindingLayout = CreateViewBindingLayout();
	m_ViewBindingSet = CreateViewBindingSet();
	m_LightBindingLayout = CreateLightBindingLayout();

	std::vector<float3> vPositions;
	std::vector<uint32_t> vIndices;
	uint32_t vPositionsByteSize = 0;
	if (heightmapTexture)
	{
		engine::TextureData* textureData = static_cast<engine::TextureData*>(heightmapTexture.get());

		const int width = textureData->width;
		const int height = textureData->height;

		//const int side = (int)sqrtf((float)textureData->dataLayout[0][0].dataSize / sizeof(float));
		vPositions.resize(width * height);
		vPositionsByteSize = width * height * sizeof(float3);

		const int halfWidth = width / 2;
		const int halfHeight = height / 2;
		const uint8_t* byteData = reinterpret_cast<const uint8_t*>(textureData->data->data());

		size_t index = 0;
		for (int h = -halfHeight; h < halfHeight; h++)
		{
			for (int w = -halfWidth; w < halfWidth; w++)
			{
				assert(index < vPositions.size());
				vPositions[index] = float3((float) w / halfWidth * scale, (float)byteData[index * 4] / 255.0f, (float)h / halfHeight * scale);
				index++;
			}
		}

		index = 0;
		vIndices.resize((width -1) * (height -1) * 6);
		for (size_t i = 0; i < height - 1; i++)
		{
			for (size_t j = 0; j < width - 1; j++)
			{
				uint32_t indexBottomLeft = i * height + j;
				uint32_t indexTopLeft = (i + 1) * height + j;
				uint32_t indexTopRight = (i + 1) * height + j + 1;
				uint32_t indexBottomRight = i * height + j + 1;

				vIndices[index++] = indexBottomLeft;
				vIndices[index++] = indexTopLeft;
				vIndices[index++] = indexTopRight;

				vIndices[index++] = indexBottomLeft;
				vIndices[index++] = indexTopRight;
				vIndices[index++] = indexBottomRight;
			}
		}
	}
	commandList->open();

	m_Buffers = std::make_shared<engine::BufferGroup>();
	if (heightmapTexture)
	{
		m_Buffers->indexBuffer = CreateGeometryBuffer(m_Device, commandList, "IndexBuffer", vIndices.data(), vIndices.size() * sizeof(uint32_t), false);

		uint64_t vertexBufferSize = 0;
		m_Buffers->getVertexBufferRange(engine::VertexAttribute::Position).setByteOffset(vertexBufferSize).setByteSize(vPositionsByteSize); vertexBufferSize += vPositionsByteSize;
		m_Buffers->getVertexBufferRange(engine::VertexAttribute::Normal).setByteOffset(vertexBufferSize).setByteSize(vPositionsByteSize); vertexBufferSize += vPositionsByteSize;
		m_Buffers->vertexBuffer = CreateGeometryBuffer(m_Device, commandList, "VertexBuffer", nullptr, vertexBufferSize, true);

		commandList->beginTrackingBufferState(m_Buffers->vertexBuffer, nvrhi::ResourceStates::CopyDest);
		commandList->writeBuffer(m_Buffers->vertexBuffer, vPositions.data(), vPositionsByteSize, m_Buffers->getVertexBufferRange(engine::VertexAttribute::Position).byteOffset);
		commandList->writeBuffer(m_Buffers->vertexBuffer, vPositions.data(), vPositionsByteSize, m_Buffers->getVertexBufferRange(engine::VertexAttribute::Normal).byteOffset);
		commandList->setPermanentBufferState(m_Buffers->vertexBuffer, nvrhi::ResourceStates::VertexBuffer);
		commandList->close();
		m_Device->executeCommandList(commandList);
	}

	std::shared_ptr<engine::MeshGeometry> geometry = std::make_shared<engine::MeshGeometry>();
	geometry->material = nullptr;
	geometry->numIndices = vIndices.size();
	geometry->numVertices = vPositions.size();

	m_MeshInfo = std::make_shared<engine::MeshInfo>();
	m_MeshInfo->name = "TerrainMesh";
	m_MeshInfo->buffers = m_Buffers;
	m_MeshInfo->objectSpaceBounds = math::box3(math::float3(-0.5f), math::float3(0.5f));;
	m_MeshInfo->totalIndices = geometry->numIndices;
	m_MeshInfo->totalVertices = geometry->numVertices;
	m_MeshInfo->geometries.push_back(geometry);

	m_MeshInstance = std::make_shared<engine::MeshInstance>(m_MeshInfo);
}

void TerrainPass::Render(nvrhi::ICommandList* commandList, const engine::ICompositeView* compositeView, const engine::ICompositeView* compositeViewPrev, engine::FramebufferFactory& framebufferFactory)
{
	commandList->beginMarker("TerrainPass");

	engine::ViewType::Enum supportedViewTypes = GetSupportedViewTypes();

	if (compositeViewPrev)
	{
		// the views must have the same topology
		assert(compositeView->GetNumChildViews(supportedViewTypes) == compositeViewPrev->GetNumChildViews(supportedViewTypes));
	}

	for (uint viewIndex = 0; viewIndex < compositeView->GetNumChildViews(supportedViewTypes); viewIndex++)
	{
		const engine::IView* view = compositeView->GetChildView(supportedViewTypes, viewIndex);
		const engine::IView* viewPrev = compositeViewPrev ? compositeViewPrev->GetChildView(supportedViewTypes, viewIndex) : nullptr;

		assert(view != nullptr);

		nvrhi::IFramebuffer* framebuffer = framebufferFactory.GetFramebuffer(*view);

		{
			Context passContext;
			SetupView(passContext, commandList, view, viewPrev);

			bool stateValid = false;

			nvrhi::GraphicsState graphicsState;
			graphicsState.framebuffer = framebuffer;
			graphicsState.viewport = view->GetViewportState();
			graphicsState.shadingRateState = view->GetVariableRateShadingState();

			SetupInputBuffers(passContext, m_Buffers.get(), graphicsState);

			const bool drawMaterial = SetupMaterial(passContext, nullptr, nvrhi::RasterCullMode::Back, graphicsState);

			if (drawMaterial)
			{
				commandList->setGraphicsState(graphicsState);

				nvrhi::DrawArguments args;
				args.vertexCount = m_MeshInfo->geometries[0]->numIndices;
				args.instanceCount = 1;
				args.startVertexLocation = m_MeshInfo->vertexOffset + m_MeshInfo->geometries[0]->vertexOffsetInMesh;
				args.startInstanceLocation = m_MeshInstance->GetInstanceIndex();

				args.startIndexLocation = m_MeshInfo->indexOffset + m_MeshInfo->geometries[0]->indexOffsetInMesh;
				
				SetPushConstants(passContext, commandList, graphicsState, args);

				commandList->drawIndexed(args);
			}
		}
	}

	commandList->endMarker();
}

engine::ViewType::Enum TerrainPass::GetSupportedViewTypes() const
{
	return m_SupportedViewTypes;
}

void TerrainPass::SetupView(GeometryPassContext& context, nvrhi::ICommandList* commandList, const engine::IView* view, const engine::IView* viewPrev)
{
	Context& terrainContext = static_cast<Context&>(context);

	TerrainViewConstants viewConstants = {};
	view->FillPlanarViewConstants(viewConstants.view);
	viewPrev->FillPlanarViewConstants(viewConstants.viewPrev);
	commandList->writeBuffer(m_TerrainViewPassCB, &viewConstants, sizeof(viewConstants));

	terrainContext.keyTemplate.bits.frontCounterClockwise = view->IsMirrored();
	terrainContext.keyTemplate.bits.reverseDepth = view->IsReverseDepth();
}

bool TerrainPass::SetupMaterial(GeometryPassContext& context, const engine::Material* material, nvrhi::RasterCullMode cullMode, nvrhi::GraphicsState& state)
{
	Context& terrainContext = static_cast<Context&>(context);

	PipelineKey key = terrainContext.keyTemplate;
	key.bits.cullMode = cullMode;

	nvrhi::GraphicsPipelineHandle& pipeline = m_Pipelines[key.value];

	if (!pipeline)
	{
		std::lock_guard<std::mutex> lockGuard(m_Mutex);

		if (!pipeline)
			pipeline = CreateGraphicsPipeline(key, state.framebuffer);

		if (!pipeline)
			return false;
	}

	assert(pipeline->getFramebufferInfo() == state.framebuffer->getFramebufferInfo());

	state.pipeline = pipeline;
	state.bindings = { m_ViewBindingSet };

	return true;
}

void TerrainPass::SetupInputBuffers(GeometryPassContext& context, const engine::BufferGroup* buffers, nvrhi::GraphicsState& state)
{
	state.vertexBuffers = {
		{buffers->vertexBuffer, 0, buffers->getVertexBufferRange(engine::VertexAttribute::Position).byteOffset },
		{buffers->vertexBuffer, 1, buffers->getVertexBufferRange(engine::VertexAttribute::Normal).byteOffset },
	};

	state.indexBuffer = { buffers->indexBuffer, nvrhi::Format::R32_UINT, 0 };
}

nvrhi::InputLayoutHandle TerrainPass::CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params)
{
	const nvrhi::VertexAttributeDesc inputDescs[] =
	{
		engine::GetVertexAttributeDesc(engine::VertexAttribute::Position, "POS", 0),
		engine::GetVertexAttributeDesc(engine::VertexAttribute::Normal, "NORMAL", 1)
	};

	return m_Device->createInputLayout(inputDescs, uint32_t(std::size(inputDescs)), vertexShader);
}

nvrhi::ShaderHandle TerrainPass::CreateVertexShader(engine::ShaderFactory& shaderFactory, const CreateParameters& params)
{
	std::vector<engine::ShaderMacro> VertexShaderMacros;
	VertexShaderMacros.push_back(engine::ShaderMacro("MOTION_VECTORS", "0"));
	return shaderFactory.CreateShader("/shaders/vrenderer/terrain/terrainPass.hlsl", "main_vs", nullptr, nvrhi::ShaderType::Vertex);
}

nvrhi::ShaderHandle TerrainPass::CreatePixelShader(engine::ShaderFactory& shaderFactory, const CreateParameters& params)
{
	std::vector<engine::ShaderMacro> PixelShaderMacros;
	PixelShaderMacros.push_back(engine::ShaderMacro("MOTION_VECTORS", "0"));
	return shaderFactory.CreateShader("/shaders/vrenderer/terrain/terrainPass.hlsl", "main_ps", nullptr, nvrhi::ShaderType::Pixel);
}

nvrhi::BindingLayoutHandle TerrainPass::CreateViewBindingLayout()
{
	nvrhi::BindingLayoutDesc viewLayoutDescs;
	viewLayoutDescs.visibility = nvrhi::ShaderType::All;
	viewLayoutDescs.bindings = {
		nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
		//nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
	};

	return m_Device->createBindingLayout(viewLayoutDescs);
}

nvrhi::BindingSetHandle TerrainPass::CreateViewBindingSet()
{
	nvrhi::BindingSetDesc bindingSetDescs;
	bindingSetDescs.bindings = {
		nvrhi::BindingSetItem::ConstantBuffer(1, m_TerrainViewPassCB),
		//nvrhi::BindingSetItem::ConstantBuffer(2, m_TerrainLightPassCB),
	};
	bindingSetDescs.trackLiveness = m_TrackLiveness;

	return m_Device->createBindingSet(bindingSetDescs, m_ViewBindingLayout);
}

nvrhi::BindingLayoutHandle TerrainPass::CreateLightBindingLayout()
{
	// to do when shadowmaps

	nvrhi::BindingLayoutDesc viewLayoutDescs;
	viewLayoutDescs.visibility = nvrhi::ShaderType::All;
	viewLayoutDescs.bindings = {
		nvrhi::BindingLayoutItem::VolatileConstantBuffer(1),
		nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
	};

	return m_Device->createBindingLayout(viewLayoutDescs);
}

nvrhi::BindingSetHandle TerrainPass::CreateLightBindingSet(nvrhi::ITexture* shadowMapTexture, nvrhi::ITexture* diffuse, nvrhi::ITexture* specular, nvrhi::ITexture* environmentBrdf)
{
	// to do when shadomaps

	nvrhi::BindingSetDesc bindingSetDescs;
	bindingSetDescs.bindings = {
		nvrhi::BindingSetItem::ConstantBuffer(1, m_TerrainViewPassCB),
		//nvrhi::BindingSetItem::ConstantBuffer(2, m_TerrainLightPassCB),
	};
	bindingSetDescs.trackLiveness = m_TrackLiveness;

	return m_Device->createBindingSet(bindingSetDescs, m_LightBindingLayout);
}

nvrhi::GraphicsPipelineHandle TerrainPass::CreateGraphicsPipeline(PipelineKey key, nvrhi::IFramebuffer* framebuffer)
{
	nvrhi::GraphicsPipelineDesc pipelineDescs;
	pipelineDescs.inputLayout = m_InputLayout;
	pipelineDescs.VS = m_VertexShader;
	pipelineDescs.PS = m_PixelShader;
	pipelineDescs.renderState.rasterState.frontCounterClockwise = key.bits.frontCounterClockwise;
	pipelineDescs.renderState.rasterState.setCullMode(key.bits.cullMode);
	pipelineDescs.bindingLayouts = { m_ViewBindingLayout };
	pipelineDescs.renderState.depthStencilState
		.setDepthWriteEnable(true)
		.setDepthFunc(key.bits.reverseDepth 
			? nvrhi::ComparisonFunc::GreaterOrEqual 
			: nvrhi::ComparisonFunc::LessOrEqual);

	return m_Device->createGraphicsPipeline(pipelineDescs, framebuffer);
}

nvrhi::BufferHandle vRenderer::TerrainPass::CreateGeometryBuffer(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const char* debugName, const void* data, uint64_t dataSize, bool isVertexBuffer)
{
	nvrhi::BufferHandle bufHandle;

	nvrhi::BufferDesc desc;
	desc.byteSize = dataSize;
	desc.isVertexBuffer = isVertexBuffer;
	desc.isIndexBuffer = !isVertexBuffer;
	desc.debugName = debugName;
	desc.initialState = nvrhi::ResourceStates::CopyDest;
	bufHandle = device->createBuffer(desc);

	if (data)
	{
		commandList->beginTrackingBufferState(bufHandle, nvrhi::ResourceStates::CopyDest);
		commandList->writeBuffer(bufHandle, data, dataSize);
		commandList->setPermanentBufferState(bufHandle, isVertexBuffer ? nvrhi::ResourceStates::VertexBuffer : nvrhi::ResourceStates::IndexBuffer);
	}

	return bufHandle;
}
