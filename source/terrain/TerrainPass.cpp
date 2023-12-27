#include "../terrain/TerrainPass.h"
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/SceneGraph.h>
#include <donut/engine/TextureCache.h>
#include <donut/engine/FramebufferFactory.h>
#include <donut/engine/CommonRenderPasses.h>
#include <donut/core/vfs/VFS.h>
#include <nvrhi/utils.h>
#include <donut/shaders/bindless.h>

#ifdef DONUT_WITH_TASKFLOW
#include <taskflow/taskflow.hpp>
#endif

#include "../UIRenderer.h"

using namespace donut::math;
#include "../../shaders/terrain/terrain_cb.h"

using namespace vRenderer;
using namespace donut;

#define MAX_INSTANCES 4096
#define WORLD_SIZE 256.0f
#define GRID_SIZE 32

struct TerrainPass::Resources
{
	std::vector<InstanceData> instanceData;
	std::shared_ptr<engine::LoadedTexture> heightmapTexture;
};

TerrainPass::TerrainPass(nvrhi::IDevice* device, std::shared_ptr<engine::CommonRenderPasses> commonPasses, UIData& uiData)
	: m_Device(device)
	, m_CommonPasses(std::move(commonPasses))
	, m_UIData(uiData)
{
	m_QuadTree = std::make_shared<QuadTree>(WORLD_SIZE, WORLD_SIZE);

	m_Resources = std::make_shared<Resources>();
	m_Resources->instanceData.resize((MAX_INSTANCES));
}

#ifdef DONUT_WITH_TASKFLOW
void TerrainPass::Init(engine::ShaderFactory& shaderFactory, const CreateParameters& params, nvrhi::ICommandList* commandList, std::shared_ptr<engine::LoadedTexture> heightmapTexture, tf::Executor& executor)
{
	m_SupportedViewTypes = engine::ViewType::PLANAR;

	m_VertexShader = CreateVertexShader(shaderFactory, params);
	m_PixelShader = CreatePixelShader(shaderFactory, params);
	m_InputLayout = CreateInputLayout(m_VertexShader, params);

	m_TerrainViewPassCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(TerrainViewConstants), "TerrainViewConstants", params.numConstantBufferVersions));
	m_TerrainLightPassCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(TerrainLightConstants), "TerrainLightConstants", params.numConstantBufferVersions));
	m_TerrainParamsPassCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(TerrainParamsConstants), "TerrainParamsConstants", params.numConstantBufferVersions));

	m_ViewBindingLayout = CreateViewBindingLayout();
	m_ViewBindingSet = CreateViewBindingSet();
	m_LightBindingLayout = CreateLightBindingLayout();

	std::vector<float3> vPositions;
	std::vector<uint32_t> vIndices;
	uint32_t vPositionsByteSize = 0;

	const int sideSize = GRID_SIZE + 1;
	const int halfSize = GRID_SIZE / 2;
	vPositions.resize(sideSize * sideSize);
	vPositionsByteSize = sideSize * sideSize * sizeof(float3);

	size_t index = 0;
	for (int h = -halfSize; h <= halfSize; h++)
	{
		for (int w = -halfSize; w <= halfSize; w++)
		{
			assert(index < vPositions.size());
			vPositions[index] = float3((float)w / halfSize, 0.0f, (float)h / halfSize);
			index++;
		}
	}

	index = 0;
	vIndices.resize((sideSize - 1) * (sideSize - 1) * 6);
	for (int i = 0; i < sideSize - 1; i++)
	{
		for (int j = 0; j < sideSize - 1; j++)
		{
			uint32_t indexBottomLeft = i * sideSize + j;
			uint32_t indexTopLeft = (i + 1) * sideSize + j;
			uint32_t indexTopRight = (i + 1) * sideSize + j + 1;
			uint32_t indexBottomRight = i * sideSize + j + 1;

			vIndices[index++] = indexBottomLeft;
			vIndices[index++] = indexTopLeft;
			vIndices[index++] = indexTopRight;

			vIndices[index++] = indexBottomLeft;
			vIndices[index++] = indexTopRight;
			vIndices[index++] = indexBottomRight;
		}
	}

	m_Resources->heightmapTexture = heightmapTexture;
	m_HeightmapBindingLayout = CreateHeightmapBindingLayout();
	
	m_QuadTree->Init(m_Resources->heightmapTexture, executor);


	commandList->open();

	m_Buffers = std::make_shared<engine::BufferGroup>();
	m_Buffers->instanceBuffer = CreateInstanceBuffer(m_Device);

	m_Buffers->indexBuffer = CreateGeometryBuffer(m_Device, commandList, "IndexBuffer", vIndices.data(), vIndices.size() * sizeof(uint32_t), false);

	uint64_t vertexBufferSize = 0;
	m_Buffers->getVertexBufferRange(engine::VertexAttribute::Position).setByteOffset(vertexBufferSize).setByteSize(vPositionsByteSize); vertexBufferSize += vPositionsByteSize;
	m_Buffers->vertexBuffer = CreateGeometryBuffer(m_Device, commandList, "VertexBuffer", nullptr, vertexBufferSize, true);

	commandList->beginTrackingBufferState(m_Buffers->vertexBuffer, nvrhi::ResourceStates::CopyDest);
	commandList->writeBuffer(m_Buffers->vertexBuffer, vPositions.data(), vPositionsByteSize, m_Buffers->getVertexBufferRange(engine::VertexAttribute::Position).byteOffset);
	commandList->setPermanentBufferState(m_Buffers->vertexBuffer, nvrhi::ResourceStates::VertexBuffer);
	commandList->close();
	m_Device->executeCommandList(commandList);

	std::shared_ptr<engine::MeshGeometry> geometry = std::make_shared<engine::MeshGeometry>();
	geometry->material = nullptr;
	geometry->numIndices = static_cast<uint32_t>(vIndices.size());
	geometry->numVertices = static_cast<uint32_t>(vPositions.size());

	m_MeshInfo = std::make_shared<engine::MeshInfo>();
	m_MeshInfo->name = "TerrainMesh";
	m_MeshInfo->buffers = m_Buffers;
	m_MeshInfo->objectSpaceBounds = math::box3(math::float3(-0.5f), math::float3(0.5f));
	m_MeshInfo->totalIndices = geometry->numIndices;
	m_MeshInfo->totalVertices = geometry->numVertices;
	m_MeshInfo->geometries.push_back(geometry);
}
#endif

