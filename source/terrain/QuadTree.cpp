#include "QuadTree.h"

#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#ifdef DONUT_WITH_TASKFLOW
#include <taskflow/taskflow.hpp>
#endif

#ifdef DONUT_WITH_TASKFLOW
void QuadTree::Init(std::shared_ptr<engine::LoadedTexture> loadedTexture, tf::Executor& executor)
{
	engine::TextureData* textureData = static_cast<engine::TextureData*>(loadedTexture.get());

	m_NumLods = min(MAX_LODS, int(log2(m_Width) + 1));
	size_t dataSize = textureData->dataLayout[0][0].dataSize;
	if (dataSize > 0)
	{
		m_HeightmapData.width = textureData->width;
		m_HeightmapData.height = textureData->height;
		m_HeightmapData.data = malloc(dataSize);
		m_TexelSize = float2(m_HeightmapData.width / m_Width, m_HeightmapData.height / m_Height);
		memcpy(m_HeightmapData.data, (void*)textureData->data->data(), textureData->data->size());
	}
	else
	{
		m_HeightmapData.width = 0;
		m_HeightmapData.height = 0;
		m_TexelSize = float2(m_HeightmapData.width / m_Width, m_HeightmapData.height / m_Height);

		log::error("Heightmap texture data missing for QuadTree generation");

	}

	executor.silent_async([this]()
		{
			float2 minMax = GetMinMaxHeightValue(float2(0.0f, 0.0f), m_Width, m_Height);
			m_RootNode = std::make_unique<Node>(float3(0.0f, minMax.x, 0.0f), float3(m_Width / 2.0f, minMax.y, m_Height / 2.0f));

			int numSplits = 1;
			Split(m_RootNode.get(), numSplits);

			m_Initialized = true;
		});
}
#endif

void QuadTree::Init(std::shared_ptr<engine::LoadedTexture> loadedTexture)
{
	engine::TextureData* textureData = static_cast<engine::TextureData*>(loadedTexture.get());

	m_NumLods = min(MAX_LODS, int(log2(m_Width) + 1));
	size_t dataSize = textureData->dataLayout[0][0].dataSize;
	float2 minMax = float2(0.0f, 0.0f);
	if (dataSize > 0)
	{
		m_HeightmapData.width = textureData->width;
		m_HeightmapData.height = textureData->height;
		m_HeightmapData.data = malloc(dataSize);

		m_TexelSize = float2(m_HeightmapData.width / m_Width, m_HeightmapData.height / m_Height);
		memcpy(m_HeightmapData.data, (void*)textureData->data->data(), textureData->data->size());

		minMax = GetMinMaxHeightValue(float2(0.0f, 0.0f), m_Width, m_Height);
	}
	else
	{
		m_HeightmapData.width = 0;
		m_HeightmapData.height = 0;
		m_TexelSize = float2(m_HeightmapData.width / m_Width, m_HeightmapData.height / m_Height);

		log::error("Heightmap texture data missing for QuadTree generation");
	}

	m_RootNode = std::make_unique<Node>(float3(0.0f, minMax.x, 0.0f), float3(m_Width / 2.0f, minMax.y, m_Height / 2.0f));

	int numSplits = 1;
	Split(m_RootNode.get(), numSplits);

	m_Initialized = true;
}

void QuadTree::Print(const Node* _Node, int _level)
{
	if (_Node == nullptr)
		return;
	
	log::info("Level: %d", _level);
	log::info("Node Pos: %f, %f. Extents: %f, %f.", _Node->m_Position.x, _Node->m_Position.y, _Node->m_Extents.x, _Node->m_Extents.y);
	
	_level++;
	for (int i = 0; i < 4; i++)
	{
		Print(_Node->m_Children[i], _level);
	}
}

void QuadTree::PrintSelected() const
{
	auto nodes = GetSelectedNodes();
	log::info("Selected Nodes");
	for (int i = 0; i < nodes.size(); i++)
	{
		log::info("Node Pos: %f, %f. Extents: %f, %f.", nodes[i]->m_Position.x, nodes[i]->m_Position.y, nodes[i]->m_Extents.x, nodes[i]->m_Extents.y);
	}
}

