#pragma pack_matrix(row_major)

#include "terrain_cb.h"
#include "terrain_common.hlsli"
#include <donut/shaders/scene_material.hlsli>
#include <donut/shaders/material_bindings.hlsli>

Texture2D t_Color : register(t1);

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

float3 hsv_to_rgb(float3 HSV)
{
    float3 RGB = HSV.z;

    float var_h = HSV.x * 6;
    float var_i = floor(var_h);   // Or ... var_i = floor( var_h )
    float var_1 = HSV.z * (1.0 - HSV.y);
    float var_2 = HSV.z * (1.0 - HSV.y * (var_h-var_i));
    float var_3 = HSV.z * (1.0 - HSV.y * (1-(var_h-var_i)));
    if      (var_i == 0) { RGB = float3(HSV.z, var_3, var_1); }
    else if (var_i == 1) { RGB = float3(var_2, HSV.z, var_1); }
    else if (var_i == 2) { RGB = float3(var_1, HSV.z, var_3); }
    else if (var_i == 3) { RGB = float3(var_1, var_2, HSV.z); }
    else if (var_i == 4) { RGB = float3(var_3, var_1, HSV.z); }
    else                 { RGB = float3(HSV.z, var_1, var_2); }

    return (RGB);
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

    float PHI = (1.0 + sqrt(50))/2.0;
    float n = i_debug.x * PHI - floor(i_debug.x * PHI);

    o_channel0.xyz = textureColor;//lerp(float3(.1, .6, .1), float3(5.0, 2.0, 3.0), pow(height, 2.0)); // albedo
    // o_channel0.xyz = hsv_to_rgb(float3(n, 0.8, 0.95));
    o_channel0.w = 1.0; //opacity
    o_channel1.xyz = float3(1.0, 1.0, 1.0)*0.01; // specular f0
    o_channel1.w = 1.0; // occlusion
    o_channel2.xyz = normal;
    o_channel2.w = 1.0; // roughness
    o_channel3.xyz = float3(0.0,0.0,0.0); // emissive
    o_channel3.w = 0;
}