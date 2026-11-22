// The particle list (0x419840): smoke, splashes, dust, sparks and debris, moved by `frames` race frames
// and drawn as small discs. Particle types:
//   0 exhaust smoke, 2 dust, 3 tyre smoke, 5..7 smoke of three colours: drift upwards with random turbulence
//     (damped by 0.95, 0.9 for type 7), drawn as a cloud of discs that grows as the particle ages;
//     under water they vanish (smoke shows as one bubble)
//   1 water splash, 4 spray, 8 crew dust, 14..18 debris fragments: fall under gravity with drag
//   9..13 debris: fall freely as one disc and burst into ten fragments (type + 5) when they expire
// The cloud discs are placed around the particle by the random table (angles and distances), so they
// change every frame.
#include <cstdint>

#include "game/fmath.h"
#include "game/race_state.h"
#include "game/render.h"

namespace {
float random_at(int i) { return guest::at<float>(g_random_table + static_cast<uint32_t>(static_cast<uint8_t>(i)) * 4u); }
uint8_t next_random_index(int n) {
    g_random_index = static_cast<uint8_t>(g_random_index + n);
    return g_random_index;
}

// Drifting: damped, then pushed by random turbulence (x / z: r * spread - spread / 2, y: r - sink).
void drift(Particle &p, float damping, float spread, float half, float sink) {
    p.vel.x = p.vel.x * damping;
    p.vel.y = p.vel.y * damping;
    p.vel.z = p.vel.z * damping;
    const uint8_t base = g_random_index;
    next_random_index(3);
    p.vel.x = (random_at(base + 1) * spread - half) + p.vel.x;
    p.vel.y = (random_at(base + 2) - sink) + p.vel.y;
    p.vel.z = (random_at(base + 3) * spread - half) + p.vel.z;
    p.pos.x = p.pos.x + p.vel.x / 100.0f;
    p.pos.y = p.pos.y + p.vel.y / 100.0f;
    p.pos.z = p.pos.z + p.vel.z / 100.0f;
}

// Falling with drag.
void fall(Particle &p, float drag) {
    p.vel.x = p.vel.x * drag + g_gravity.x / 100.0f;
    p.vel.y = p.vel.y * drag + g_gravity.y / 100.0f;
    p.vel.z = p.vel.z * drag + g_gravity.z / 100.0f;
    p.pos.x = p.pos.x + p.vel.x / 100.0f;
    p.pos.y = p.pos.y + p.vel.y / 100.0f;
    p.pos.z = p.pos.z + p.vel.z / 100.0f;
}

void fall_free(Particle &p) {
    p.vel.x = g_gravity.x / 100.0f + p.vel.x;
    p.vel.y = g_gravity.y / 100.0f + p.vel.y;
    p.vel.z = g_gravity.z / 100.0f + p.vel.z;
    p.pos.x = p.vel.x / 100.0f + p.pos.x;
    p.pos.y = p.pos.y + p.vel.y / 100.0f;
    p.pos.z = p.pos.z + p.vel.z / 100.0f;
}

// Runs `step` for up to `frames` frames while the particle lives; the number of frames run.
template <class F> int age(Particle &p, int frames, F step) {
    int n = 0;
    for (; n < frames && p.life != 0; n++) {
        step();
        p.life = static_cast<uint8_t>(p.life - 1);
    }
    return n;
}

// The particle on screen: x, y and view distance z (false when behind the camera).
struct Screen {
    float x, y, z;
};
bool project(const Particle &p, Screen &s) {
    const float dx = p.pos.x - g_view_eye.x, dy = p.pos.y - g_view_eye.y, dz = p.pos.z - g_view_eye.z;
    s.z = dy * g_view_forward.y + dx * g_view_forward.x + dz * g_view_forward.z;
    if (!(1e-4f < s.z)) return false;
    s.x = ((dy * g_view_right.y + dx * g_view_right.x + dz * g_view_right.z) / s.z + 0.5f) * g_start_light_depth;
    s.y = ((dy * g_view_down.y + dx * g_view_down.x + dz * g_view_down.z) / s.z + g_screen_aspect * 0.5f) * g_start_light_depth;
    return true;
}

bool above_water(const Particle &p) { return g_water_level < p.pos.y || g_water_level == p.pos.y; }

// A disc of the cloud at angle `turn` (of a full turn) and distance r around the particle.
void cloud_disc(uint8_t colour, const Screen &s, float turn, float r, float radius) {
    const float a = turn * 6.28f;
    const float sn = guest_sinf(a);
    const float cs = guest_cosf(a);
    draw_disc(colour, false, cs * r + s.x, sn * r + s.y, s.z, radius);
}

// A smoke cloud: discs while k * k * density <= life, each at a random distance (up to `size` / z) and
// colour (base - random * shades).
void smoke_cloud(int i, const Particle &p, const Screen &s, float size, uint8_t base, float shades, int density = 1) {
    for (int k = 0; k * k * density <= p.life; k++) {
        const float r = random_at(i + k);
        const float turn = random_at(i + 1 + k);
        const float d = r * (size / s.z);
        const float a = turn * 6.28f;
        const float sn = guest_sinf(a);
        const float cs = guest_cosf(a);
        draw_disc(static_cast<uint8_t>(base - guest::to_int(r * shades)), false, cs * d + s.x, sn * d + s.y, s.z, 0.3f);
    }
}

// Debris bursting into ten fragments (type + 5) flying off in random directions.
void burst(Particle &p) {
    for (int k = 0; k < 10; k++) {
        Particle &f = particle(g_unknown_4a4794);
        f.type = static_cast<uint8_t>(p.type + 5);
        f.life = 0xff;
        f.pos = p.pos;
        f.vel = p.vel;
        f.vel.x = (random_at(g_random_index + 3 * k + 1) * 30.0f - 15.0f) + f.vel.x;
        f.vel.y = (random_at(g_random_index + 3 * k + 2) * 30.0f - 15.0f) + f.vel.y;
        f.vel.z = (random_at(g_random_index + 3 * k + 3) * 30.0f - 15.0f) + f.vel.z;
        next_particle();
    }
    next_random_index(30);
}

void update(int i, Particle &p, int frames) {
    Screen s;
    switch (p.type) {
    case 0:  // exhaust smoke
        age(p, frames, [&] { drift(p, 0.95f, 2.0f, 1.0f, 0.2f); });
        if (!project(p, s)) break;
        if (above_water(p)) {
            const float size = guest_sqrtf(static_cast<float>(100 - p.life)) * 0.3f * g_start_light_depth;
            smoke_cloud(i, p, s, size, 0x1d, 10.0f);
        } else {
            draw_disc(0xfc, false, s.x, s.y, s.z, 0.4f);  // a bubble
        }
        break;
    case 1: {  // water splash: stops on the water surface
        age(p, frames, [&] { fall(p, 0.98f); });
        if (!project(p, s)) break;
        if (p.pos.y < g_water_level) {
            p.pos.y = g_water_level;
            p.vel = Vec3(0.0f, 0.0f, 0.0f);
        }
        const float r = (guest_sqrtf(static_cast<float>(0x82 - p.life)) * 0.3f * g_start_light_depth) / s.z;
        for (int k = 0; k * k <= p.life; k++) {
            const uint8_t c = next_random_index(1);
            const float a = random_at(k + i) * 6.28f;
            const float sn = guest_sinf(a);
            const float cs = guest_cosf(a);
            draw_disc(static_cast<uint8_t>(-6 - guest::to_int(random_at(c) * -5.0f)), false, cs * r + s.x, sn * r + s.y, s.z, 0.2f);
        }
        break;
    }
    case 2: {  // dust
        age(p, frames, [&] { drift(p, 0.95f, 6.0f, 3.0f, 0.2f); });
        if (!project(p, s)) break;
        if (!above_water(p)) {
            p.life = 0;
            break;
        }
        const float r = (static_cast<float>(0x32 - p.life) * 0.01f * g_start_light_depth) / s.z;
        for (int k = 0, j = 0; k <= p.life; k += 5, j++) cloud_disc(0xae, s, random_at(j + i), r, 0.15f);
        break;
    }
    case 3: {  // tyre smoke
        age(p, frames, [&] { drift(p, 0.95f, 2.0f, 1.0f, 0.3f); });
        if (!project(p, s)) break;
        if (!above_water(p)) {
            p.life = 0;
            break;
        }
        smoke_cloud(i, p, s, g_start_light_depth * 0.8f, static_cast<uint8_t>(-0x4f), 2.0f, 40);
        break;
    }
    case 4: {  // spray
        const int n = age(p, frames, [&] { fall(p, 0.95f); });
        if (!project(p, s)) break;
        if (!above_water(p)) {
            p.life = 0;
            break;
        }
        const float k = g_start_light_depth / s.z;
        draw_disc(static_cast<uint8_t>(0x3b - guest::to_int(random_at(n + i) * 5.0f)), false,
                  (random_at(i) - 0.5f) * k + s.x, (random_at(i + 1) - 0.5f) * k + s.y, k * 0.0f + s.z, 0.3f);
        break;
    }
    case 5:
    case 6:
    case 7: {  // coloured smoke
        static constexpr uint8_t kBase[3] = {0x4a, 0x59, 0x68};
        age(p, frames, [&] { drift(p, p.type == 7 ? 0.9f : 0.95f, 2.0f, 1.0f, 0.5f); });
        if (!project(p, s)) break;
        if (!above_water(p)) {
            p.life = 0;
            break;
        }
        const float size = guest_sqrtf(static_cast<float>(200 - p.life)) * 0.2f * g_start_light_depth;
        smoke_cloud(i, p, s, size, kBase[p.type - 5], 5.0f);
        break;
    }
    case 8: {  // crew dust
        age(p, frames, [&] { fall(p, 0.98f); });
        if (!project(p, s)) break;
        if (!above_water(p)) {
            p.life = 0;
            break;
        }
        const float r = (static_cast<float>(100 - p.life) * 0.01f * g_start_light_depth) / s.z;
        for (int k = 0; k * k <= p.life; k++) cloud_disc(0x95, s, random_at(k + i), r, 0.2f);
        break;
    }
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:  // debris
        age(p, frames, [&] { fall_free(p); });
        if (project(p, s)) draw_disc(static_cast<uint8_t>(p.type + 0x15), false, s.x, s.y, s.z, 0.3f);
        if (p.life == 0) burst(p);
        break;
    case 14:
    case 15:
    case 16:
    case 17:
    case 18: {  // debris fragments
        age(p, frames, [&] { fall(p, 0.98f); });
        if (!project(p, s)) break;
        const float r = ((7.5f - 150.0f / static_cast<float>(0x113 - p.life)) * g_start_light_depth) / s.z;
        if (0.0f <= static_cast<float>(p.life)) {
            for (int k = 0; static_cast<float>(k) * 20.0f <= static_cast<float>(p.life); k++)
                cloud_disc(static_cast<uint8_t>(p.type + 0x10), s, random_at(k + i), r, 0.3f);
        }
        break;
    }
    default:
        break;
    }
}
}  // namespace

void particles_update(int frames) {
    for (int i = 0; i < g_race_options.w1e; i++) {
        Particle &p = particle(static_cast<uint16_t>(i));
        if (p.life != 0) update(i, p, frames);
    }
}
