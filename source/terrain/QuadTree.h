#pragma once

#include <donut/core/math/math.h>
#include <array>
#include <memory>
#include <vector>

using namespace donut;
using namespace donut::math;

struct Node
{
	Node(const float2 _Position, const float2 _Extents) 
		: m_Position(_Position), m_Extents(_Extents) 
	{
		m_Children.fill(nullptr);
	};

	bool Intersects(float2 _Position, float _Radius) const
	{
		float nodeRadius = sqrtf(dot(m_Extents, m_Extents));
		return length(m_Position - _Position) - nodeRadius <= _Radius;
	};

	float2 m_Position;
	float2 m_Extents;
	std::array<Node*, 4> m_Children;

	static constexpr int TL = 0;
	static constexpr int TR = 1;
	static constexpr int BL = 2;
	static constexpr int BR = 3;
};

class QuadTree
{
public:
	static constexpr int NUM_LODS = 4;
private:

	std::unique_ptr<Node> m_RootNode;
	std::vector<const Node*> m_SelectedNodes;
	std::array<float, NUM_LODS> m_LodRanges;

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

	void Init();

	void Print(const Node* _Node, int _level);

	void PrintSelected();

	bool NodeSelect(const float2 _Position, const Node* _Node, int _LodLevel, const dm::frustum& _Frustum);

	const std::vector<const Node*> GetSelectedNodes() const { return m_SelectedNodes; };

	const std::unique_ptr<Node>& GetRootNode() const { return m_RootNode; };

	void ClearSelectedNodes() { m_SelectedNodes.clear(); };
};