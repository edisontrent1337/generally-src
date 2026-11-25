// One physics step of one car (0x407cd0): controls, engine, tyres, surface effects, integration and
// the collisions with the ground, the track border, objects and other cars, then the checkpoints.
//
// Car state layout (g_car_states[p], 0x194 bytes) as used here:
//   +0x01 u8   false start: frames of forced full throttle left
//   +0x03 u8   smoke countdown (frames until the next exhaust puff)
//   +0x04 u8[4] per wheel: surface effect build-up (spray / dust / marks)
//   +0x08 u8   splash countdown (water)
//   +0x0c i32  upside-down counter (1000 = being turned back over)
//   +0x18 + 12k  joystick binding of control k (0 accelerate, 1 brake, 2 left, 3 right)
//   +0x48 + 4k   key of control k; +0x48 == 0 marks an AI driver and +0x4c is then its skill
//   +0x58 pos, +0x64 velocity, +0x70 angular velocity, +0x7c / +0x88 / +0x94 axes (right, up, forward)
//   +0xa0 wheel spin speed, +0xa4..+0xb0 wheel loads, +0xb4 body load, +0xb8 load-weighted torque factor
//   +0xbc, +0xc0 front / rear slip, +0xc4 inverse inertia, +0xc8 mass, +0xcc smoothed power
//   +0xd0 grip, +0xd6 u16 next checkpoint, +0xd8 u8 gate toggle, +0xd9 / +0xda last gate / checkpoint
//   +0xde u16 tyre wear, +0xe0 u16 fuel, +0xe2 u16 damage, +0xe4 u8[4] wheel surfaces
//   +0xe8 + 12i  wheel contact points (i = model[0xbd] + wheel)
#include <cstdint>

#include "game/car_physics.h"
#include "game/fmath.h"
#include "game/input.h"
#include "game/menu.h"
#include "game/objects.h"
#include "game/race_options.h"
#include "game/race_state.h"
#include "game/render.h"
#include "game/sound.h"
#include "game/track.h"

namespace {
float random_entry(uint8_t i) { return guest::at<float>(g_random_table + i * 4u); }
float next_random() {
    g_random_index = static_cast<uint8_t>(g_random_index + 1);
    return random_entry(g_random_index);
}

struct Car {
    uint8_t *s;
    const uint8_t *m;  // the car's model record (g_race_models[p])
    float &f(int off) const { return *reinterpret_cast<float *>(s + off); }
    uint8_t &b(int off) const { return s[off]; }
    uint16_t &w(int off) const { return *reinterpret_cast<uint16_t *>(s + off); }
    int32_t &i(int off) const { return *reinterpret_cast<int32_t *>(s + off); }
    Vec3 &v(int off) const { return *reinterpret_cast<Vec3 *>(s + off); }
    float mf(int off) const { return *reinterpret_cast<const float *>(m + off); }

    Vec3 &pos() const { return v(0x58); }
    Vec3 &vel() const { return v(0x64); }
    Vec3 &ang() const { return v(0x70); }
    Vec3 &right() const { return v(0x7c); }
    Vec3 &up() const { return v(0x88); }
    Vec3 &fwd() const { return v(0x94); }
    float &spin() const { return f(0xa0); }
    float &load(int k) const { return f(0xa4 + 4 * k); }
    float &slip(int axle) const { return f(0xbc + 4 * axle); }
    float &inv_inertia() const { return f(0xc4); }
    float &mass() const { return f(0xc8); }
    Vec3 &wheel(int w) const { return v(0xe8 + 0xc * (m[0xbd] + w)); }
    uint8_t &effect(int w) const { return s[4 + w]; }
};

// The tile type a worn tile wears back to (0x28 -> 0, 0x29 -> 1, ...); other types go to 0.
int unworn_tile(uint8_t t) {
    switch (t) {
    case 0x28: return 0;
    case 0x29: return 1;
    case 0x2a: return 0xc;
    case 0x2b: return 0x10;
    case 0x2c: return 0x14;
    case 0x2d: return 0x1c;
    case 0x2e: return 0x20;
    case 0x2f: return 0x24;
    default: return 0;
    }
}
}  // namespace

