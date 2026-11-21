// Loose objects on the track (balls).
#pragma once
#include "bridge/guest.h"
#include "game/vec3.h"

struct TrackObject {
    uint32_t unknown00;
    Vec3 pos;  // +0x04
    uint8_t unknown10[0x10];
    float mass;  // +0x20
    Vec3 vel;  // +0x24
    float radius;  // +0x30
};
static_assert(sizeof(TrackObject) == 0x34);
GLOBAL(ptr32<TrackObject>, g_objects, 0x4a4744);

void object_collide_plane(int i, float px, float py, float pz, float nx, float ny, float nz, float thickness);
void object_collide_line(int i, float px, float py, float pz, float dx, float dy, float dz, float thickness);
Vec3 *object_collide_sphere(Vec3 *impulse, int i, float other_mass, float px, float py, float pz, float radius);
