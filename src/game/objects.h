// The track's objects: the start positions (0..5, one per car) and loose objects (balls).
#pragma once
#include "bridge/guest.h"
#include "game/vec3.h"

struct TrackObject {
    uint8_t type;  // from the track file
    uint8_t unknown01[3];
    Vec3 pos;  // +0x04
    Vec3 heading;  // +0x10: (cos a, 0, -sin a)
    uint8_t unknown1c;
    uint8_t moving;  // +0x1d: set when pushed, cleared when it comes to rest
    uint8_t unknown1e[2];
    float mass;  // +0x20
    Vec3 vel;  // +0x24
    float radius;  // +0x30
};
static_assert(sizeof(TrackObject) == 0x34);
GLOBAL(ptr32<TrackObject>, g_objects, 0x4a4744);
GLOBAL(uint16_t, g_object_count, 0x4a47f0);    // entries of g_objects; 0..5 are the cars

void object_collide_plane(int i, float px, float py, float pz, float nx, float ny, float nz, float thickness);
void object_collide_line(int i, float px, float py, float pz, float dx, float dy, float dz, float thickness);
Vec3 *object_collide_sphere(Vec3 *impulse, int i, float other_mass, float px, float py, float pz, float radius);
// Collides loose object i with every other object of the track (their shapes by type), 0x42f640.
void object_collide_objects(int i);
// Collides car `car` with every object of the track, 0x420560.
void car_collide_objects(int car);
