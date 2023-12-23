#ifndef TERRAIN_CB_H
#define TERRAIN_CB_H

#include "donut/shaders/light_cb.h"
#include "donut/shaders/view_cb.h"

struct TerrainViewConstants
{
	PlanarViewConstants view;
	PlanarViewConstants viewPrev;
	float size;
	float maxHeight;
};

struct TerrainLightConstants
{
	LightConstants lights[16];
};

#endif // TERRAIN_CB_H