void TerrainPass::Init(engine::ShaderFactory& shaderFactory, const CreateParameters& params, nvrhi::ICommandList* commandList, std::shared_ptr<engine::LoadedTexture> heightmapTexture)
{
	m_SupportedViewTypes = engine::ViewType::PLANAR;

	m_VertexShader = CreateVertexShader(shaderFactory, params);
	m_PixelShader = CreatePixelShader(shaderFactory, params);
	m_InputLayout = CreateInputLayout(m_VertexShader, params);

	m_TerrainViewPassCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(TerrainViewConstants), "TerrainViewConstants", params.numConstantBufferVersions));
	m_TerrainLightPassCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(TerrainLightConstants), "TerrainLightConstants", params.numConstantBufferVersions));
	m_TerrainParamsPassCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(TerrainParamsConstants), "TerrainParamsConstants", params.numConstantBufferVersions));

	m_ViewBindingLayout = CreateViewBindingLayout();
	m_ViewBindingSet = CreateViewBindingSet();
	m_LightBindingLayout = CreateLightBindingLayout();

	std::vector<float3> vPositions;
	std::vector<uint32_t> vIndices;
	uint32_t vPositionsByteSize = 0;
	
	const int sideSize = GRID_SIZE + 1;
	const int halfSize = GRID_SIZE / 2;
	vPositions.resize(sideSize * sideSize);
	vPositionsByteSize = sideSize * sideSize * sizeof(float3);

	size_t index = 0;
	for (int h = -halfSize; h <= halfSize; h++)
	{
		for (int w = -halfSize; w <= halfSize; w++)
		{
			assert(index < vPositions.size());
			vPositions[index] = float3((float)w / halfSize, 0.0f, (float)h / halfSize);
			index++;
		}
	}

	index = 0;
	vIndices.resize((sideSize - 1) * (sideSize - 1) * 6);
	for (int i = 0; i < sideSize - 1; i++)
	{
		for (int j = 0; j < sideSize - 1; j++)
		{
			uint32_t indexBottomLeft = i * sideSize + j;
			uint32_t indexTopLeft = (i + 1) * sideSize + j;
			uint32_t indexTopRight = (i + 1) * sideSize + j + 1;
			uint32_t indexBottomRight = i * sideSize + j + 1;

			vIndices[index++] = indexBottomLeft;
			vIndices[index++] = indexTopLeft;
			vIndices[index++] = indexTopRight;

			vIndices[index++] = indexBottomLeft;
			vIndices[index++] = indexTopRight;
			vIndices[index++] = indexBottomRight;
		}
	}

	m_Resources->heightmapTexture = heightmapTexture;
	m_HeightmapBindingLayout = CreateHeightmapBindingLayout();
	engine::TextureData* textureData = static_cast<engine::TextureData*>(m_Resources->heightmapTexture.get());

	m_QuadTree->Init(m_Resources->heightmapTexture);

	commandList->open();

	m_Buffers = std::make_shared<engine::BufferGroup>();
	m_Buffers->instanceBuffer = CreateInstanceBuffer(m_Device);

	m_Buffers->indexBuffer = CreateGeometryBuffer(m_Device, commandList, "IndexBuffer", vIndices.data(), vIndices.size() * sizeof(uint32_t), false);

	uint64_t vertexBufferSize = 0;
	m_Buffers->getVertexBufferRange(engine::VertexAttribute::Position).setByteOffset(vertexBufferSize).setByteSize(vPositionsByteSize); vertexBufferSize += vPositionsByteSize;
	m_Buffers->vertexBuffer = CreateGeometryBuffer(m_Device, commandList, "VertexBuffer", nullptr, vertexBufferSize, true);

	commandList->beginTrackingBufferState(m_Buffers->vertexBuffer, nvrhi::ResourceStates::CopyDest);
	commandList->writeBuffer(m_Buffers->vertexBuffer, vPositions.data(), vPositionsByteSize, m_Buffers->getVertexBufferRange(engine::VertexAttribute::Position).byteOffset);
	commandList->setPermanentBufferState(m_Buffers->vertexBuffer, nvrhi::ResourceStates::VertexBuffer);
	commandList->close();
	m_Device->executeCommandList(commandList);

	std::shared_ptr<engine::MeshGeometry> geometry = std::make_shared<engine::MeshGeometry>();
	geometry->material = nullptr;
	geometry->numIndices = static_cast<uint32_t>(vIndices.size());
	geometry->numVertices = static_cast<uint32_t>(vPositions.size());

	m_MeshInfo = std::make_shared<engine::MeshInfo>();
	m_MeshInfo->name = "TerrainMesh";
	m_MeshInfo->buffers = m_Buffers;
	m_MeshInfo->objectSpaceBounds = math::box3(math::float3(-0.5f), math::float3(0.5f));
	m_MeshInfo->totalIndices = geometry->numIndices;
	m_MeshInfo->totalVertices = geometry->numVertices;
	m_MeshInfo->geometries.push_back(geometry);
}