bool QuadTree::NodeSelect(const float3 _Position, const Node* _Node, int _LodLevel, const dm::frustum& _Frustum, const float _MaxHeight)
{
	if (!m_Initialized)
		return false;

	if (!_Node->Intersects(_Position, m_LodRanges[_LodLevel] * m_LodRanges[_LodLevel])) // discard nodes out of range
		return false;

	float3 min = _Node->m_Position - _Node->m_Extents;
	float3 max = _Node->m_Position + _Node->m_Extents;
	min.y *= _MaxHeight;
	max.y *= _MaxHeight;
	
	if (!_Frustum.intersectsWith(box3(min, max)))
		return true; // Node out of frustum - return true to prevent parent from being selected

	if (_LodLevel == 0) // Add leaf nodes
	{
		// Add Node
		m_SelectedNodes.push_back(_Node);
		return true;
	}
	else
	{
		if (!_Node->Intersects(_Position, m_LodRanges[_LodLevel - 1] * m_LodRanges[_LodLevel - 1])) // Add it if only this level is intersecting and not a deeper one
		{
			// Add Node
			m_SelectedNodes.push_back(_Node);
		}
		else
		{
			for (int i = 0; i < 4; i++) // Recursive call to check if children intersect 
			{
				if (!NodeSelect(_Position, _Node->m_Children[i], _LodLevel - 1, _Frustum, _MaxHeight))
				{
					// Add Node
					m_SelectedNodes.push_back(_Node->m_Children[i]);
				}
			}
		}
	}
	return true;
}

float QuadTree::GetHeightValue(float2 _Position)
{
	const uint8_t* byteData = reinterpret_cast<const uint8_t*>(m_HeightmapData.data);

	int index = static_cast<int>(_Position.x + _Position.y * m_HeightmapData.width);

	float heightValue = static_cast<float>(byteData[index]) / 255.0f;

	return heightValue;
}

float2 QuadTree::GetMinMaxHeightValue(float2 _Position, float width, float height)
{
	float2 minV = _Position - float2(width / 2, height / 2);
	minV += float2(m_Width / 2, m_Height / 2);
	minV *= m_TexelSize;

	float2 maxV = minV + float2(width, height) * m_TexelSize;

	float2 minMax = float2(infinity, -infinity);
	for (int i = minV.x; i < maxV.x; i++)
	{
		for (int j = minV.y; j < maxV.y; j++)
		{
			minMax.x = min(minMax.x, GetHeightValue(float2(i,j)));
			minMax.y = max(minMax.y, GetHeightValue(float2(i,j)));
		}
	}
	return minMax;
}

void QuadTree::Split(Node* _Node, int _NumSplits)
{
	float3 extents = _Node->m_Extents / 2.0f;
	float3 position0 = _Node->m_Position + float3(-extents.x, 0.0, extents.z);
	float3 position1 = _Node->m_Position + extents;
	float3 position2 = _Node->m_Position - extents;
	float3 position3 = _Node->m_Position - float3(-extents.x, 0.0, extents.z);

	float2 minMax0 = GetMinMaxHeightValue(float2(position0.x, position0.z), _Node->m_Extents.x, _Node->m_Extents.z);
	float2 minMax1 = GetMinMaxHeightValue(float2(position1.x, position1.z), _Node->m_Extents.x, _Node->m_Extents.z);
	float2 minMax2 = GetMinMaxHeightValue(float2(position2.x, position2.z), _Node->m_Extents.x, _Node->m_Extents.z);
	float2 minMax3 = GetMinMaxHeightValue(float2(position3.x, position3.z), _Node->m_Extents.x, _Node->m_Extents.z);

	position0.y = minMax0.x;
	position1.y = minMax1.x;
	position2.y = minMax2.x;
	position3.y = minMax3.x;

	extents.y = minMax0.y;
	_Node->m_Children[Node::TL] = new Node(position0, extents);
	extents.y = minMax1.y;
	_Node->m_Children[Node::TR] = new Node(position1, extents);
	extents.y = minMax2.y;
	_Node->m_Children[Node::BL] = new Node(position2, extents);
	extents.y = minMax3.y;
	_Node->m_Children[Node::BR] = new Node(position3, extents);

	_NumSplits++;

	if (_NumSplits < m_NumLods)
	{
		for (int i = 0; i < 4; i++)
		{
			Split(_Node->m_Children[i], _NumSplits);
		}
	}
}

void QuadTree::InitLodRanges()
{
	float minLodDistance = 4.0f;
	for (int i = 0; i < MAX_LODS; i++)
	{
		m_LodRanges[i] = minLodDistance * pow(2.0f, i);
	}
}