// Grip of the surface under a point, from the model's per-surface factors (+0x2c..+0x60 for the wheels,
// +0x64..+0x98 for the body). One player uses the factor of the tile's group; with more players the
// factor is interpolated across the group (the two-player tracks use the in-between tile types).
static float surface_grip(const uint8_t *m, int t, int off) {
    auto mf = [&](int o) { return *reinterpret_cast<const float *>(m + off + o); };
    if (g_player_count == 1) {
        if (t == 0) return mf(0x00);
        if (t < 8) return mf(0x08);
        if (t < 0x10) return mf(0x10);
        if (t < 0x14) return mf(0x18);
        if (t < 0x18) return mf(0x20);
        if (t < 0x1c) return mf(0x28);
        if (t < 0x28) return mf(0x30);
        return mf(0x18);
    }
    auto lerp = [&](int o, int base, float div) {
        return ((mf(o + 4) - mf(o)) * static_cast<float>(t - base)) / div + mf(o);
    };
    if (t == 0) return mf(0x00);
    if (t < 8) return lerp(0x04, 1, 6.0f);
    if (t < 0x10) return lerp(0x0c, 8, 7.0f);
    if (t < 0x14) return lerp(0x14, 0x10, 3.0f);
    if (t < 0x18) return lerp(0x1c, 0x14, 3.0f);
    if (t < 0x1c) return lerp(0x24, 0x18, 3.0f);
    if (t < 0x20) return lerp(0x2c, 0x1c, 3.0f);
    if (t < 0x24) return lerp(0x2c, 0x20, 3.0f);
    if (t < 0x28) return lerp(0x2c, 0x24, 3.0f);
    return mf(0x34);
}