void TerrainPass::Render(nvrhi::ICommandList* commandList, const engine::ICompositeView* compositeView, const engine::ICompositeView* compositeViewPrev, engine::FramebufferFactory& framebufferFactory, const RenderParams& renderParams)
{
	commandList->beginMarker("TerrainPass");

	m_RenderParams = renderParams;

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

		if (!m_RenderParams.lockView)
		{
			m_QuadTree->ClearSelectedNodes();
			m_QuadTree->NodeSelect(float3(view->GetViewOrigin()), m_QuadTree->GetRootNode().get(), m_QuadTree->GetNumLods() - 1, view->GetViewFrustum(), m_UIData.m_MaxHeight);

			Update(commandList);
		}
		//m_QuadTree->PrintSelected();

		nvrhi::IFramebuffer* framebuffer = framebufferFactory.GetFramebuffer(*view);

		auto nodes = m_QuadTree->GetSelectedNodes();
		assert(int(nodes.size()) < MAX_INSTANCES);
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
				args.instanceCount = nodes.size();
				args.startVertexLocation = m_MeshInfo->vertexOffset + m_MeshInfo->geometries[0]->vertexOffsetInMesh;
				args.startInstanceLocation = 0;// m_MeshInstance->GetInstanceIndex();

				args.startIndexLocation = m_MeshInfo->indexOffset + m_MeshInfo->geometries[0]->indexOffsetInMesh;
				
				SetPushConstants(passContext, commandList, graphicsState, args);

				commandList->drawIndexed(args);
			}
		}
	}

	commandList->endMarker();
}

