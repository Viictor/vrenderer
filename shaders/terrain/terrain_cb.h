#ifndef TERRAIN_CB_H
#define TERRAIN_CB_H

#include "donut/shaders/light_cb.h"
#include "donut/shaders/view_cb.h"

struct TerrainViewConstants
{
	PlanarViewConstants view;
	PlanarViewConstants viewPrev;
};

struct TerrainLightConstants
{
	LightConstants lights[16];
};

struct TerrainParamsConstants
{
	float size;
	float maxHeight;
	float gridSize;
	float padding;
	float4 lodRanges[12]; // same as QuadTree::MAX_LODS
};

#endif // TERRAIN_CB_H