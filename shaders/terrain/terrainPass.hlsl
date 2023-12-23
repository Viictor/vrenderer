
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

Texture2D t_Heightmap : register(t0);
SamplerState s_HeightmapSampler : register(s0);

void main_vs(
    in SceneVertex i_vtx,
    in float3x4 i_instanceMatrix : TRANSFORM,
    in uint i_instance : SV_InstanceID,
    out float4 o_position : SV_Position,
    out SceneVertex o_vtx,
    out float3 o_location : LOCATION
)
{
    o_vtx = i_vtx;
    
    float4 uv = float4(mul(i_instanceMatrix, float4(i_vtx.pos.x, 0, i_vtx.pos.z, 1.0)), 1.0);
    const float halfSize = c_Terrain.size * 0.5;
    uv = (uv + halfSize) / c_Terrain.size;
    
    float height = t_Heightmap.SampleLevel(s_HeightmapSampler, uv.xz, 0).r * c_Terrain.maxHeight;
    float4 worldPos = float4(mul(i_instanceMatrix, float4(i_vtx.pos.x, height, i_vtx.pos.z, 1.0)), 1.0);
    
    o_vtx.pos = worldPos.xyz;
    
    float4 viewPos = mul(worldPos, c_Terrain.view.matWorldToView);
    o_position = mul(viewPos, c_Terrain.view.matViewToClip);

    o_location = float3(height, height, height) / c_Terrain.maxHeight;
}

void main_ps(
    in float4 i_position : SV_Position,
    in SceneVertex i_vtx,
    in float3 i_location : LOCATION,
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
    o_channel0.xyz = float3(i_location);
    o_channel0.w = 1.0;
    o_channel1.xyz = float3(0.0, 0.0, 0.0);
    o_channel1.w = 1.0;
    o_channel2.xyz = normal;
    o_channel2.w = 0.2;
    o_channel3.xyz = float3(0.0,0.0,0.0);
    o_channel3.w = 0;
}