// Loose objects on the track (0x34-byte records at g_objects): balls that roll, bounce off walls and
// get pushed by cars. Layout so far: +0x04 position, +0x20 mass, +0x24 velocity, +0x30 radius.
#include "game/objects.h"

#include "game/fmath.h"

// Collision of object i with the plane through `p` with normal `n` (unit length), the plane being
// `thickness` thick on each side: when the object's centre is within radius + thickness of the plane and
// moving into it, it is pushed out along the normal (facing the object) and its velocity component
// into the plane is removed.
void object_collide_plane(int i, float px, float py, float pz, float nx, float ny, float nz, float thickness) {
    TrackObject &o = g_objects.get()[i];
    float dx = o.pos.x - px, dy = o.pos.y - py, dz = o.pos.z - pz;
    float dist = ny * dy + nx * dx + nz * dz;
    if (0.0f > dist) {  // the normal is turned to face the object (dist keeps its sign)
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    float r = o.radius;
    if (!(dist > -thickness - r && r + thickness > dist)) return;
    // the object's nearest point, relative to the plane's far surface
    float ay = -ny * r - ny * thickness + dy;
    float ax = -nx * r - nx * thickness + dx;
    float az = -nz * r - nz * thickness + dz;
    float closing = -(o.vel.y * ny + o.vel.x * nx + o.vel.z * nz);
    float depth = -(ny * ay + nx * ax + nz * az);
    if (depth > 0.0f && closing > 0.0f) {
        o.pos.x = o.pos.x + depth * nx;
        o.pos.y = o.pos.y + depth * ny;
        o.pos.z = o.pos.z + depth * nz;
        o.vel.x = closing * nx + o.vel.x;
        o.vel.y = o.vel.y + closing * ny;
        o.vel.z = o.vel.z + closing * nz;
    }
}

// Collision of object i with the line through `p` along `d` (any length), `thickness` thick: the object's
// offset from the line (perpendicular part) gives the normal; when closer than radius + thickness and
// moving towards the line it is pushed out and its velocity towards the line removed.
// (The push-out uses the unnormalised offset times the radius, as the original does.)
void object_collide_line(int i, float px, float py, float pz, float dx, float dy, float dz, float thickness) {
    TrackObject &o = g_objects.get()[i];
    float len = guest_sqrtf(dy * dy + dx * dx + dz * dz) + 0.001f;
    float ux = dx / len, uy = dy / len, uz = dz / len;
    float rx = o.pos.x - px, ry = o.pos.y - py, rz = o.pos.z - pz;
    float along = ry * uy + rx * ux + rz * uz;
    rx = rx - along * ux;
    ry = ry - along * uy;
    rz = rz - along * uz;
    float reach = o.radius + thickness;
    float dist2 = ry * ry + rx * rx + rz * rz;
    if (!(reach * reach > dist2)) return;
    float dist = guest_sqrtf(dist2) + 0.001f;
    float nx = rx / dist, ny = ry / dist, nz = rz / dist;
    float r = o.radius;
    float ax = -rx * r - nx * thickness + rx;
    float ay = -ry * r - ny * thickness + ry;
    float az = -rz * r - nz * thickness + rz;
    float closing = -(o.vel.y * ny + nx * o.vel.x + o.vel.z * nz);
    float depth = -(ny * ay + nx * ax + nz * az);
    if (depth > 0.0f && closing > 0.0f) {
        o.pos.x = depth * nx + o.pos.x;
        o.pos.y = o.pos.y + depth * ny;
        o.pos.z = o.pos.z + depth * nz;
        o.vel.y = ny * closing + o.vel.y;
        o.vel.z = nz * closing + o.vel.z;
        o.vel.x = o.vel.x + nx * closing;
    }
}

// Collision of object i with a sphere (centre p, `radius`), e.g. a car's wheel. When they overlap and the
// object moves towards the sphere it is pushed out along the centre line; its velocity towards the sphere
// is removed entirely if other_mass >= 0 (an immovable sphere), else shared by the masses (|other_mass|
// against the object's). Returns in *impulse the push the sphere gets back:
// 100 * object mass * n * closing speed * |other_mass| / (object mass + |other_mass|), zero unless
// other_mass < 0 and they collided.
// (The push-out uses the unnormalised offset times the object's radius, as the original does.)
Vec3 *object_collide_sphere(Vec3 *impulse, int i, float other_mass, float px, float py, float pz, float radius) {
    TrackObject &o = g_objects.get()[i];
    float nx = 0.0f, ny = 0.0f, nz = 0.0f, closing = 0.0f, m = 0.0f;
    float dy = o.pos.y - py, dx = o.pos.x - px, dz = o.pos.z - pz;
    float reach = o.radius + radius;
    float dist2 = dy * dy + dx * dx + dz * dz;
    if (reach * reach > dist2) {
        float dist = guest_sqrtf(dist2) + 0.001f;
        nx = dx / dist;
        ny = dy / dist;
        nz = dz / dist;
        float r = o.radius;
        float ay = -dy * r - ny * radius + dy;
        float az = -dz * r - nz * radius + dz;
        float ax = -dx * r - nx * radius + dx;
        closing = -(o.vel.y * ny + nx * o.vel.x + o.vel.z * nz);
        float depth = -(ny * ay + nx * ax + nz * az);
        if (depth > 0.0f && closing > 0.0f) {
            o.pos.x = o.pos.x + depth * nx;
            o.pos.y = o.pos.y + depth * ny;
            o.pos.z = o.pos.z + depth * nz;
            float vx = nx * closing, vy = ny * closing, vz = nz * closing;
            if (0.0f > other_mass) {
                m = -other_mass;
                float total = o.mass + m;
                o.vel.y = vy * m / total + o.vel.y;
                o.vel.z = vz * m / total + o.vel.z;
                o.vel.x = o.vel.x + vx * m / total;
            } else {
                o.vel.x = o.vel.x + vx;
                o.vel.y = o.vel.y + vy;
                o.vel.z = o.vel.z + vz;
            }
        }
    }
    float total = o.mass + m;
    float k = o.mass * 100.0f;
    impulse->x = k * nx * closing * m / total;
    impulse->y = k * ny * closing * m / total;
    impulse->z = k * nz * closing * m / total;
    return impulse;
}
