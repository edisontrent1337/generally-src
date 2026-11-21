// The loaded track: its size and the terrain height grid.
#pragma once
#include "bridge/guest.h"
#include "game/vec3.h"

GLOBAL(float, g_track_size, 0x4a4dfc);         // side length of the (square) track, world units
GLOBAL(int32_t, g_track_length, 0x4a4ee0);     // length of a lap, metres
GLOBAL(ptr32<uint8_t>, g_height_map, 0x4a47a0);  // terrain heights, 64 per row, one byte per grid point
GLOBAL(float, g_height_cell, 0x4a47bc);        // distance between grid points
constexpr int kHeightRow = 64;

float terrain_height(Vec3 p);
void terrain_sample(Vec3 *p, Vec3 *normal);
