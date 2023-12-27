#include "QuadTree.h"

#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <taskflow/taskflow.hpp>

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
		memcpy(m_HeightmapData.data, (void*)textureData->data->data(), dataSize);
	}
	else
	{
		m_HeightmapData.width = 0;
		m_HeightmapData.height = 0;
		m_TexelSize = float2(m_HeightmapData.width / m_Width, m_HeightmapData.height / m_Height);

		log::error("Heightmap texture data missing for QuadTree generation");

	}

	m_RootNode = std::make_unique<Node>(float3(0.0f, 0.0, 0.0f), float3(m_Width / 2.0f, 0.0, m_Height / 2.0f));

	int numSplits = 1;
	Split(m_RootNode.get(), numSplits);

	executor.silent_async([this]()
		{
			int numSplits = 0;
			SetHeight(m_RootNode.get(), numSplits);
			m_HeightLoaded = true;
			log::info("QuadTree nodes height set");
		});
}

void QuadTree::Print(const Node* node, int level)
{
	if (node == nullptr)
		return;
	
	log::info("Level: %d", level);
	log::info("Node Pos: %f, %f %f. Extents: %f, %f %f.", node->m_Position.x, node->m_Position.y, node->m_Position.z, node->m_Extents.x, node->m_Extents.y, node->m_Extents.z);
	
	level++;
	for (int i = 0; i < 4; i++)
	{
		Print(node->m_Children[i], level);
	}
}

void QuadTree::PrintSelected() const
{
	auto nodes = GetSelectedNodes();
	log::info("Selected Nodes");
	for (int i = 0; i < nodes.size(); i++)
	{
		log::info("Node Pos: %f, %f %f. Extents: %f, %f %f.", nodes[i]->m_Position.x, nodes[i]->m_Position.y, nodes[i]->m_Position.z, nodes[i]->m_Extents.x, nodes[i]->m_Extents.y, nodes[i]->m_Extents.z);
	}
}

bool QuadTree::NodeSelect(const float3 position, const Node* node, int lodLevel, const dm::frustum& frustum, const float maxHeight)
{
	if (!node->Intersects(position, m_LodRanges[lodLevel] * m_LodRanges[lodLevel])) // discard nodes out of range
		return false;

	float3 min = node->m_Position - node->m_Extents;
	float3 max = node->m_Position + node->m_Extents;
	if (m_HeightLoaded)
	{
		min.y *= maxHeight;
		max.y *= maxHeight;
	}
	else
	{
		min.y = 0.0f;
		max.y = position.y;
	}
	
	if (!frustum.intersectsWith(box3(min, max)))
		return true; // Node out of frustum - return true to prevent parent from being selected

	if (lodLevel == 0) // Add leaf nodes
	{
		// Add Node
		m_SelectedNodes.push_back(node);
		return true;
	}
	else
	{
		if (!node->Intersects(position, m_LodRanges[lodLevel - 1] * m_LodRanges[lodLevel - 1])) // Add it if only this level is intersecting and not a deeper one
		{
			// Add Node
			m_SelectedNodes.push_back(node);
		}
		else
		{
			for (int i = 0; i < 4; i++) // Recursive call to check if children intersect 
			{
				if (!NodeSelect(position, node->m_Children[i], lodLevel - 1, frustum, maxHeight))
				{
					// Add Node
					m_SelectedNodes.push_back(node->m_Children[i]);
				}
			}
		}
	}
	return true;
}

float QuadTree::GetHeightValue(float2 position)
{
	const uint8_t* byteData = reinterpret_cast<const uint8_t*>(m_HeightmapData.data);

	int index = static_cast<int>(position.x + position.y * m_HeightmapData.width);

	float heightValue = static_cast<float>(byteData[index]) / 255.0f;

	return heightValue;
}

float2 QuadTree::GetMinMaxHeightValue(float2 position, float width, float height)
{
	float2 minV = position - float2(width / 2, height / 2);
	minV += float2(m_Width / 2, m_Height / 2);
	minV *= m_TexelSize;

	float2 maxV = minV + float2(width, height) * m_TexelSize;

	int2 limitX = int2(int(minV.x), int(maxV.x));
	int2 limitY = int2(int(minV.y), int(maxV.y));

	float2 minMax = float2(infinity, -infinity);
	for (int i = limitX.x; i < limitX.y; i++)
	{
		for (int j = limitY.x; j < limitY.y; j++)
		{
			minMax.x = min(minMax.x, GetHeightValue(float2(float(i),float(j))));
			minMax.y = max(minMax.y, GetHeightValue(float2(float(i),float(j))));
		}
	}
	return minMax;
}

void QuadTree::SetHeight(Node* node, int numSplits)
{
	float2 minMax = GetMinMaxHeightValue(float2(node->m_Position.x, node->m_Position.z), node->m_Extents.x * 2.0f, node->m_Extents.z * 2.0f);

	float extent = (minMax.y - minMax.x) / 2.0f;

	node->m_Position.y = minMax.x + extent;
	node->m_Extents.y = extent;

	numSplits++;
	if (numSplits < m_NumLods)
	{
		for (int i = 0; i < 4; i++)
		{
			SetHeight(node->m_Children[i], numSplits);
		}
	}
}

void QuadTree::Split(Node* node, int numSplits)
{
	float3 extents = node->m_Extents / 2.0f;
	float3 position0 = node->m_Position + float3(-extents.x, 0.0, extents.z);
	float3 position1 = node->m_Position + extents;
	float3 position2 = node->m_Position - extents;
	float3 position3 = node->m_Position - float3(-extents.x, 0.0, extents.z);

	node->m_Children[Node::TL] = new Node(position0, extents);
	node->m_Children[Node::TR] = new Node(position1, extents);
	node->m_Children[Node::BL] = new Node(position2, extents);
	node->m_Children[Node::BR] = new Node(position3, extents);

	numSplits++;

	if (numSplits < m_NumLods)
	{
		for (int i = 0; i < 4; i++)
		{
			Split(node->m_Children[i], numSplits);
		}
	}
}

void QuadTree::InitLodRanges()
{
	float minLodDistance = 4.0f;
	for (int i = 0; i < MAX_LODS; i++)
	{
		m_LodRanges[i] = minLodDistance * pow(2.0f, float(i));
	}
}