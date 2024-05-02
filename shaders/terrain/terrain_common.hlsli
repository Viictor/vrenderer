
#include <donut/shaders/vulkan.hlsli>

struct SceneVertex
{
    float3 pos : POS;
};

cbuffer c_Terrain : register(b1 VK_DESCRIPTOR_SET(1))
{
    TerrainViewConstants c_Terrain;
};

cbuffer c_TerrainParams : register(b2 VK_DESCRIPTOR_SET(2))
{
    TerrainParamsConstants c_TerrainParams;
};

Texture2D t_Heightmap : register(t0);
SamplerState s_LinearClampSampler : register(s0);