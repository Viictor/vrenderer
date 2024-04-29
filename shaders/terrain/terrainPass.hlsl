
#pragma pack_matrix(row_major)

#include "terrain_cb.h"
#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/material_bindings.hlsli>
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
Texture2D t_Color : register(t1);
SamplerState s_LinearClampSampler : register(s0);

// morphs input vertex uv from high to low detailed mesh position
//  - gridPos: normalized [0, 1] .xy grid position of the source vertex
//  - vertex:  vertex.xy components in the world space
//  - morphK:  morph value
float2 morphVertex(float2 gridPos, float2 vertex, float morphK, float gridExtents)
{
    float2 fracPart = frac(gridPos * c_TerrainParams.gridSize * 0.5) * 2.0 / c_TerrainParams.gridSize;
    return vertex - fracPart * gridExtents * morphK;
}

float computeMorphK(float distance, float gridExtents)
{
    int lod = clamp(int(log2(gridExtents)), 0, 11); // same as QuadTree::MAX_LODS

    float start = c_TerrainParams.lodRanges[lod].x * 0.85;
    float end = c_TerrainParams.lodRanges[lod].x;
    float delta = end - start;
    float morph = (distance - start) / delta; // [0,1]
    return saturate(morph);
}

float sampleHeight(float2 worldPos)
{
    const float halfSize = c_TerrainParams.worldSize * 0.5;
    float2 uv = (worldPos + halfSize) / c_TerrainParams.worldSize;
    
    return t_Heightmap.SampleLevel(s_LinearClampSampler, uv, 0.1).r * c_TerrainParams.maxHeight;
}

float3 sampleColor(float2 worldPos)
{
    const float halfSize = c_TerrainParams.worldSize * 0.5;
    float2 uv = (worldPos + halfSize) / c_TerrainParams.worldSize;
    
    return t_Color.Sample(s_LinearClampSampler, uv).rgb;
}

float sampleHeight(float2 worldPos, float2 offset)
{
    const float halfSize = c_TerrainParams.worldSize * 0.5;
    float2 uv = (worldPos + halfSize) / c_TerrainParams.worldSize;
    
    return t_Heightmap.Sample(s_LinearClampSampler, uv + offset).r;
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
    
    float distance = length(worldPos.xz - c_Terrain.view.matViewToWorld[3].xz);
    float gridExtents = 2.0 * length(float3(i_instanceMatrix[0].x, i_instanceMatrix[1].x, i_instanceMatrix[2].x));
    float morphK = computeMorphK(distance, gridExtents);
    float2 gridPos = (i_vtx.pos.xz + 1.0) * 0.5;
    worldPos.xz = morphVertex(gridPos, worldPos.xz, morphK, gridExtents);
    worldPos.y = sampleHeight(worldPos.xz);

    o_debug = float3(worldPos.y, worldPos.y, worldPos.y) / c_TerrainParams.maxHeight;

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
    
    float offset = .1;
    float hDx = sampleHeight(i_vtx.pos.xz, float2(offset, 0.0)) - sampleHeight(i_vtx.pos.xz, float2(-offset, 0.0));
    float hDy = sampleHeight(i_vtx.pos.xz, float2(0.0, offset)) - sampleHeight(i_vtx.pos.xz ,float2(0.0, -offset));
    
    float3 normal = normalize(float3(-hDx, 2.0 * offset, -hDy));
    //normal = -normalize(cross(ddx(i_vtx.pos), ddy(i_vtx.pos)));
    
    float height = i_vtx.pos.y / c_TerrainParams.maxHeight;
    
    float3 textureColor = sampleColor(i_vtx.pos.xz);

    o_channel0.xyz = textureColor;//lerp(float3(.1, .6, .1), float3(5.0, 2.0, 3.0), pow(height, 2.0)); // albedo
    o_channel0.w = 1.0; //opacity
    o_channel1.xyz = float3(1.0, 1.0, 1.0)*0.01; // specular f0
    o_channel1.w = 1.0; // occlusion
    o_channel2.xyz = normal;
    o_channel2.w = 1.0; // roughness
    o_channel3.xyz = float3(0.0,0.0,0.0); // emissive
    o_channel3.w = 0;
}