void vRenderer::TerrainPass::Update(nvrhi::ICommandList* commandList)
{
	// Update Instances
	auto nodes = m_QuadTree->GetSelectedNodes();
	assert(int(nodes.size()) < MAX_INSTANCES);

	for (int i = 0; i < nodes.size(); i++)
	{
		const Node* node = nodes[i];
		InstanceData& idata = m_Resources->instanceData[i];

		math::affine3 scale = math::scaling(node->m_Extents);
		math::affine3 translation = math::translation(node->m_Position);
		math::affine3 transform = scale * translation;

		affineToColumnMajor(transform, idata.transform);
		idata.firstGeometryInstanceIndex = 0;
		idata.firstGeometryIndex = 0;
		idata.numGeometries = 1;
		idata.padding = 0u;
	}

	commandList->writeBuffer(m_Buffers->instanceBuffer, m_Resources->instanceData.data(), m_Resources->instanceData.size() * sizeof(InstanceData));
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

	TerrainParamsConstants paramsConstants = {};
	paramsConstants.size = WORLD_SIZE;
	paramsConstants.maxHeight = m_UIData.m_MaxHeight;

	auto lodRanges = m_QuadTree->GetLodRanges();
	for (int i = 0; i < lodRanges.size(); i++)
	{
		paramsConstants.lodRanges[i].x = lodRanges[i];
	}
	commandList->writeBuffer(m_TerrainViewPassCB, &viewConstants, sizeof(viewConstants));
	commandList->writeBuffer(m_TerrainParamsPassCB, &paramsConstants, sizeof(paramsConstants));

	terrainContext.keyTemplate.bits.frontCounterClockwise = view->IsMirrored();
	terrainContext.keyTemplate.bits.reverseDepth = view->IsReverseDepth();
}

bool TerrainPass::SetupMaterial(GeometryPassContext& context, const engine::Material* material, nvrhi::RasterCullMode cullMode, nvrhi::GraphicsState& state)
{
	Context& terrainContext = static_cast<Context&>(context);

	PipelineKey key = terrainContext.keyTemplate;
	key.bits.cullMode = cullMode;
	key.bits.fillMode = m_RenderParams.wireframe ? nvrhi::RasterFillMode::Wireframe : nvrhi::RasterFillMode::Fill;

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

	m_HeightmapBindingSet = GetOrCreateHeightmapBindingSet();
	
	state.pipeline = pipeline;
	state.bindings = { m_ViewBindingSet, m_HeightmapBindingSet };

	return true;
}

void TerrainPass::SetupInputBuffers(GeometryPassContext& context, const engine::BufferGroup* buffers, nvrhi::GraphicsState& state)
{
	state.vertexBuffers = {
		{buffers->vertexBuffer, 0, buffers->getVertexBufferRange(engine::VertexAttribute::Position).byteOffset },
		{buffers->instanceBuffer, 1, 0 },
	};

	state.indexBuffer = { buffers->indexBuffer, nvrhi::Format::R32_UINT, 0 };
}

nvrhi::InputLayoutHandle TerrainPass::CreateInputLayout(nvrhi::IShader* vertexShader, const CreateParameters& params)
{
	const nvrhi::VertexAttributeDesc inputDescs[] =
	{
		engine::GetVertexAttributeDesc(engine::VertexAttribute::Position, "POS", 0),
		engine::GetVertexAttributeDesc(engine::VertexAttribute::Transform, "TRANSFORM", 1)
	};

	return m_Device->createInputLayout(inputDescs, dim(inputDescs), vertexShader);
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
		nvrhi::BindingLayoutItem::VolatileConstantBuffer(2),
	};

	return m_Device->createBindingLayout(viewLayoutDescs);
}