void car_step(int p) {
    const Car c{g_car_states[p].raw, g_race_models[p]};
    const uint8_t *m = c.m;
    Vec3 &vel = c.vel(), &ang = c.ang();
    const Vec3 &A = c.right(), &B = c.up(), &C = c.fwd();

    const float speed = guest_sqrtf((vel.x * vel.x + vel.y * vel.y) + vel.z * vel.z);
    const float fwd_speed = (vel.x * C.x + vel.y * C.y) + vel.z * C.z;

    // mass with the fuel load, and the inverse inertia
    c.mass() = (static_cast<float>(c.w(0xe0)) * static_cast<float>(g_race_options.b0c)) * (1.0f / 65536.0f) + c.mf(0xb0);
    c.inv_inertia() = 1200.0f / (c.mass() + 200.0f);
    const float load_sum = (((c.load(0) + c.load(1)) + c.load(2)) + c.load(3)) + c.f(0xb4);
    if (load_sum > 1e-6f) c.f(0xb8) = c.f(0xb8) / load_sum;
    c.f(0xb4) = c.mf(0x48) * c.f(0xb4);

    // wheel grip from the surface under each wheel: 2 = dust (fixed), 3 = water (fixed), else the tile
    for (int w = 0; w < 4; w++) {
        const uint8_t surface = c.b(0xe4 + w);
        float g;
        if (surface == 2) {
            g = c.mf(0x30);
        } else if (surface == 3) {
            g = c.mf(0x50);
        } else {
            const Vec3 &at = c.wheel(w);
            int tx = guest::to_int(at.x / g_tile_size);
            if (tx < 0) tx = 0; else if (tx > 0x1ff) tx = 0x1ff;
            int tz = guest::to_int(at.z / g_tile_size);
            if (tz < 0) tz = 0; else if (tz > 0x1ff) tz = 0x1ff;
            g = surface_grip(m, guest::at<uint8_t>(g_tile_map + static_cast<uint32_t>(tz * 0x200 + tx)), 0x2c);
        }
        c.load(w) = g * c.load(w);
    }

    // engine power: the drive (traction, for steering) and the total, both reduced by tyre wear
    const float wear = (static_cast<float>(c.w(0xde)) * 0.36f) * (1.0f / 65536.0f) + 0.64f;
    float drive = ((((c.load(0) + c.load(1)) * 2.0f) + c.f(0xb4)) * (c.mf(0xb4) * wear)) * 0.45f + 0.2f;
    float total = ((((c.load(0) + c.load(1)) + c.load(2)) + c.load(3)) + c.f(0xb4)) * wear;
    const bool ai = c.i(0x48) == 0;
    const uint32_t level = static_cast<uint32_t>(c.i(0x4c));
    float skill;
    if (ai && level > 100) {
        // skills above 100 make the car itself stronger
        const float k = static_cast<float>(static_cast<double>(level)) * 0.01f;
        total = total * k;
        drive = drive * k;
        skill = 1.0f;
    } else {
        skill = static_cast<float>(static_cast<double>(level)) * 0.01f;
    }
    const float power = total + drive;
    if (power > c.f(0xcc) * 2.0f)
        c.f(0xcc) = c.f(0xcc) * 0.02f + power * 0.49f;
    else
        c.f(0xcc) = c.f(0xcc) * 0.98f + power * 0.01f;
    if (-0.7f > B.y) c.i(0xc) = 1000;

    // controls: throttle -100..100 (1 = idle), steering -1..1
    int throttle;
    float steer = 0.0f;
    if (g_game_state > 2) {
        throttle = 0;
    } else if (c.i(0xc) >= 1000) {
        // upside down: roll back over around the horizontal axis across the up vector
        const float nx = -B.z, nz = B.x;
        const float len = guest_sqrtf((nx * nx + 0.0f) + nz * nz);
        if (len > 0.001f) {
            ang.x = ang.x - nx / len;
            ang.y = ang.y - 0.0f / len;
            ang.z = ang.z - nz / len;
        } else {
            ang.x = ang.x - C.x;
            ang.y = ang.y - C.y;
            ang.z = ang.z - C.z;
        }
        if (B.y > 0.0f) c.i(0xc) = 0;
        throttle = 1;
    } else if (ai) {
        int32_t t = 0;
        float s = 0.0f;
        ai_drive(p, &t, &s, skill);
        throttle = t;
        steer = s;
    } else if (c.b(1) != 0) {
        throttle = 1;
        c.b(1) = static_cast<uint8_t>(c.b(1) - 1);
    } else {
        auto value = [&](int k) {
            return control_value(static_cast<uint32_t>(c.i(0x48 + 4 * k)), static_cast<uint32_t>(c.i(0x18 + 12 * k)),
                                 static_cast<uint32_t>(c.i(0x1c + 12 * k)), static_cast<uint32_t>(c.i(0x20 + 12 * k)));
        };
        const int brake = value(1);
        throttle = value(0) - brake;
        if (static_cast<uint32_t>(throttle + 10) < 21) throttle = 1;
        const int right = value(3);
        steer = static_cast<float>(value(2) - right) * 0.01f;
    }

    // fuel use
    const int used = guest::to_int((static_cast<float>(g_race_options.b0a) * speed) / 100.0f);
    if (used < static_cast<int>(c.w(0xe0)))
        c.w(0xe0) = static_cast<uint16_t>(c.w(0xe0) - used);
    else
        c.w(0xe0) = 0;

    // damage makes the steering wobble
    const uint8_t wobble = static_cast<uint8_t>(g_random_index + 1);
    steer = ((random_entry(wobble) * 4.0f - 2.0f) * static_cast<float>(c.w(0xe2))) * (1.0f / 65536.0f) + steer;
    const float spin0 = c.spin();
    float rev = spin0 > 0.0f ? spin0 * 4.0f + 50.0f : 50.0f - spin0 * 4.0f;
    rev = rev / c.mf(0xa8);

    // a damaged engine smokes, puffs coming faster with more damage and revs
    if (c.w(0xe2) < 5000) {
        g_random_index = wobble;
        c.b(3) = 10;
    } else if (c.b(3) != 0) {
        g_random_index = wobble;
        c.b(3) = static_cast<uint8_t>(c.b(3) - 1);
    } else {
        Particle &q = particle(g_unknown_4a4794);
        *reinterpret_cast<uint16_t *>(&q) = 0x6400;
        const float back = c.mf(0x18);
        q.pos = Vec3(c.pos().x - back * C.x, c.pos().y - C.y * back, c.pos().z - C.z * back);
        q.vel = vel;
        next_particle();
        g_random_index = static_cast<uint8_t>(g_random_index + 2);
        c.b(3) = static_cast<uint8_t>(guest::to_int(((random_entry(g_random_index) * 4.0f + 2.0f) * 655350.0f) /
                                                    (static_cast<float>(c.w(0xe2)) * rev)));
    }

    // wheel spin: accelerate (throttle >= 2), brake / reverse (< 0), else roll with the ground
    float &spin = c.spin();
    auto rolling = [&](bool coast) {
        if (coast) {
            const float x = fwd_speed * 9.25f;
            if (x > 2.0f) return fwd_speed * 0.25f - 2.0f;
            if (x > -2.0f) return fwd_speed * -9.0f;
            return fwd_speed * 0.25f + 2.0f;
        }
        const float x = fwd_speed * 9.9f;
        if (x > 0.5f) return fwd_speed * 0.9f - 0.5f;
        if (x > -0.5f) return fwd_speed * -9.0f;
        return fwd_speed * 0.9f + 0.5f;
    };
    if (throttle >= 2 && c.b(3) >= 10) {
        float v;
        if (c.w(0xe0) == 0)
            v = spin * spin + 14.71f;
        else
            v = ((((static_cast<float>(throttle - 1) / 99.0f) * 14710.0f) / 100.0f) * c.mf(0xa4)) / c.mass() + spin * spin;
        spin = guest_sqrtf(v);
        throttle = 1;
    } else if (throttle < 0 && fwd_speed * 9.25f > 2.0f) {
        spin = fwd_speed * 0.25f - (static_cast<float>(-throttle) / 100.0f) * 2.0f;
        throttle = 0;
    } else if (throttle < 0 && c.b(3) >= 10) {
        spin = -guest_sqrtf(spin * spin - ((static_cast<float>(throttle) / 100.0f) * 1471.0f) / 100.0f);
        throttle = 1;
    } else {
        spin = rolling(throttle == 0);
        throttle = 0;
    }
    const float max_spin = c.mf(0xa8);
    if (spin > max_spin) spin = max_spin;
    if (-max_spin > spin) spin = -max_spin;
    if (c.b(0xd8) == 1) {
        if (spin > 15.0f) spin = 15.0f;
        if (-15.0f > spin) spin = -15.0f;
    }

    // engine sound: quieter when coasting, an octave lower under water
    const uint32_t h = g_engine_sound[p];
    if (h != 0) {
        const int vol = throttle == 0 ? -10000 - guest::to_int(g_unknown_4a47f4 * -0.93f) : guest::to_int(g_unknown_4a47f4) - 10000;
        if (SoundRef *s = sound_ref(h)) {
            const float a = guest_powf(10.0f, static_cast<float>(vol) / -2000.0f);
            s->volume = guest::to_int(128.0 / static_cast<double>(a) + 0.5);
        }
        const float pitch = g_water_level > c.pos().y ? 5000.0f : 10000.0f;
        if (SoundRef *s = sound_ref(h)) s->rate = static_cast<float>(guest::to_int(rev * pitch)) / 22050.0f;
    }

    // tyre forces: find the scale k at which the longitudinal (slip) and lateral forces fit in the
    // friction ellipse of the available power, by bisection on k
    float lat_force;
    if (!(total > 1e-4f)) {
        c.slip(0) = 0.0f;
        c.slip(1) = 0.0f;
        lat_force = 0.0f;
    } else {
        const float spin_in = spin;
        float slip = spin_in - fwd_speed;
        float grip = (slip * 0.1f + fwd_speed) * c.mf(0xac);
        float lat = -((A.y * vel.y + A.x * vel.x) + A.z * vel.z);
        if (grip < 0.0f) grip = -grip;
        const float lat2 = lat * lat, slip2 = slip * slip, total2 = total * total;
        float k;
        if ((lat2 / (grip + 1.0f)) / (grip + 1.0f) + (slip2 / 10.0f) / 10.0f <= total2) {
            k = 1.0f;
        } else {
            k = 2.0f;
            float step = 0.25f;
            do {
                const float q = (slip2 / (k + 9.0f)) / (k + 9.0f) + (lat2 / (k + grip)) / (k + grip);
                k = k / (q <= total2 ? step * k + 1.0f : 1.0f - step * k);
                step = step * 0.5f;
            } while (step > 1e-4f);
        }
        slip = slip / (k + 9.0f);
        const float f = (k - 1.0f) * 0.1f;
        lat = lat / (k + grip);
        spin = spin_in - slip * 9.0f;
        if (throttle == 0) {
            c.slip(0) = f;
        } else {
            float x = (f * lat) / total;
            if (lat <= 0.0f) x = -x;
            c.slip(0) = x;
        }
        const float sum = c.slip(0) + f;
        c.slip(1) = f;
        const float root = guest_sqrtf(sum);
        c.w(0xde) = static_cast<uint16_t>(guest::to_int(
            static_cast<float>(c.w(0xde)) /
            ((((static_cast<float>(g_race_options.b09) * 2e-5f) * root) * total) * total + 1.0f)));
        const float t = c.f(0xb8), ii = c.inv_inertia();
        ang.x = ((slip * A.x - C.x * lat) * t) * ii + ang.x;
        ang.y = ang.y + ((A.y * slip - C.y * lat) * t) * ii;
        ang.z = ang.z + ((A.z * slip - C.z * lat) * t) * ii;
        vel.x = vel.x + (C.x * slip + A.x * lat);
        vel.y = vel.y + (C.y * slip + A.y * lat);
        vel.z = vel.z + (C.z * slip + A.z * lat);
        lat_force = lat;
    }

    // steering: a yaw torque around the up axis from the steering and the lateral tyre force
    float yaw;
    if (g_race_options.b1b == 0) {
        if (speed < 5.0f) lat_force = lat_force * (speed / 5.0f);
        float g = (speed * speed - fwd_speed * fwd_speed) * 0.2f;
        g = (g + fwd_speed) / (g + 5.0f);
        if (!(g >= -1.0f))
            steer = -steer;
        else if (g < 1.0f)
            steer = g * steer;
        if (ai && g < 0.0f) steer = -steer;
        yaw = ((((steer * drive) * 2.0f + lat_force) * 5.0f) * c.inv_inertia()) / (15.0f - c.inv_inertia());
    } else {
        const float x = (steer * drive) * 2.0f + lat_force;
        if (speed >= 5.0f) {
            yaw = ((x * 5.0f) * c.inv_inertia()) / (15.0f - c.inv_inertia());
        } else {
            yaw = ((x * speed) * c.inv_inertia()) / (15.0f - c.inv_inertia());
        }
    }
    ang.z = B.z * yaw + ang.z;
    ang.y = B.y * yaw + ang.y;
    ang.x = ang.x + B.x * yaw;

    // damp the roll / pitch rate and part of the yaw rate
    {
        const float d = ((ang.y * B.y + B.x * ang.x) + B.z * ang.z) * (0.08f - c.inv_inertia() / 15.0f);
        const float ax = ang.x * 0.08f, ay = ang.y * 0.08f, az = ang.z * 0.08f;
        ang.y = (B.y * d - ay) + ang.y;
        ang.x = ang.x + (d * B.x - ax);
        ang.z = (B.z * d - az) + ang.z;
    }

    // surface effects per wheel
    auto spray_vel = [&](float r) {
        return Vec3((vel.x + (vel.x - C.x * spin) * 0.5f) + B.x * r, (vel.y + (vel.y - C.y * spin) * 0.5f) + B.y * r,
                    (vel.z + (vel.z - C.z * spin) * 0.5f) + B.z * r);
    };
    auto emit = [&](int w, uint8_t type, float life) -> Particle & {
        Particle &q = particle(g_unknown_4a4794);
        q.type = type;
        q.life = static_cast<uint8_t>(guest::to_int(life));
        q.pos = c.wheel(w);
        return q;
    };
    // skid sound and the dust divisor shared by dust and the soft surfaces
    auto skid = [&](int w) {
        const float d = (spin > 0.0f ? spin + 40.0f : 40.0f - spin) * 1e-4f * (speed + 10.0f);
        const float v = ((c.slip(w / 2) * c.load(w)) * 0.4f) / d - 1.0f;
        if (v > 0.0f) g_skid_amount = v * 1e7f + g_skid_amount;
        return d;
    };
    for (int w = 0; w < 4; w++) {
        const uint8_t surface = c.b(0xe4 + w);
        uint8_t &e = c.effect(w);
        const float sl = c.slip(w / 2), ld = c.load(w);
        if (surface == 3) {
            // water: spray
            const float quarter = speed * 0.25f;
            const float f = 1.0f - 1.0f / ((sl * 0.5f + quarter) * ld + 1.0f);
            e = static_cast<uint8_t>(guest::to_int(f * 30.0f) - guest::to_int(static_cast<float>(e) * -0.98f));
            if (e > 0x96) {
                Particle &q = emit(w, 8, f * 200.0f);
                q.vel = spray_vel(next_random() * 10.0f);
                next_particle();
                e = static_cast<uint8_t>(e + 0x6a);
            }
            wheel_spray(p, w, 0.5f, (c.slip(w / 2) * 0.5f + quarter) * c.load(w));
        } else if (surface == 2) {
            // dust
            const float d = skid(w);
            const float f = 1.0f - 1.0f / (sl * ld + 1.0f);
            e = static_cast<uint8_t>(
                guest::to_int(static_cast<float>(static_cast<int>(e) - guest::to_int(f * -75.0f)) / (d + 1.0f)));
            wheel_spray(p, w, 0.2f, 1.0f - 1.0f / ((static_cast<float>(e) * c.load(w)) * 0.025f + 1.0f));
            if (e > 0x96) {
                Particle &q = emit(w, 3, f * 200.0f);
                q.vel = vel;
                next_particle();
                e = static_cast<uint8_t>(e + 0x6a);
            }
        } else {
            // the tiles under the wheel's contact disc: soft surfaces get marks
            const float r = 0.2f / g_tile_size;
            const Vec3 &at = c.wheel(w);
            const float wz = at.z / g_tile_size, wx = at.x / g_tile_size;
            int z = guest::to_int((wz - r) + 1.0f);
            if (z < 0) z = 0;
            int z_end = guest::to_int((r + wz) + 1.0f);
            if (z_end > 0x200) z_end = 0x200;
            const float wx1 = wx + 1.0f, r2 = r * r;
            for (; z < z_end; z++) {
                const float dz = static_cast<float>(z) - wz;
                const float half = guest_sqrtf(r2 - dz * dz);
                int x = guest::to_int((wx - half) + 1.0f);
                if (x < 0) x = 0;
                int x_end = guest::to_int(wx1 + half);
                if (x_end > 0x200) x_end = 0x200;
                const uint32_t row = g_tile_map + static_cast<uint32_t>(z) * 0x200u;
                for (; x < x_end; x++) {
                    uint8_t &tile = guest::at<uint8_t>(row + static_cast<uint32_t>(x));
                    const uint8_t t = tile;
                    int delta = 0;
                    if (t == 0) {
                        skid(w);
                        e = 0;
                    } else if (t < 8 || (t >= 0x1c && t < 0x28)) {
                        // grass / gravel kinds: marks fade with skid
                        const int top = t < 8 ? 8 : t < 0x20 ? 0x20 : t < 0x24 ? 0x24 : 0x28;
                        const float k = t < 8 ? 0.025f : 0.07f;
                        const float d = skid(w);
                        const float f = 1.0f - 1.0f / (c.slip(w / 2) * c.load(w) + 1.0f);
                        e = static_cast<uint8_t>(guest::to_int(
                            static_cast<float>(static_cast<int>(e) - guest::to_int(f * -75.0f)) / (d + 1.0f)));
                        delta = guest::to_int((1.0f - 1.0f / ((static_cast<float>(e) * c.load(w)) * k + 1.0f)) *
                                              static_cast<float>(top - t));
                        if (e > 0x96) {
                            Particle &q = emit(w, 3, f * 200.0f);
                            q.vel = vel;
                            next_particle();
                            e = static_cast<uint8_t>(e + 0x6a);
                        }
                    } else if (t < 0x10) {
                        const float g = t < 0xc ? c.slip(w / 2) : c.slip(w / 2) * 3.0f;
                        const float f = 1.0f - 1.0f / (g * c.load(w) + 1.0f);
                        e = static_cast<uint8_t>(guest::to_int(
                            static_cast<float>(static_cast<int>(e) - guest::to_int(f * -25.0f)) * 0.95f));
                        delta = guest::to_int((1.0f - 1.0f / ((static_cast<float>(e) * c.load(w)) * 0.05f + 1.0f)) *
                                              static_cast<float>(0x10 - t));
                        if (e > 0x96) {
                            Particle &q = emit(w, 4, f * 50.0f);
                            q.vel = spray_vel(next_random() * 10.0f);
                            next_particle();
                            e = static_cast<uint8_t>(e + 0x6a);
                        }
                    } else if (t < 0x1c) {
                        const int top = t < 0x14 ? 0x14 : t < 0x18 ? 0x18 : 0x1c;
                        const uint8_t type = t < 0x14 ? 5 : t < 0x18 ? 6 : 7;
                        const float f =
                            1.0f - 1.0f / ((c.slip(w / 2) * 0.5f + speed * 0.25f) * c.load(w) + 1.0f);
                        e = static_cast<uint8_t>(guest::to_int(
                            static_cast<float>(static_cast<int>(e) - guest::to_int(f * -30.0f)) * 0.95f));
                        delta = guest::to_int((1.0f - 1.0f / (static_cast<float>(e) * 0.007f + 1.0f)) *
                                              static_cast<float>(top - t));
                        if (e > 0x96) {
                            Particle &q = emit(w, type, f * 200.0f);
                            q.vel = spray_vel(next_random() * 10.0f);
                            next_particle();
                            e = static_cast<uint8_t>(e + 0x6a);
                        }
                    } else {
                        // a worn tile: wear a random neighbour in the direction of travel, heal this one
                        if (!(c.load(w) > 0.001f)) continue;
                        const uint8_t ix = static_cast<uint8_t>(g_random_index + 1);
                        g_random_index = static_cast<uint8_t>(g_random_index + 2);
                        int nz = guest::to_int((vel.z - spin * C.z) * random_entry(g_random_index)) + z;
                        int nx = guest::to_int((vel.x - spin * C.x) * random_entry(ix)) + x;
                        if (nx < 0) nx = 0; else if (nx > 0x1ff) nx = 0x1ff;
                        if (nz < 0) nz = 0; else if (nz > 0x1ff) nz = 0x1ff;
                        if (nx == x && nz == z) continue;
                        uint8_t &other = guest::at<uint8_t>(g_tile_map + static_cast<uint32_t>(nz * 0x200 + nx));
                        other = worn_tile(other);
                        track_redraw_tile(nx, nz);
                        const uint8_t now = tile;
                        delta = unworn_tile(now) - now;
                    }
                    if (delta != 0) {
                        tile = static_cast<uint8_t>(tile + delta);
                        track_redraw_tile(x, z);
                    }
                }
            }
        }
    }

    // move
    c.pos().x = vel.x / 100.0f + c.pos().x;
    c.pos().y = c.pos().y + vel.y / 100.0f;
    c.pos().z = c.pos().z + vel.z / 100.0f;

    // rotate the axes by the angular velocity (axis-angle, angle = |w| / 100)
    {
        const float len = guest_sqrtf((ang.x * ang.x + ang.y * ang.y) + ang.z * ang.z);
        if (len > 1e-4f) {
            const float ux = ang.x / len, uy = ang.y / len, uz = ang.z / len;
            const float a = len / 100.0f;
            const float s = guest_sinf(a);
            const float co = guest_cosf(a);
            const float k = 1.0f - co;
            const float kx = k * ux, ky = k * uy;
            const float xx = ux * kx + co;
            const float xy = ux * ky - uz * s;
            const float xz = uz * kx + uy * s;
            const float yx = uy * kx + uz * s;
            const float yy = uy * ky + co;
            const float zx = uz * kx - uy * s;
            const float yz = uz * ky - ux * s;
            const float zy = ux * s + uz * ky;
            const float zz = (k * uz) * uz + co;
            auto rotate = [&](Vec3 &a) {
                const Vec3 o = a;
                a = Vec3((yx * o.y + o.x * xx) + zx * o.z, (yy * o.y + o.x * xy) + zy * o.z, (yz * o.y + o.x * xz) + zz * o.z);
            };
            rotate(c.right());
            rotate(c.up());
            rotate(c.fwd());
        }
    }

    // loose objects (6.. are not cars): move, follow the terrain, slow down, stop
    {
        const float cell = g_height_cell;
        const uint32_t heights = g_height_map.a;
        for (int i = 6; i < g_object_count; i++) {
            uint8_t *o = &guest::at<uint8_t>(g_objects.a + static_cast<uint32_t>(i) * 0x34u);
            if (o[0x1c] != 1) continue;
            Vec3 &op = *reinterpret_cast<Vec3 *>(o + 4);
            Vec3 &ov = *reinterpret_cast<Vec3 *>(o + 0x24);
            const float nx = ov.x / 100.0f + op.x;
            const float nz = op.z + ov.z / 100.0f;
            op.y = op.y + ov.y / 100.0f;
            op.x = nx;
            op.z = nz;
            const float limit = g_track_size - 0.001f;
            float cx = nx, cz = nz;
            if (!(cx >= 0.0f)) cx = 0.0f; else if (cx > limit) cx = limit;
            if (!(cz >= 0.0f)) cz = 0.0f; else if (cz > limit) cz = limit;
            float fx = cx / cell, fz = cz / cell;
            const int ix = guest::to_int(fx), iz = guest::to_int(fz);
            fx = fx - static_cast<float>(ix);
            fz = fz - static_cast<float>(iz);
            const uint8_t *hp = &guest::at<uint8_t>(heights + static_cast<uint32_t>(iz * 0x40 + ix));
            const float gx = 1.0f - fx, gz = 1.0f - fz;
            op.y = ((((static_cast<float>(hp[1]) * fx) * gz + (static_cast<float>(hp[0]) * gx) * gz) +
                     (static_cast<float>(hp[0x40]) * gx) * fz) +
                    (static_cast<float>(hp[0x41]) * fx) * fz) *
                   0.05f;
            ov.x = ov.x * 0.95f;
            ov.y = ov.y * 0.95f;
            ov.z = ov.z * 0.95f;
            if (static_cast<double>((ov.x * ov.x + ov.y * ov.y) + ov.z * ov.z) < 0.01) {
                ov = Vec3(0.0f, 0.0f, 0.0f);
                o[0x1d] = 0;
            }
        }
    }

    // air / water drag and buoyancy
    Vec3 ground = c.pos(), normal;
    terrain_sample(&ground, &normal);
    const float radius = c.mf(0x14);
    float fx, fy, fz;
    auto drag = [&](float scale) {
        const float k = c.mf(0x9c), r = c.mf(0xa0);
        const float vx = vel.x * k, vy = vel.y * k, vz = vel.z * k;
        const float bx = B.x * r, by = B.y * r, bz = B.z * r;
        const float f = (scale / c.mass()) / 100.0f;
        fz = (bz + vz) * f;
        fy = (by + vy) * f;
        fx = (bx + vx) * f;
    };
    const float water = g_water_level;
    if (water <= ground.y || water < c.pos().y - radius) {
        drag(speed);
    } else if (water < radius + c.pos().y) {
        // partly under water: lift through the point under water, scaled by the submerged part
        const float depth = (water - c.pos().y) / radius;
        const float s1 = (radius * radius) * B.y;
        const float s2 = (c.mf(0x18) * c.mf(0x18)) * C.y;
        const float s3 = (c.mf(0x10) * c.mf(0x10)) * A.y;
        const float X = (A.x * s3 + B.x * s1) + C.x * s2;
        const float Y = (A.y * s3 + B.y * s1) + C.y * s2;
        const float Z = (A.z * s3 + B.z * s1) + C.z * s2;
        const float L = guest_sqrtf((X * X + Y * Y) + Z * Z) + 0.001f;
        const float speed2 = (vel.x * vel.x + vel.y * vel.y) + vel.z * vel.z;
        const float F = ((speed2 * (((1.0f - depth * depth) * 8.0f) * c.mf(0x9c))) / c.mass()) / 100.0f;
        const float ii = c.inv_inertia();
        const float zero = F * 0.0f;
        ang.x = ((-(Z / L)) * F) * ii + ang.x;
        ang.y = zero * ii + ang.y;
        ang.z = ((X / L) * F) * ii + ang.z;
        vel.x = zero + vel.x;
        vel.y = vel.y + F;
        vel.z = vel.z + zero;
        if (static_cast<float>(c.b(8)) * F > 1.5f) {
            Particle &q = particle(g_unknown_4a4794);
            *reinterpret_cast<uint16_t *>(&q) = 0x6401;
            q.pos = Vec3(c.pos().x, water, c.pos().z);
            q.vel = Vec3(vel.x, (F * 30.0f) * next_random(), vel.z);
            c.b(8) = 0;
            next_particle();
        } else {
            c.b(8) = static_cast<uint8_t>(c.b(8) + 1);
        }
        c.i(4) = 0;
        const float k = c.mf(0x9c), r = c.mf(0xa0);
        const float f = (((depth * 4.0f + 5.0f) * speed) / c.mass()) / 100.0f;
        fx = (B.x * r + vel.x * k) * f;
        fz = (B.z * r + vel.z * k) * f;
        fy = (B.y * r + vel.y * k) * f;
    } else {
        // under water
        c.i(4) = 0;
        drag(speed * 9.0f);
    }
    vel.z = (g_gravity.z / 100.0f - fz) + vel.z;
    vel.y = (g_gravity.y / 100.0f - fy) + vel.y;
    vel.x = (g_gravity.x / 100.0f - fx) + vel.x;

    // wheel contact points for the next step; the loads are summed again by the collisions
    for (int w = 0; w < 4; w++) {
        const float *mw = reinterpret_cast<const float *>(m + 0x504 + 0xc * w);
        const float x = mw[0], y = mw[1], z = mw[2];
        const Vec3 &P = c.pos();
        Vec3 &out = c.wheel(w);
        out.x = ((A.x * x + P.x) + B.x * y) + C.x * z;
        out.z = ((A.z * x + P.z) + B.z * y) + C.z * z;
        out.y = 0.0f;
        c.load(w) = 0.0f;
    }
    c.f(0xb4) = 0.0f;
    c.f(0xb8) = 0.0f;
    c.f(0xd0) = 1.0f - 3.0f / (speed + 3.0f);

    // collisions: the ground (with the grip of the tile under the car) and the four borders
    const float friction = surface_grip(
        m,
        guest::at<uint8_t>(g_tile_map + static_cast<uint32_t>(guest::to_int(c.pos().z / g_tile_size)) * 0x200u +
                           static_cast<uint32_t>(guest::to_int(c.pos().x / g_tile_size))),
        0x64);
    car_plane_collide(p, 1, ground.x - normal.x * 10.0f, ground.y - normal.y * 10.0f, ground.z - normal.z * 10.0f,
                      normal.x, normal.y, normal.z, 10.0f, friction, 0.3f, 0.03f);
    const float far = g_track_size + 1.0f;
    car_plane_collide(p, 0, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.5f, 1.0f);
    car_plane_collide(p, 0, far, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.5f, 1.0f);
    car_plane_collide(p, 0, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.5f, 1.0f);
    car_plane_collide(p, 0, 0.0f, 0.0f, far, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f, 1.5f, 1.0f);
    car_collide_objects(p);
    for (int i = 6; i < g_object_count; i++) {
        const uint8_t *o = &guest::at<uint8_t>(g_objects.a + static_cast<uint32_t>(i) * 0x34u);
        if (o[0x1c] != 1 || o[0x1d] != 1) continue;
        const float edge = g_track_size + 1.0f;
        object_collide_plane(i, -1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
        object_collide_plane(i, edge, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f);
        object_collide_plane(i, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
        object_collide_plane(i, 0.0f, 0.0f, edge, 0.0f, 0.0f, -1.0f, 1.0f);
        object_collide_objects(i);
    }

    // checkpoints: crossing the next one in order advances; the last one completes a lap
    if (g_checkpoint_count != 0) {
        const uint16_t cp = c.w(0xd6);
        const Segment &sg = guest::at<Segment>(g_checkpoints + cp * 0x14u);
        const float rx = c.pos().x - sg.x, rz = c.pos().z - sg.z;
        const float along = sg.dx * rx + sg.dz * rz;
        const float side = rx * sg.dz - sg.dx * rz;
        if (along > 0.0f && sg.length > along && side > -1.0f && 1.0f > side) {
            if (cp != static_cast<int>(static_cast<int8_t>(c.b(0xda)))) {
                c.b(0xda) = static_cast<uint8_t>(cp);
                c.w(0xd6) = static_cast<uint16_t>(cp + 1);
                if (c.w(0xd6) >= g_checkpoint_count) {
                    lap_line_crossed(p);
                    c.w(0xd6) = 0;
                }
            }
        } else {
            c.b(0xda) = 0xff;
        }
    }

    // gates: entering one toggles +0xd8 (the speed limit through pit lanes)
    bool inside = false;
    for (int i = 0; i < g_gate_count; i++) {
        const Segment &sg = guest::at<Segment>(g_gates + static_cast<uint32_t>(i) * 0x14u);
        const float rx = c.pos().x - sg.x, rz = c.pos().z - sg.z;
        const float along = rx * sg.dx + sg.dz * rz;
        const float side = rx * sg.dz - sg.dx * rz;
        if (along > 0.0f && sg.length > along && side > -1.0f && 1.0f > side) {
            inside = true;
            if (i != static_cast<int>(static_cast<int8_t>(c.b(0xd9)))) {
                c.b(0xd9) = static_cast<uint8_t>(i);
                c.b(0xd8) = c.b(0xd8) == 0;
            }
        }
    }
    if (!inside) c.b(0xd9) = 0xff;
}
