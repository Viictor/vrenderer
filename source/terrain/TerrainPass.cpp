#include "../terrain/TerrainPass.h"
#include <donut/engine/ShaderFactory.h>
#include <donut/engine/SceneTypes.h>
#include <donut/engine/SceneGraph.h>
#include <nvrhi/utils.h>

using namespace donut::math;
#include "../../shaders/terrain/terrain_cb.h"

using namespace vRenderer;
using namespace donut;

TerrainPass::TerrainPass(nvrhi::IDevice* device)
	: m_Device(device)
{
}

void TerrainPass::Init(engine::ShaderFactory& shaderFactory, const CreateParameters& params, nvrhi::ICommandList* commandList)
{
	m_SupportedViewTypes = engine::ViewType::PLANAR;

	m_VertexShader = CreateVertexShader(shaderFactory, params);
	m_PixelShader = CreatePixelShader(shaderFactory, params);
	m_InputLayout = CreateInputLayout(m_VertexShader, params);

	m_TerrainViewPassCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(TerrainViewConstants), "TerrainViewConstants", params.numConstantBufferVersions));
	m_TerrainLightPassCB = m_Device->createBuffer(nvrhi::utils::CreateVolatileConstantBufferDesc(sizeof(TerrainLightConstants), "TerrainLightConstants", params.numConstantBufferVersions));

	m_ViewBindingLayout = CreateViewBindingLayout();
	m_ViewBindingSet = CreateViewBindingSet();
	m_LightBindingLayout = CreateLightBindingLayout();


	commandList->open();
	// Terrain Geo data
	static const uint32_t g_Indices[] = {
		 0,  1,  2,   0,  3,  1, // front face
		 4,  5,  6,   4,  7,  5, // left face
		 8,  9, 10,   8, 11,  9, // right face
		12, 13, 14,  12, 15, 13, // back face
		16, 17, 18,  16, 19, 17, // top face
		20, 21, 22,  20, 23, 21, // bottom face
	};
	static const donut::math::float3 g_Positions[] = {
	{-0.5f,  0.5f, -0.5f}, // front face
	{ 0.5f, -0.5f, -0.5f},
	{-0.5f, -0.5f, -0.5f},
	{ 0.5f,  0.5f, -0.5f},

	{ 0.5f, -0.5f, -0.5f}, // right side face
	{ 0.5f,  0.5f,  0.5f},
	{ 0.5f, -0.5f,  0.5f},
	{ 0.5f,  0.5f, -0.5f},

	{-0.5f,  0.5f,  0.5f}, // left side face
	{-0.5f, -0.5f, -0.5f},
	{-0.5f, -0.5f,  0.5f},
	{-0.5f,  0.5f, -0.5f},

	{ 0.5f,  0.5f,  0.5f}, // back face
	{-0.5f, -0.5f,  0.5f},
	{ 0.5f, -0.5f,  0.5f},
	{-0.5f,  0.5f,  0.5f},

	{-0.5f,  0.5f, -0.5f}, // top face
	{ 0.5f,  0.5f,  0.5f},
	{ 0.5f,  0.5f, -0.5f},
	{-0.5f,  0.5f,  0.5f},

	{ 0.5f, -0.5f,  0.5f}, // bottom face
	{-0.5f, -0.5f, -0.5f},
	{ 0.5f, -0.5f, -0.5f},
	{-0.5f, -0.5f,  0.5f},
	};
	static const donut::math::uint g_Normals[] = {
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 0.0f, -1.0f, 0.0f)), // front face
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 0.0f, -1.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 0.0f, -1.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 0.0f, -1.0f, 0.0f)),

		donut::math::vectorToSnorm8(donut::math::float4(1.0f, 0.0f, 0.0f, 0.0f)), // right side face
		donut::math::vectorToSnorm8(donut::math::float4(1.0f, 0.0f, 0.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(1.0f, 0.0f, 0.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(1.0f, 0.0f, 0.0f, 0.0f)),

		donut::math::vectorToSnorm8(donut::math::float4(-1.0f, 0.0f, 0.0f, 0.0f)), // left side face
		donut::math::vectorToSnorm8(donut::math::float4(-1.0f, 0.0f, 0.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(-1.0f, 0.0f, 0.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(-1.0f, 0.0f, 0.0f, 0.0f)),

		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 0.0f, 1.0f, 0.0f)), // back face
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 0.0f, 1.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 0.0f, 1.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 0.0f, 1.0f, 0.0f)),

		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 1.0f, 0.0f, 0.0f)), // top face
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 1.0f, 0.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 1.0f, 0.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, 1.0f, 0.0f, 0.0f)),

		donut::math::vectorToSnorm8(donut::math::float4(0.0f, -1.0f, 0.0f, 0.0f)), // bottom face
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, -1.0f, 0.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, -1.0f, 0.0f, 0.0f)),
		donut::math::vectorToSnorm8(donut::math::float4(0.0f, -1.0f, 0.0f, 0.0f)),
	};

	m_Buffers = std::make_shared<engine::BufferGroup>();
	m_Buffers->indexBuffer = CreateGeometryBuffer(m_Device, commandList, "IndexBuffer", g_Indices, sizeof(g_Indices), false);

	uint64_t vertexBufferSize = 0;
	m_Buffers->getVertexBufferRange(engine::VertexAttribute::Position).setByteOffset(vertexBufferSize).setByteSize(sizeof(g_Positions)); vertexBufferSize += sizeof(g_Positions);
	m_Buffers->getVertexBufferRange(engine::VertexAttribute::Normal).setByteOffset(vertexBufferSize).setByteSize(sizeof(g_Normals)); vertexBufferSize += sizeof(g_Normals);
	m_Buffers->vertexBuffer = CreateGeometryBuffer(m_Device, commandList, "VertexBuffer", nullptr, vertexBufferSize, true);

	commandList->beginTrackingBufferState(m_Buffers->vertexBuffer, nvrhi::ResourceStates::CopyDest);
	commandList->writeBuffer(m_Buffers->vertexBuffer, g_Positions, sizeof(g_Positions), m_Buffers->getVertexBufferRange(engine::VertexAttribute::Position).byteOffset);
	commandList->writeBuffer(m_Buffers->vertexBuffer, g_Normals, sizeof(g_Normals), m_Buffers->getVertexBufferRange(engine::VertexAttribute::Normal).byteOffset);
	commandList->setPermanentBufferState(m_Buffers->vertexBuffer, nvrhi::ResourceStates::VertexBuffer);
	commandList->close();
	m_Device->executeCommandList(commandList);

	std::shared_ptr<engine::MeshGeometry> geometry = std::make_shared<engine::MeshGeometry>();
	geometry->material = nullptr;
	geometry->numIndices = dim(g_Indices);
	geometry->numVertices = dim(g_Positions);

	m_MeshInfo = std::make_shared<engine::MeshInfo>();
	m_MeshInfo->name = "TerrainMesh";
	m_MeshInfo->buffers = m_Buffers;
	m_MeshInfo->objectSpaceBounds = math::box3(math::float3(-0.5f), math::float3(0.5f));;
	m_MeshInfo->totalIndices = geometry->numIndices;
	m_MeshInfo->totalVertices = geometry->numVertices;
	m_MeshInfo->geometries.push_back(geometry);

	m_MeshInstance = std::make_shared<engine::MeshInstance>(m_MeshInfo);
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
