// The AI driver (0x40b890): throttle and steering of a computer-driven car.
//
// The car follows the track's racing line (g_race_line, a closed loop of points) or, when it pits, the pit
// lane (g_pit_line, entered at racing-line point g_pit_entry and rejoining at g_pit_rejoin) and then a pit
// box. Per car (g_car_states[p]):
//   +0x01 u8   route: 0 racing line, 1 / 2 pit lane (2 = leaving), 3 + b = driving into / standing in pit box b
//   +0x02 u8   pit request (1 = pit at the next chance, 2 = racing to the box)
//   +0x0c i32  stuck counter (frames without progress; 140..199 of each 400 back up, 200..399 drive on)
//   +0x50 u32  index of the next line point
//   +0x54 i32  wanted lateral offset, 0..200 (100 = on the line; set from traffic)
//
// Throttle is -100..100 (1 = coast), steering -1 or 1 (full lock towards the aim point).
#include <cstdint>

#include "game/fmath.h"
#include "game/menu.h"
#include "game/race_options.h"
#include "game/race_state.h"
#include "game/render.h"
#include "game/track.h"
#include "game/vec3.h"

GLOBAL(uint32_t, g_race_line, 0x4a473c);       // Vec3[g_race_line_count]
GLOBAL(uint16_t, g_race_line_count, 0x4a47e0);
GLOBAL(uint32_t, g_pit_line, 0x4a47ec);        // Vec3[g_pit_line_count]
GLOBAL(uint8_t, g_pit_line_count, 0x4a475d);
GLOBAL(uint8_t, g_pit_entry, 0x4a4730);        // racing-line point where the pit lane starts
GLOBAL(uint8_t, g_pit_rejoin, 0x4a4793);       // racing-line point where it ends
GLOBAL(uint8_t, g_pit_shared, 0x4a4732);       // nonzero: each car uses the pit box of its own number

