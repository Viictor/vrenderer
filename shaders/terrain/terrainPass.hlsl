
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

void main_vs(
    in SceneVertex i_vtx,
    out float4 o_position : SV_Position,
    out SceneVertex o_vtx
)
{
    o_vtx = i_vtx;
    
    float4 worldPos = float4(o_vtx.pos, 1.0);
    float4 viewPos = mul(worldPos, c_Terrain.view.matWorldToView);
    o_position = mul(viewPos, c_Terrain.view.matViewToClip);
}

void main_ps(
    in float4 i_position : SV_Position,
    in SceneVertex i_vtx,
    out float4 o_channel0 : SV_Target0,
    out float4 o_channel1 : SV_Target1,
    out float4 o_channel2 : SV_Target2,
    out float4 o_channel3 : SV_Target3
)
{
    //MaterialTextureSample textures = SampleMaterialTexturesAuto(i_vtx.texCoord);

    //MaterialSample surface = EvaluateSceneMaterial(i_vtx.normal, i_vtx.tangent, g_Material, textures);
    
    o_channel0.xyz = float3(1.0,1.0,1.0);
    o_channel0.w = 1.0;
    o_channel1.xyz = float3(0.0, 0.0, 0.0);
    o_channel1.w = 1.0;
    o_channel2.xyz = i_vtx.normal;
    o_channel2.w = 0.2;
    o_channel3.xyz = float3(0.0,0.0,0.0);
    o_channel3.w = 0;
}