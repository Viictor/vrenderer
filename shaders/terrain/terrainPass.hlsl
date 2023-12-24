
#pragma pack_matrix(row_major)

#include "terrain_cb.h"
#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/material_bindings.hlsli>
#include <donut/shaders/vulkan.hlsli>

struct SceneVertex
{
    float3 pos : POS;
    centroid float3 normal : NORMAL;
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
SamplerState s_HeightmapSampler : register(s0);

// morphs input vertex uv from high to low detailed mesh position
//  - gridPos: normalized [0, 1] .xy grid position of the source vertex
//  - vertex:  vertex.xy components in the world space
//  - morphK:  morph value
float2 morphVertex(float2 gridPos, float2 vertex, float morphK, float gridExtents)
{
    const float GRID_SIZE = 16.0;
    float2 fracPart = frac(gridPos * GRID_SIZE * 0.5) * 2.0 / GRID_SIZE;
    return vertex - fracPart * gridExtents * morphK;
}

float computeMorphK(float distance, float gridExtents)
{
    int lod = int(log2(gridExtents));

    float low = 0.0;
    if (lod > 0)
        low = c_TerrainParams.lodRanges[lod - 1].x;

    float high = c_TerrainParams.lodRanges[lod].x;
    float delta = high - low;
    float morph = (distance - low) / delta; // [0,1]
    
    return clamp(morph / 0.5 - 1.0, 0.001, 1.0); // [-1,1] means we are going to start morphing after the half way point
}

float sampleHeight(float2 worldPos)
{
    const float halfSize = c_TerrainParams.size * 0.5;
    float2 uv = (worldPos + halfSize) / c_TerrainParams.size;
    
    return t_Heightmap.SampleLevel(s_HeightmapSampler, uv, 0).r;
}

void main_vs(
    in SceneVertex i_vtx,
    in float3x4 i_instanceMatrix : TRANSFORM,
    in uint i_instance : SV_InstanceID,
    out float4 o_position : SV_Position,
    out SceneVertex o_vtx,
    out float3 o_debug : LOCATION
)
{
    float4 worldPos = float4(mul(i_instanceMatrix, float4(i_vtx.pos, 1.0)), 1.0);
    float height = sampleHeight(worldPos.xz) * c_TerrainParams.maxHeight;
    worldPos.y = height;
    //worldPos = float4(mul(i_instanceMatrix, float4(i_vtx.pos.x, height, i_vtx.pos.z, 1.0)), 1.0);
    
    float distance = length(worldPos.xz - c_Terrain.view.matViewToWorld[3].xz);
    float gridExtents = 2.0 * length(float3(i_instanceMatrix[0].x, i_instanceMatrix[1].x, i_instanceMatrix[2].x));
    float morphK = computeMorphK(distance, gridExtents);
    float2 gridPos = (i_vtx.pos.xz + 1.0) * 0.5;
    worldPos.xz = morphVertex(gridPos, worldPos.xz, morphK, gridExtents);
    worldPos.y = sampleHeight(worldPos.xz) * c_TerrainParams.maxHeight;

    o_debug = float3(worldPos.y, worldPos.y, worldPos.y) / c_TerrainParams.maxHeight;
    //o_debug = float3(gridPos.x, morphK, gridPos.y);

    o_vtx = i_vtx;
    o_vtx.pos = worldPos.xyz;
    
    float4 viewPos = mul(worldPos, c_Terrain.view.matWorldToView);
    o_position = mul(viewPos, c_Terrain.view.matViewToClip);
}

void main_ps(
    in float4 i_position : SV_Position,
    in SceneVertex i_vtx,
    in float3 i_debug : LOCATION,
    out float4 o_channel0 : SV_Target0,
    out float4 o_channel1 : SV_Target1,
    out float4 o_channel2 : SV_Target2,
    out float4 o_channel3 : SV_Target3
)
{
    //MaterialTextureSample textures = SampleMaterialTexturesAuto(i_vtx.texCoord);

    //MaterialSample surface = EvaluateSceneMaterial(i_vtx.normal, i_vtx.tangent, g_Material, textures);
    
    float3 dx = ddx(i_vtx.pos);
    float3 dy = ddy(i_vtx.pos);
    float3 normal = -normalize(cross(dx, dy));
    
    //o_channel0.xyz = float3(i_vtx.pos.yyy / 20.f);
    o_channel0.xyz = float3(i_debug.y, i_debug.y, i_debug.y);
    o_channel0.w = 1.0;
    o_channel1.xyz = float3(0.0, 0.0, 0.0);
    o_channel1.w = 1.0;
    o_channel2.xyz = normal;
    o_channel2.w = 0.2;
    o_channel3.xyz = float3(0.0,0.0,0.0);
    o_channel3.w = 0;
}