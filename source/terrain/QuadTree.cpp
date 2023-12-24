#include "QuadTree.h"

#include <donut/core/log.h>

void QuadTree::Init(engine::TextureData* textureData)
{
	m_TextureData = textureData;

	m_NumLods = min(MAX_LODS, int(log2(m_Width) + 1));

	m_RootNode = std::make_unique<Node>(float2(0.0f, 0.0f), float2(m_Width / 2.0f, m_Height / 2.0f));

	int numSplits = 1;
	Split(m_RootNode.get(), numSplits);

	//Print(m_RootNode.get(), 0);
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

bool QuadTree::NodeSelect(const float2 _Position, const Node* _Node, int _LodLevel, const dm::frustum& _Frustum)
{
	if (!_Node->Intersects(_Position, m_LodRanges[_LodLevel])) // discard nodes out of range
		return false;

	float3 min = float3(_Node->m_Position.x - _Node->m_Extents.x, 0.0f, _Node->m_Position.y - _Node->m_Extents.y);
	float3 max = float3(_Node->m_Position.x + _Node->m_Extents.x, 0.0f, _Node->m_Position.y + _Node->m_Extents.y);
	
	//if (!_Frustum.intersectsWith(box3(min, max)))
	//	return true; // Node out of frustum - return true to prevent parent from being selected

	if (_LodLevel == 0) // Add leaf nodes
	{
		// Add Node
		m_SelectedNodes.push_back(_Node);
		return true;
	}
	else
	{
		if (!_Node->Intersects(_Position, m_LodRanges[_LodLevel - 1])) // Add it if only this level is intersecting and not a deeper one
		{
			// Add Node
			m_SelectedNodes.push_back(_Node);
		}
		else
		{
			for (int i = 0; i < 4; i++) // Recursive call to check if children intersect 
			{
				if (!NodeSelect(_Position, _Node->m_Children[i], _LodLevel - 1, _Frustum))
				{
					// Add Node
					m_SelectedNodes.push_back(_Node->m_Children[i]);
				}
			}
		}
	}
	return true;
}

void QuadTree::Split(Node* _Node, int _NumSplits)
{
	_Node->m_Children[Node::TL] = new Node(_Node->m_Position + float2(-_Node->m_Extents.x / 2.0f, _Node->m_Extents.y / 2.0f), _Node->m_Extents / 2.0f);
	_Node->m_Children[Node::TR] = new Node(_Node->m_Position + _Node->m_Extents / 2.0f, _Node->m_Extents / 2.0f);
	_Node->m_Children[Node::BL] = new Node(_Node->m_Position - float2(_Node->m_Extents.x / 2.0f, _Node->m_Extents.y / 2.0f), _Node->m_Extents / 2.0f);
	_Node->m_Children[Node::BR] = new Node(_Node->m_Position - float2(-_Node->m_Extents.x / 2.0f, _Node->m_Extents.y / 2.0f), _Node->m_Extents / 2.0f);

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
		m_LodRanges[i] = minLodDistance * pow(2, i);
	}
}