namespace {
// The parts of a pit box (g_pits, 0x188 bytes) used here.
struct Box {
    const uint8_t *b;
    uint8_t owner() const { return b[0]; }
    uint8_t crew(int k) const { return b[k]; }  // 1, 2, 7, 8: service timers, 0xff = done
    const Vec3 &pos() const { return *reinterpret_cast<const Vec3 *>(b + 0x16c); }
    const Vec3 &dir() const { return *reinterpret_cast<const Vec3 *>(b + 0x178); }
};
Box box(int i) { return Box{&guest::at<uint8_t>(g_pits.a + static_cast<uint32_t>(i) * 0x188u)}; }

const Vec3 &line_point(int i) { return guest::at<Vec3>(g_race_line + static_cast<uint32_t>(i) * 0xcu); }
const Vec3 &pit_point(int i) { return guest::at<Vec3>(g_pit_line + static_cast<uint32_t>(i) * 0xcu); }

struct Car {
    uint8_t *s;
    float &f(int off) const { return *reinterpret_cast<float *>(s + off); }
    int32_t &i(int off) const { return *reinterpret_cast<int32_t *>(s + off); }
    uint32_t &u(int off) const { return *reinterpret_cast<uint32_t *>(s + off); }
    uint16_t &w(int off) const { return *reinterpret_cast<uint16_t *>(s + off); }
    Vec3 &v(int off) const { return *reinterpret_cast<Vec3 *>(s + off); }
};

float model_f(int p, int off) { return *reinterpret_cast<const float *>(g_race_models[p] + off); }
float rnd() {
    g_random_index = static_cast<uint8_t>(g_random_index + 1);
    return guest::at<float>(g_random_table + g_random_index * 4u);
}
float unsigned_to_float(int32_t v) { return static_cast<float>(static_cast<double>(static_cast<uint32_t>(v))); }

// The wanted offset from the cars ahead: start from `base` and move away from each car that is near and
// closing in (looking ahead by `look` seconds), clamped to 0..10 and stored as 0..200 in +0x54.
void traffic_offset(int p, const Car &c, float base, float look) {
    const Vec3 &P = c.v(0x58), &V = c.v(0x64), &A = c.v(0x7c), &C = c.v(0x94);
    float f = base;
    for (int u = 0; u < g_player_count; u++) {
        if (u == p) continue;
        const uint8_t *o = g_car_states[u].raw;
        const Vec3 &Q = *reinterpret_cast<const Vec3 *>(o + 0x58), &W = *reinterpret_cast<const Vec3 *>(o + 0x64);
        const float ahead = ((Q.y - P.y) * C.y + (Q.x - P.x) * C.x) + (Q.z - P.z) * C.z;
        const float reach = (model_f(u, 0x18) + model_f(p, 0x18)) + 0.5f;
        if (!(-ahead < reach)) continue;
        const float closing = ((W.y - V.y) * C.y + (W.x - V.x) * C.x) + (W.z - V.z) * C.z;
        if (!(closing * look + ahead < reach)) continue;
        const float side = ((Q.y - P.y) * A.y + (Q.x - P.x) * A.x) + (Q.z - P.z) * A.z;
        const float room = (model_f(p, 0x10) + 0.5f) + model_f(u, 0x10);
        if (side <= 0.0f) {
            const float x = room + side;
            if (0.0f < x) f = f + x;
        } else {
            const float x = side - room;
            if (x < 0.0f) f = x + f;
        }
    }
    if (!(0.0f <= f))
        c.i(0x54) = 0;
    else if (f <= 10.0f)
        c.i(0x54) = guest::to_int(f / 0.05f);
    else
        c.i(0x54) = 200;
}

// Another car near box b's approach, closer to it than `limit` (squared) and heading into it: true when
// its predicted path passes within 5 of the box's line.
bool box_contested(int p, const Box &bx, float limit) {
    bool hit = false;
    for (int u = 0; u < g_player_count; u++) {
        if (u == p) continue;
        const uint8_t *o = g_car_states[u].raw;
        const Vec3 &Q = *reinterpret_cast<const Vec3 *>(o + 0x58), &W = *reinterpret_cast<const Vec3 *>(o + 0x64);
        float dx = bx.pos().x - Q.x, dy = bx.pos().y - Q.y, dz = bx.pos().z - Q.z;
        if (!((dy * dy + dx * dx) + dz * dz < limit)) continue;
        float k = (dy * W.y + dx * W.x) + dz * W.z;
        if (!(0.0f < k)) continue;
        k = k / (((W.y * W.y + W.x * W.x) + W.z * W.z) + 225.0f);
        dz = dz - k * W.z;
        dx = dx - k * W.x;
        dy = dy - k * W.y;
        const Vec3 &D = bx.dir();
        const float q = ((D.x * dx + dy * D.y) + dz * D.z) * 0.5f;
        dy = dy - D.y * q;
        dx = dx - q * D.x;
        dz = dz - D.z * q;
        if ((dy * dy + dx * dx) + dz * dz < 25.0f) hit = true;
    }
    return hit;
}
}  // namespace

