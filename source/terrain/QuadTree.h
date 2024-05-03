#pragma once

#include <donut/core/math/math.h>
#include <donut/engine/TextureCache.h>
#include <array>
#include <memory>
#include <vector>

namespace donut::engine
{
	class IView;
}

using namespace donut;
using namespace donut::math;

namespace tf
{
	class Executor;
}

struct Node
{
	Node(const float3 position, const float3 extents) 
		: m_Position(position)
		, m_Extents(extents) 
	{
		m_Children.fill(nullptr);
	};

	bool Intersects(const float3 position, const float radius) const
	{
		const float3 min = m_Position - m_Extents;
		const float3 max = m_Position + m_Extents;
		float3 distance = float3(0.0,0.0, 0.0);

		if (position.x < min.x) distance.x = (position.x - min.x);
		else if (position.x > max.x) distance.x = (position.x - max.x);
		/*if (position.y < min.y) distance.y = (position.y - min.y);
		else if (position.y > max.y) distance.y = (position.y - max.y);*/
		if (position.z < min.z) distance.z = (position.z - min.z);
		else if (position.z > max.z) distance.z = (position.z - max.z);

		return dot(distance, distance) <= radius;
	};

	float3 m_Position;
	float3 m_Extents;
	std::array<Node*, 4> m_Children;

	static constexpr int TL = 0;
	static constexpr int TR = 1;
	static constexpr int BL = 2;
	static constexpr int BR = 3;
};

struct HeightmapData
{
	void* data;
	uint32_t width;
	uint32_t height;
};

class QuadTree
{
public:
	static constexpr int MAX_LODS = 12;
private:

	bool m_HeightLoaded = false;

	std::unique_ptr<Node> m_RootNode;
	std::vector<const Node*> m_SelectedNodes;
	std::array<float, MAX_LODS> m_LodRanges;
	HeightmapData m_HeightmapData;

	float3 m_Location;
	int m_NumLods;
	float m_Width;
	float m_Height;
	float m_WorldSize;
	float2 m_TexelSize;

	float GetHeightValue(float2 position) const;
	float2 GetMinMaxHeightValue(float2 position, float width, float height) const;

	void Split(Node* node, int numSplits = 0);

	void SetHeight(Node* node, int numSplits = 0);

	void InitLodRanges();

public:
	QuadTree(const float width, const float height,  float worldSize, const float3 location = float3(0.0f, 0.0f, 0.0f));

	~QuadTree()
	{
		if (m_HeightmapData.data)
			free(m_HeightmapData.data);
	}

	void Init(const std::shared_ptr<engine::LoadedTexture>& loadedTexture, tf::Executor& executor);

	static void Print(const Node* node, int level);

	void PrintSelected() const;

	bool NodeSelect(const float3 position, const Node* node, int lodLevel, const dm::frustum& frustum, const float maxHeight);

	const std::vector<const Node*> GetSelectedNodes() const { return m_SelectedNodes; }

	const std::unique_ptr<Node>& GetRootNode() const { return m_RootNode; }

	void ClearSelectedNodes() { m_SelectedNodes.clear(); }

	int GetNumLods() const { return m_NumLods; }

	const std::array<float, MAX_LODS>& GetLodRanges() const { return m_LodRanges; }

	void DebugDraw(const Node* node) const;

	struct DebugDrawData
	{
		const engine::IView* view;
		std::vector<const Node*> culledNodes;
	} m_DebugDrawData;
};