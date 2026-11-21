// Car physics: collisions of the moving cars (g_car_states) with the world.
#pragma once
#include "bridge/guest.h"
#include "game/vec3.h"

GLOBAL(float, g_impact_energy, 0x4a474c);  // summed hard-impact energy (drives the crash sounds)

Vec3 *car_sphere_collide(Vec3 *out, int car, float bounce, float qx, float qy, float qz, float radius, float friction,
                         float damping);
void car_cylinder_collide(int car, float px, float py, float pz, float dx, float dy, float dz, float radius, float friction,
                          float damping, float bounce);
void car_plane_collide(int car, uint8_t surface, float px, float py, float pz, float nx, float ny, float nz, float t, float friction,
                       float damping, float bounce);
void car_car_collide(int a, int b);
void car_step(int p);
void ai_drive(int p, int32_t *throttle, float *steer, float skill);
