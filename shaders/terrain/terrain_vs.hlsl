#pragma pack_matrix(row_major)

#include "terrain_cb.h"
#include "terrain_common.hlsli"

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
    o_debug = float3(i_instance, i_instance, i_instance);

    o_vtx = i_vtx;
    o_vtx.pos = worldPos.xyz;
    
    float4 viewPos = mul(worldPos, c_Terrain.view.matWorldToView);
    o_position = mul(viewPos, c_Terrain.view.matViewToClip);
}