#include "QuadTree.h"

#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <taskflow/taskflow.hpp>

#include "../editor/ImGuizmo.h"
#include "donut/engine/View.h"

QuadTree::QuadTree(const float width, const float height, const float worldSize, const float3 location)
	: m_Location(location)
	, m_Width(width)
	, m_Height(height)
	, m_WorldSize(worldSize)
{
	InitLodRanges();
}

void QuadTree::Init(const std::shared_ptr<engine::LoadedTexture>& loadedTexture, tf::Executor& executor)
{
	const engine::TextureData* textureData = static_cast<engine::TextureData*>(loadedTexture.get());
	m_NumLods = min(MAX_LODS-1, static_cast<int>(log2(m_Width)));
	const size_t dataSize = textureData->dataLayout[0][0].dataSize;
	if (dataSize > 0)
	{
		m_HeightmapData.width = textureData->width;
		m_HeightmapData.height = textureData->height;
		m_HeightmapData.data = malloc(dataSize);
		m_TexelSize = float2(static_cast<float>(m_HeightmapData.width) / m_WorldSize, static_cast<float>(m_HeightmapData.height) / m_WorldSize);

		memcpy(m_HeightmapData.data, textureData->data->data(), dataSize);
	}
	else
	{
		m_HeightmapData.width = 0;
		m_HeightmapData.height = 0;
		m_TexelSize = float2(0.0f, 0.0f);

		log::error("Heightmap texture data missing for QuadTree generation");
	}

	m_RootNode = std::make_unique<Node>(m_Location, float3(m_Width / 2.0f, 0.0, m_Height / 2.0f));

	Split(m_RootNode.get(), 1);

	executor.silent_async([this]()
		{
			SetHeight(m_RootNode.get(), 0);
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
	const std::vector<const Node*> nodes = GetSelectedNodes();
	log::info("Selected Nodes");
	for (const Node* node : nodes)
	{
		log::info("Node Pos: %f, %f %f. Extents: %f, %f %f.", node->m_Position.x, node->m_Position.y, node->m_Position.z,
		          node->m_Extents.x, node->m_Extents.y, node->m_Extents.z);
	}
}

bool QuadTree::NodeSelect(const float3 position, const Node* node, const int lodLevel, const dm::frustum& frustum, const float maxHeight)
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
	box3 cube = box3(min, max);
	
	if (!frustum.intersectsWith(cube))
	{
		m_DebugDrawData.culledNodes.push_back(node);
		DebugDraw(node);
		return true; // Node out of frustum - return true to prevent parent from being selected
	}

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

void QuadTree::DebugDraw(const Node* node) const
{
	//float height = m_DebugDrawData.view->GetViewOrigin().y * 0.5f;
	//float3 size = float3(node->m_Extents.x * 2.0f, height, node->m_Extents.z * 2.0f);
	//float3 position = float3(node->m_Position.x, height, node->m_Position.z);

	float3 size = node->m_Extents * 2.0f;
	float3 position = node->m_Position;

	const float4x4 transform = affineToHomogeneous(scaling(size) * math::translation(position));
	const float4x4 view = affineToHomogeneous(m_DebugDrawData.view->GetViewMatrix());
	const float4x4 proj = m_DebugDrawData.view->GetProjectionMatrix(true);

	box3 cube = box3(float3(-0.5f), float3(0.5f)) * homogeneousToAffine(transform);

	ImU32 color = m_DebugDrawData.view->GetViewFrustum().intersectsWith(cube) ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255);

	ImGuizmo::DrawCubes(view.m_data, proj.m_data, transform.m_data, 1, color);
}

float QuadTree::GetHeightValue(const float2 position) const
{
	const uint8_t* byteData = static_cast<const uint8_t*>(m_HeightmapData.data);

	const int index = static_cast<int>(position.x + position.y * static_cast<float>(m_HeightmapData.width));

	const float heightValue = static_cast<float>(byteData[index]) / 255.0f;

	return heightValue;
}

float2 QuadTree::GetMinMaxHeightValue(const float2 position, const float width, const float height) const
{
	float2 minV = position - float2(width / 2, height / 2);
	minV += float2(m_WorldSize / 2, m_WorldSize / 2);
	minV *= m_TexelSize;

	const float2 maxV = minV + float2(width, height) * m_TexelSize;

	const int2 limitX = int2(floor(minV.x), ceil(maxV.x));
	const int2 limitY = int2(floor(minV.y), ceil(maxV.y));

	float2 minMax = float2(infinity, -infinity);
	for (int i = limitX.x; i < limitX.y; i++)
	{
		for (int j = limitY.x; j < limitY.y; j++)
		{
			const float sampledHeight = GetHeightValue(float2(static_cast<float>(i), static_cast<float>(j)));
			minMax.x = min(minMax.x, sampledHeight);
			minMax.y = max(minMax.y, sampledHeight);
		}
	}

	minMax.x = (minMax.y - minMax.x) == 0.f ? 0.f : minMax.x;

	return minMax;
}

void QuadTree::SetHeight(Node* node, int numSplits)
{
	const float2 minMax = GetMinMaxHeightValue(float2(node->m_Position.x, node->m_Position.z), node->m_Extents.x * 2.0f, node->m_Extents.z * 2.0f);

	const float extent = (minMax.y - minMax.x) / 2.0f;

	node->m_Position.y = minMax.x + extent;
	node->m_Extents.y = extent;

	numSplits++;
	if (numSplits <= m_NumLods)
	{
		for (int i = 0; i < 4; i++)
		{
			SetHeight(node->m_Children[i], numSplits);
		}
	}
}

void QuadTree::Split(Node* node, int numSplits)
{
	const float3 extents = node->m_Extents / 2.0f;
	const float3 position0 = node->m_Position + float3(-extents.x, 0.0f, extents.z);
	const float3 position1 = node->m_Position + extents;
	const float3 position2 = node->m_Position - extents;
	const float3 position3 = node->m_Position - float3(-extents.x, 0.0f, extents.z);

	node->m_Children[Node::TL] = new Node(position0, extents);
	node->m_Children[Node::TR] = new Node(position1, extents);
	node->m_Children[Node::BL] = new Node(position2, extents);
	node->m_Children[Node::BR] = new Node(position3, extents);

	numSplits++;

	if (numSplits <= m_NumLods)
	{
		for (int i = 0; i < 4; i++)
		{
			Split(node->m_Children[i], numSplits);
		}
	}
}

void QuadTree::InitLodRanges()
{
	const float minLodDistance = 4.0f;
	for (int i = 0; i < MAX_LODS; i++)
	{
		m_LodRanges[i] = minLodDistance * pow(2.0f, static_cast<float>(i));
	}
}

