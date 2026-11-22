// The loaded track (see track.h).
#include "game/track.h"

#include "game/fmath.h"

uint8_t worn_tile(uint8_t t) {
    if (t == 0) return 0x28;
    if (t < 8) return 0x29;
    if (t < 0x10) return 0x2a;
    if (t < 0x14) return 0x2b;
    if (t < 0x18) return 0x2c;
    if (t < 0x1c) return t;
    if (t < 0x20) return 0x2d;
    if (t < 0x24) return 0x2e;
    if (t < 0x28) return 0x2f;
    return t;
}

// Terrain height below p (x/z, y ignored): bilinear interpolation of the four surrounding grid points;
// heights are stored in units of 0.05. Positions are clamped to the track.
float terrain_height(Vec3 p) {
    float x = p.x, z = p.z;
    float limit = g_track_size - 0.001f;
    if (0.0f > x) x = 0.0f;
    else if (x > limit) x = limit;
    if (0.0f > z) z = 0.0f;
    else if (z > limit) z = limit;
    x /= g_height_cell;
    z /= g_height_cell;
    int ix = static_cast<int>(x), iz = static_cast<int>(z);
    float fx = x - static_cast<float>(ix), fz = z - static_cast<float>(iz);
    const uint8_t *h = g_height_map.get() + iz * kHeightRow + ix;
    float h00 = static_cast<float>(h[0]) * (1.0f - fx) * (1.0f - fz);
    float h01 = static_cast<float>(h[1]) * fx * (1.0f - fz);
    float h10 = static_cast<float>(h[kHeightRow]) * (1.0f - fx) * fz;
    float h11 = static_cast<float>(h[kHeightRow + 1]) * fx * fz;
    return (h00 + h01 + h10 + h11) * 0.05f;
}

// The terrain at p: clamps p's x/z to the track, sets p.y to the terrain height there (as
// terrain_height) and *normal to the surface normal (-dh/dx, 1, -dh/dz), normalised. The slopes are
// interpolated from the differences to the next grid points (only the two nearest within the last two
// columns / rows of the grid).
void terrain_sample(Vec3 *p, Vec3 *normal) {
    float limit = g_track_size - 0.001f;
    if (0.0f > p->x) p->x = 0.0f;
    else if (p->x > limit) p->x = limit;
    if (0.0f > p->z) p->z = 0.0f;
    else if (p->z > limit) p->z = limit;
    float cell = g_height_cell;
    float x = p->x / cell, z = p->z / cell;
    int ix = guest::to_int(x), iz = guest::to_int(z);
    const uint8_t *h = g_height_map.get() + iz * kHeightRow + ix;
    auto H = [&](int k) { return static_cast<int>(h[k]); };
    auto F = [](int v) { return static_cast<float>(v); };
    float u = x - static_cast<float>(ix), u1 = 1.0f - u;
    float v = z - static_cast<float>(iz), v1 = 1.0f - v;
    p->y = (F(H(0)) * u1 * v1 + F(H(1)) * u * v1 + F(H(0x40)) * u1 * v + F(H(0x41)) * u * v) * 0.05f;

    float sx;  // slope along x, in height units per cell
    if (ix > 61) {
        sx = F(H(0x41) - H(0x40)) * v + F(H(1) - H(0)) * v1;
    } else {
        sx = F(H(2) - H(1)) * u * v1 + F(H(1) - H(0)) * u1 * v1;
        sx = sx + F(H(0x41) - H(0x40)) * u1 * v;
        sx = sx + F(H(0x42) - H(0x41)) * u * v;
    }
    float gx = sx * 0.05f / cell;
    float sz;
    if (iz > 61) {
        sz = F(H(0x41) - H(1)) * u + F(H(0x40) - H(0)) * u1;
    } else {
        sz = F(H(0x80) - H(0x40)) * v * u1 + F(H(0x40) - H(0)) * v1 * u1;
        sz = sz + F(H(0x41) - H(1)) * v1 * u;
        sz = sz + F(H(0x81) - H(0x41)) * v * u;
    }
    float gz = sz * 0.05f / cell;
    float len = guest_sqrtf(gx * gx + 1.0f + gz * gz);
    normal->x = -gx / len;
    normal->y = 1.0f / len;
    normal->z = -gz / len;
}
