#pragma once

#include <donut/core/math/math.h>
#include <donut/engine/TextureCache.h>
#include <array>
#include <memory>
#include <vector>

using namespace donut;
using namespace donut::math;

struct Node
{
	Node(const float2 _Position, const float2 _Extents, const float _Height = 0) 
		: m_Position(_Position)
		, m_Extents(_Extents) 
		, m_Height(_Height) 
	{
		m_Children.fill(nullptr);
	};

	bool Intersects(float2 _Position, float _Radius) const
	{

		float minDist = length(m_Position - _Position);
		{
			float2 vertPos0 = m_Position - m_Extents;
			float2 vertPos1 = m_Position + m_Extents;
			float2 vertPos2 = float2(m_Position.x + m_Extents.x, m_Position.y - m_Extents.y);
			float2 vertPos3 = float2(m_Position.x - m_Extents.x, m_Position.y + m_Extents.y);

			minDist = fminf(length(vertPos0 - _Position), minDist);
			minDist = fminf(length(vertPos1 - _Position), minDist);
			minDist = fminf(length(vertPos2 - _Position), minDist);
			minDist = fminf(length(vertPos3 - _Position), minDist);
		}

		return minDist <= _Radius;
	};

	float2 m_Position;
	float2 m_Extents;
	float m_Height;
	std::array<Node*, 4> m_Children;

	static constexpr int TL = 0;
	static constexpr int TR = 1;
	static constexpr int BL = 2;
	static constexpr int BR = 3;
};

class QuadTree
{
public:
	static constexpr int MAX_LODS = 12;
private:

	std::unique_ptr<Node> m_RootNode;
	std::vector<const Node*> m_SelectedNodes;
	std::array<float, MAX_LODS> m_LodRanges;
	engine::TextureData* m_TextureData;

	int m_NumLods;
	float m_Width;
	float m_Height;

	void Split(Node* _Node, int _NumSplits = 0);

	void InitLodRanges();

public:
	QuadTree(const float _Width, const float _Height) 
		: m_Width(_Width), m_Height(_Height) 
	{
		InitLodRanges();
	};

	void Init(engine::TextureData* textureData);

	void Print(const Node* _Node, int _level);

	void PrintSelected() const;

	bool NodeSelect(const float2 _Position, const Node* _Node, int _LodLevel, const dm::frustum& _Frustum);

	const std::vector<const Node*> GetSelectedNodes() const { return m_SelectedNodes; };

	const std::unique_ptr<Node>& GetRootNode() const { return m_RootNode; };

	void ClearSelectedNodes() { m_SelectedNodes.clear(); };

	const int GetNumLods() const { return m_NumLods; };

	const std::array<float, MAX_LODS>& GetLodRanges() const { return m_LodRanges; };
};