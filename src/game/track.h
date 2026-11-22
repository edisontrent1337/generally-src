// The loaded track: its size and the terrain height grid.
#pragma once
#include "bridge/guest.h"
#include "game/vec3.h"

GLOBAL(float, g_track_size, 0x4a4dfc);         // side length of the (square) track, world units
GLOBAL(int32_t, g_track_length, 0x4a4ee0);     // length of a lap, metres
GLOBAL(ptr32<uint8_t>, g_height_map, 0x4a47a0);  // terrain heights, 64 per row, one byte per grid point
GLOBAL(float, g_height_cell, 0x4a47bc);        // distance between grid points
constexpr int kHeightRow = 64;
GLOBAL(float, g_view_zoom, 0x4a4db8);          // race camera distance factor (track file, gr.ini Zoom)

// A line segment across the track: start, unit direction, length.
struct Segment {
    float x, z, dx, dz, length;
};
static_assert(sizeof(Segment) == 0x14);
GLOBAL(uint8_t, g_checkpoint_count, 0x4a4731);
GLOBAL(uint32_t, g_checkpoints, 0x4a477c);     // Segment[g_checkpoint_count]; the last is the finish line
GLOBAL(uint8_t, g_gate_count, 0x4a475f);
GLOBAL(uint32_t, g_gates, 0x4a47e8);           // Segment[g_gate_count]: pit lane gates
GLOBAL(uint32_t, g_race_line, 0x4a473c);       // Vec3[g_race_line_count]
GLOBAL(uint16_t, g_race_line_count, 0x4a47e0);
GLOBAL(uint32_t, g_pit_line, 0x4a47ec);        // Vec3[g_pit_line_count]
GLOBAL(uint8_t, g_pit_line_count, 0x4a475d);
GLOBAL(uint8_t, g_pit_entry, 0x4a4730);        // racing-line point where the pit lane starts
GLOBAL(uint8_t, g_pit_rejoin, 0x4a4793);       // racing-line point where it ends

float terrain_height(Vec3 p);
void terrain_sample(Vec3 *p, Vec3 *normal);
// Turns tile type t into the "worn" type (0x28..0x2f) of its group; types 0x18..0x1b and >= 0x28 stay.
uint8_t worn_tile(uint8_t t);
// Loads tracks\<selected track>.trk; 0 on success, -1 if it is missing or of a newer version.
int track_load();