void ai_drive(int p, int32_t *throttle, float *steer, float skill) {
    const Car c{g_car_states[p].raw};
    const Vec3 &P = c.v(0x58), &V = c.v(0x64), &W = c.v(0x70), &A = c.v(0x7c), &B = c.v(0x88), &C = c.v(0x94);
    uint8_t &route = c.s[1];
    int32_t &target = c.i(0x54);
    const float speed = guest_sqrtf((V.x * V.x + V.y * V.y) + V.z * V.z);
    const float fwd = (C.y * V.y + C.x * V.x) + C.z * V.z;
    const int32_t stuck = c.i(0xc);
    if (g_unknown_4a47f8 < stuck) {
        *throttle = 0;
        *steer = 0.0f;
        return;
    }

    // standing in our pit box: wait for the crew, then leave (route 2)
    if (route >= 3 && box(route - 3).owner() == p) {
        const Box bx = box(route - 3);
        bool done = bx.crew(1) == 0xff && bx.crew(2) == 0xff && bx.crew(8) == 0xff;
        if (done) {
            bool need_fuel;
            if (g_race_options.length_unit == 2 && g_unknown_4a4792 == 0)
                need_fuel = c.w(0xe0) < 0xffff;
            else
                need_fuel = static_cast<int32_t>(c.w(0xe0)) <=
                            static_cast<int32_t>(static_cast<uint32_t>(g_race_laps - c.w(0xd4)) * g_race_options.b0a *
                                                 static_cast<uint32_t>(g_track_length));
            if (need_fuel && bx.crew(7) != 0xff) done = false;
        }
        if (done) route = 2;
        *steer = 0.0f;
        *throttle = 1;
        return;
    }

    // stuck: drive on (200..399 of each 400 frames), or back up towards the line (140..199)
    const int32_t phase = stuck % 400;
    if (phase > 199) {
        *steer = 0.0f;
        *throttle = 100;
        if (20.0f < speed) {
            c.i(0xc) = (stuck - phase) + 400;
            return;
        }
        c.i(0xc) = stuck + 1;
        return;
    }
    if (phase > 0x8b) {
        float dx = 0.0f, dy = 0.0f, dz = 0.0f;
        if (route == 0) {
            const uint32_t i = c.u(0x50);
            const uint32_t j = i + 1 < g_race_line_count ? i + 1 : 0;
            const Vec3 &a = line_point(static_cast<int>(i)), &b = line_point(static_cast<int>(j));
            dx = a.x - P.x;
            dy = a.y - P.y;
            const float sx = b.x - a.x, sy = b.y - a.y;
            dz = a.z - P.z;
            const float sz = b.z - a.z;
            const float t = ((dy * sy + dx * sx) + dz * sz) / (((sy * sy + sx * sx) + sz * sz) + 1e-4f);
            dx = dx - t * sx;
            dy = dy - t * sy;
            dz = dz - t * sz;
        } else if (route < 3) {
            const int i = static_cast<int>(c.u(0x50));
            const Vec3 &a = pit_point(i), &b = pit_point(i + 1);
            dx = a.x - P.x;
            dy = a.y - P.y;
            const float sx = b.x - a.x, sy = b.y - a.y;
            dz = a.z - P.z;
            const float sz = b.z - a.z;
            const float t = ((dy * sy + dx * sx) + dz * sz) / (((sy * sy + sx * sx) + sz * sz) + 1e-4f);
            dx = dx - sx * t;
            dy = dy - sy * t;
            dz = dz - sz * t;
        } else if (route < 9) {
            const Vec3 &D = box(route - 3).dir();
            if ((D.y * C.y + C.x * D.x) + D.z * C.z <= 0.0f) {
                dx = -D.x;
                dy = -D.y;
                dz = -D.z;
            } else {
                dx = D.x;
                dy = D.y;
                dz = D.z;
            }
        }
        *steer = 0.0f <= (A.y * dy + A.x * dx) + A.z * dz ? -1.0f : 1.0f;
        *throttle = -100;
        c.i(0xc) = stuck + 1;
        return;
    }

    float dx, dy, dz;  // the aim for the throttle decision, relative to the car
    bool decide = true;
    if (route == 0) {
        // the racing line
        const uint32_t i = c.u(0x50);
        const int h = static_cast<int>(i == 0 ? g_race_line_count : i) - 1;
        const Vec3 &a = line_point(h), &b = line_point(static_cast<int>(i));
        const float sx = b.x - a.x, sy = b.y - a.y, sz = b.z - a.z;
        const float rx = P.x - a.x, rz = P.z - a.z;
        const float look = (2.5f / guest_sqrtf(c.f(0xcc) * 100.0f)) * skill;
        const float t = (((P.y - a.y) * sy + rx * sx) + rz * sz) / (((sy * sy + sx * sx) + sz * sz) + 1e-4f);
        if (0.0f < t) {
            float side = (rx * sz - rz * sx) * 0.75f;
            side = side / guest_sqrtf(sz * sz + sx * sx) + 1.5f;
            if (!(0.0f <= side))
                side = 0.0f;
            else if (3.0f < side)
                side = 3.0f;
            traffic_offset(p, c, (rnd() * 3.0f + 2.0f) + side, look);
            c.u(0x50) = c.u(0x50) + 1 < g_race_line_count ? c.u(0x50) + 1 : 0;
            if (c.u(0x50) == g_pit_entry) {
                bool pit = false;
                if (c.s[2] == 1) {
                    pit = true;
                } else if (g_leader_laps < g_race_laps) {
                    // will the fuel or the car last? pit when it would not (or, at random, early)
                    const float left = static_cast<float>(static_cast<int32_t>((g_race_laps - c.w(0xd4)) - 1));
                    const float lap = static_cast<float>(g_track_length);
                    if (static_cast<float>(g_race_options.b0a) * left * lap <= static_cast<float>(c.w(0xe0))) {
                        pit = static_cast<float>(c.w(0xe2)) * left * lap > 2e7f;
                    } else {
                        const int32_t per_lap = static_cast<int32_t>(g_race_options.b0a) * g_track_length;
                        const int32_t tank = 0x10000 / per_lap;  // laps on a full tank
                        const float tf = static_cast<float>(tank);
                        if (guest::to_int(((left - 1.0f) - static_cast<float>(static_cast<int32_t>(c.w(0xe0)) / per_lap)) / tf) <
                            guest::to_int((left - 1.0f) / tf)) {
                            if (2e7f < (left - static_cast<float>(guest::to_int(left / tf) * tank)) * static_cast<float>(c.w(0xe2)) * lap)
                                pit = true;
                        } else if (c.w(0xe2) > 1000) {
                            pit = true;
                        } else {
                            const int stints = guest::to_int(left / tf + 1.0f);
                            if (guest::to_int(static_cast<float>(stints * static_cast<int32_t>(c.w(0xe0))) * rnd()) < per_lap)
                                pit = true;
                        }
                    }
                }
                if (pit) {
                    c.u(0x50) = 1;
                    route = 1;
                }
            }
        }
        // aim a little ahead on the line, offset sideways by +0x54
        float ahead = ((look / 5.0f) * speed + 1.0f) + t;
        if (ahead < 0.0f) ahead = 0.0f;
        const int n = g_race_line_count;
        int k = h + guest::to_int(ahead);
        const float frac = ahead - static_cast<float>(guest::to_int(ahead));
        if (n <= k) k = k - n;
        const int k1 = k + 1 < n ? k + 1 : 0;
        const Vec3 &q0 = line_point(k), &q1 = line_point(k1);
        const float ez = q1.z - q0.z, ex = q1.x - q0.x;
        float off = unsigned_to_float(target) * 0.05f - 5.0f;
        off = off / guest_sqrtf(ez * ez + ex * ex);
        const float ox = ez * off, oy = off * 0.0f, oz = -ex * off;
        const float yaw = ((W.x * B.x + W.y * B.y) + W.z * B.z) * look;
        const float s = guest_sinf(yaw), co = guest_cosf(yaw);
        const float reach = model_f(p, 0xb8);
        const float g = 1.0f - frac;
        const float tx = ((((q0.x * g + q1.x * frac) - P.x) + ox) - V.x * look) - (co * C.x - A.x * s) * reach;
        const float ty = ((((q0.y * g + q1.y * frac) - P.y) + oy) - V.y * look) - (C.y * co - A.y * s) * reach;
        const float tz = ((((q0.z * g + q1.z * frac) - P.z) + oz) - V.z * look) - (C.z * co - A.z * s) * reach;
        *steer = 0.0f <= (A.y * ty + A.x * tx) + A.z * tz ? -1.0f : 1.0f;
        // the throttle aims further ahead the faster the car goes
        int m = (static_cast<int>(c.u(0x50)) - guest::to_int((((speed * -0.016f) * speed) * look) * look)) + 1;
        if (n <= m) m = m - n;
        const Vec3 &r = line_point(m);
        dx = (ox + r.x) - P.x;
        dy = (r.y + oy) - P.y;
        dz = (r.z + oz) - P.z;
        *throttle = 1;
    } else if (route < 3) {
        // the pit lane, continuing on the racing line after its end
        const int i = static_cast<int>(c.u(0x50));
        const Vec3 &a = pit_point(i - 1), &b = pit_point(i);
        const float sx = b.x - a.x, sy = b.y - a.y, sz = b.z - a.z;
        const float power = c.f(0xcc) * 100.0f;
        const float look = (2.5f / guest_sqrtf(power)) * skill;
        float t = (((P.y - a.y) * sy + (P.x - a.x) * sx) + (P.z - a.z) * sz) / (((sy * sy + sx * sx) + sz * sz) + 1e-4f);
        if (0.0f < t) {
            traffic_offset(p, c, rnd() * 3.0f + 3.5f, look);
            c.u(0x50) = c.u(0x50) + 1;
        }
        float ahead = ((look / 5.0f) * speed + 1.0f) + t;
        if (ahead < 0.0f) ahead = 0.0f;
        const int n = g_pit_line_count;
        const int lines = g_race_line_count;
        int k = static_cast<int>(c.u(0x50)) + guest::to_int(ahead);
        const float frac = ahead - static_cast<float>(guest::to_int(ahead));
        const Vec3 *q0, *q1;
        if (k < n) {
            q0 = &pit_point(k - 1);
            q1 = &pit_point(k);
        } else {
            k = k + (g_pit_rejoin - n);
            if (lines <= k) k = k - lines;
            if (k < 1) {
                q1 = &line_point(lines - 1);
                q0 = &line_point(0);
            } else {
                q1 = &line_point(k);
                q0 = &line_point(k - 1);
            }
        }
        const float ex = q1->x - q0->x, ez = q1->z - q0->z;
        float off = unsigned_to_float(target) * 0.02f - 2.0f;
        off = off / guest_sqrtf(ez * ez + ex * ex);
        const float ox = ez * off, oy = off * 0.0f, oz = -ex * off;
        const float yaw = ((W.x * B.x + W.y * B.y) + W.z * B.z) * look;
        const float s = guest_sinf(yaw), co = guest_cosf(yaw);
        const float reach = model_f(p, 0xb8);
        const float rx = reach * (C.x * co - s * A.x), rz = reach * (C.z * co - A.z * s);
        const float g = 1.0f - frac;
        const float bx = g * q0->x + frac * q1->x;
        const float bz = q0->z * g + q1->z * frac;
        const float ty = ((((q0->y * g + q1->y * frac) - P.y) + oy) - V.y * look) - reach * (C.y * co - A.y * s);
        const float tx = (((bx - P.x) + ox) - V.x * look) - rx;
        const float tz = (((bz - P.z) + oz) - V.z * look) - rz;
        *steer = 0.0f <= (A.y * ty + A.x * tx) + A.z * tz ? -1.0f : 1.0f;
        int m = (static_cast<int>(c.u(0x50)) - guest::to_int((((speed * -0.016f) * speed) * look) * look)) + 1;
        if (m < n) {
            const Vec3 &r = pit_point(m);
            dz = (r.z + oz) - P.z;
            dx = (r.x + ox) - P.x;
            dy = (r.y + oy) - P.y;
            // look for a free pit box ahead along the lane
            int m2 = (static_cast<int>(c.u(0x50)) - guest::to_int(((speed * -0.1f) * speed) / power)) + 1;
            float ax, az, hx, hz;
            if (m2 < n) {
                const Vec3 &e = pit_point(m2), &e0 = pit_point(m2 - 1);
                az = e.z;
                ax = e.x;
                hx = ax - e0.x;
                hz = az - e0.z;
            } else {
                m2 = m2 + (g_pit_rejoin - n);
                int m3 = m2 - 1;
                if (lines <= m2) m2 = m2 - lines;
                if (lines <= m3) m3 = m3 - lines;
                const Vec3 &e = line_point(m2), &e0 = line_point(m3);
                az = e.z;
                ax = e.x;
                hz = az - e0.z;
                hx = ax - e0.x;
            }
            if (route == 1) {
                for (int b = 0; b < g_pit_count; b++) {
                    const Box bx2 = box(b);
                    const float qx = ax - bx2.pos().x, qz = az - bx2.pos().z;
                    if (!(qz * qz + qx * qx < 225.0f)) continue;
                    if (!(qz * hz + qx * hx > 0.0f)) continue;
                    if (bx2.owner() < 6 && bx2.owner() != p) continue;
                    if (g_pit_shared == 0 || p == b) route = static_cast<uint8_t>(b + 3);
                    if (g_pit_shared != 0) continue;
                    const float my = ((bx2.pos().y - P.y) * (bx2.pos().y - P.y) + (bx2.pos().x - P.x) * (bx2.pos().x - P.x)) +
                                     (bx2.pos().z - P.z) * (bx2.pos().z - P.z);
                    if (box_contested(p, bx2, my)) route = 1;
                    if (route != 1) break;
                }
            }
        } else {
            m = m + (g_pit_rejoin - n);
            if (lines <= m) m = m - lines;
            const Vec3 &r = line_point(m);
            dx = (r.x + ox) - P.x;
            dy = (r.y + oy) - P.y;
            dz = (r.z + oz) - P.z;
        }
        const uint8_t rejoin = g_pit_rejoin;
        *throttle = 1;
        if (static_cast<uint32_t>(n) <= c.u(0x50)) {
            c.u(0x50) = rejoin;
            route = 0;
        }
    } else {
        // driving into pit box route - 3: aim at its line, stop when slow enough or there
        const Box bx = box(route - 3);
        const Vec3 &D = bx.dir();
        const float rx = bx.pos().x - P.x, ry = bx.pos().y - P.y, rz = bx.pos().z - P.z;
        const float q = ((ry * D.y + rx * D.x) + rz * D.z) * 0.5f;
        dx = rx - q * D.x;
        dz = rz - D.z * q;
        dy = ry - D.y * q;
        *steer = 0.0f > (A.y * dy + A.x * dx) + A.z * dz ? 1.0f : -1.0f;
        const float dd = (dy * dy + dx * dx) + dz * dz;
        const float rr = (ry * ry + rx * rx) + rz * rz;
        const float s2 = speed * speed;
        if (s2 > ((guest_sqrtf(dd) * 2.0f) * c.f(0xcc)) * 100.0f || 4.0f > rr)
            *throttle = 0;
        else
            *throttle = 1;
        if (box_contested(p, bx, rr)) route = 1;
        if (bx.owner() < 6 && bx.owner() != p) route = 1;
        decide = *throttle != 0;
    }

    if (decide) {
        // throttle: brake for an aim behind, full throttle when rolling backwards, else from the turn
        // radius needed to reach the aim against the speed
        if (0.0f > (C.y * dy + C.x * dx) + C.z * dz) {
            *throttle = -100;
        } else if (0.0f > (C.y * V.y + C.x * V.x) + C.z * V.z) {
            *throttle = 100;
        } else {
            const float nx = B.z * V.y - B.y * V.z;
            const float nz = B.y * V.x - B.x * V.y;
            const float ny = B.x * V.z - B.z * V.x;
            const float k = (ny * dy + nx * dx) + nz * dz;
            float limit;
            if (1.0f > k && k > -1.0f) {
                limit = 1000.0f;
            } else {
                const float n2 = (ny * ny + nx * nx) + nz * nz;
                const float root = guest_sqrtf(n2);
                const float d2 = (dy * dy + dx * dx) + dz * dz;
                const float pw = k > 0.0f ? (c.f(0xcc) * 100.0f) * 0.5f : (c.f(0xcc) * -100.0f) * 0.5f;
                limit = guest_sqrtf(root * ((d2 * pw) / k));
            }
            if (c.s[2] == 2) {
                *throttle = limit > speed && 10.0f > fwd ? 100 : 1;
            } else if (limit <= speed) {
                *throttle = fwd < limit ? 1 : 0;
            } else {
                *throttle = 100;
            }
        }
    }
    if (3.0f <= speed || (0 <= *throttle && *throttle < 2))
        c.i(0xc) = 0;
    else
        c.i(0xc) = stuck + 1;
}