nvrhi::BindingSetHandle TerrainPass::CreateViewBindingSet()
{
	nvrhi::BindingSetDesc bindingSetDescs;
	bindingSetDescs.bindings = {
		nvrhi::BindingSetItem::ConstantBuffer(1, m_TerrainViewPassCB),
		nvrhi::BindingSetItem::ConstantBuffer(2, m_TerrainParamsPassCB),
	};
	bindingSetDescs.trackLiveness = m_TrackLiveness;

	return m_Device->createBindingSet(bindingSetDescs, m_ViewBindingLayout);
}

nvrhi::BindingLayoutHandle vRenderer::TerrainPass::CreateHeightmapBindingLayout()
{
	nvrhi::BindingLayoutDesc heightmapLayoutDescs;
	heightmapLayoutDescs.visibility = nvrhi::ShaderType::All;
	heightmapLayoutDescs.bindings = {
		nvrhi::BindingLayoutItem::Texture_SRV(0),
		nvrhi::BindingLayoutItem::Sampler(0)
	};

	return m_Device->createBindingLayout(heightmapLayoutDescs);
}

nvrhi::BindingSetHandle vRenderer::TerrainPass::GetOrCreateHeightmapBindingSet()
{
	nvrhi::BindingSetDesc bindingSetDescs;
	bindingSetDescs.bindings = {
		nvrhi::BindingSetItem::Texture_SRV(0, m_Resources->heightmapTexture->texture ? m_Resources->heightmapTexture->texture : m_CommonPasses->m_BlackTexture, nvrhi::Format::UNKNOWN),
		nvrhi::BindingSetItem::Sampler(0, m_CommonPasses->m_LinearWrapSampler)
	};

	size_t hash = 0;
	nvrhi::hash_combine(hash, bindingSetDescs);

	if (m_HeightmapBindingSetHash != hash)
	{
		m_HeightmapBindingSetHash = hash;
		return m_Device->createBindingSet(bindingSetDescs, m_HeightmapBindingLayout);
	}
	else
		return m_HeightmapBindingSet;
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
	pipelineDescs.renderState.rasterState.setFillMode(key.bits.fillMode);
	pipelineDescs.bindingLayouts = { m_ViewBindingLayout, m_HeightmapBindingLayout };
	pipelineDescs.renderState.depthStencilState
		.setDepthWriteEnable(true)
		.setDepthFunc(key.bits.reverseDepth 
			? nvrhi::ComparisonFunc::GreaterOrEqual 
			: nvrhi::ComparisonFunc::LessOrEqual);

	return m_Device->createGraphicsPipeline(pipelineDescs, framebuffer);
}

nvrhi::BufferHandle TerrainPass::CreateGeometryBuffer(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const char* debugName, const void* data, uint64_t dataSize, bool isVertexBuffer)
{
	nvrhi::BufferHandle bufHandle;

	nvrhi::BufferDesc desc;
	desc.byteSize = dataSize;
	desc.isVertexBuffer = isVertexBuffer;
	desc.isIndexBuffer = !isVertexBuffer;
	desc.debugName = debugName;
	//desc.initialState = nvrhi::ResourceStates::CopyDest;
	bufHandle = device->createBuffer(desc);

	if (data)
	{
		commandList->beginTrackingBufferState(bufHandle, nvrhi::ResourceStates::CopyDest);
		commandList->writeBuffer(bufHandle, data, dataSize);
		commandList->setPermanentBufferState(bufHandle, isVertexBuffer ? nvrhi::ResourceStates::VertexBuffer : nvrhi::ResourceStates::IndexBuffer);
	}

	return bufHandle;
}

nvrhi::BufferHandle TerrainPass::CreateInstanceBuffer(nvrhi::IDevice* device)
{
	nvrhi::BufferDesc bufferDesc;
	bufferDesc.byteSize = sizeof(InstanceData) * m_Resources->instanceData.size();
	bufferDesc.debugName = "Instances";
	bufferDesc.structStride = /*m_EnableBindlessResources*/true ? sizeof(InstanceData) : 0;
	bufferDesc.canHaveRawViews = true;
	bufferDesc.canHaveUAVs = true;
	bufferDesc.isVertexBuffer = true;
	bufferDesc.initialState = nvrhi::ResourceStates::ShaderResource;
	bufferDesc.keepInitialState = true;

	return device->createBuffer(bufferDesc);
}
