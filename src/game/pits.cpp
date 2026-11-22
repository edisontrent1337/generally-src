// The pit boxes and their crews (0x41c760): a car stopping in a free box is claimed by it, the crew walks
// up and works (jacks, tyres, fuel, repairs) while the car stands still, and walks back to its places when
// the car leaves. Crew members are small physical bodies: they keep to the ground and are pushed away
// by cars (raising a dust puff and braking the car).
#include <cstdint>

#include "game/fmath.h"
#include "game/menu.h"
#include "game/race_options.h"
#include "game/race_state.h"
#include "game/text.h"
#include "game/track.h"
#include "game/video.h"
#include "bridge/imports.h"

namespace {
// Fields of a car (g_car_states, 0x194 bytes) used here. The index is a box's state, which can be any
// value: as in the original, indexes past the six cars read whatever follows the array.
struct Car {
    uint8_t *s;
    explicit Car(uint32_t i) : s(&guest::at<uint8_t>(0x4a5210u + i * 0x194u)) {}
    uint8_t place() const { return s[0]; }
    uint8_t &state() const { return s[2]; }    // 0 racing, 1 short of fuel ("F"), 2 finished
    uint8_t &dust() const { return s[8]; }     // countdown to the next dust puff
    Vec3 &pos() const { return v(0x58); }
    Vec3 &vel() const { return v(0x64); }
    Vec3 &ang() const { return v(0x70); }
    Vec3 &right() const { return v(0x7c); }
    Vec3 &up() const { return v(0x88); }
    Vec3 &fwd() const { return v(0x94); }
    float spin() const { return f(0xa0); }
    float inv_inertia() const { return f(0xc4); }
    float mass() const { return f(0xc8); }
    uint16_t laps() const { return w(0xd4); }
    uint16_t &tyres() const { return w(0xde); }  // 0xffff = new
    uint16_t &fuel() const { return w(0xe0); }
    uint16_t &damage() const { return w(0xe2); }
    Vec3 &v(int off) const { return *reinterpret_cast<Vec3 *>(s + off); }
    float &f(int off) const { return *reinterpret_cast<float *>(s + off); }
    uint16_t &w(int off) const { return *reinterpret_cast<uint16_t *>(s + off); }
};

// The car's model record (g_race_models, 0x540 bytes): half extents and the wheels.
struct Model {
    const uint8_t *m;
    explicit Model(uint32_t i) : m(&guest::at<uint8_t>(0x4a9050u + i * 0x540u)) {}
    float width() const { return f(0x10); }   // half width, height, length
    float height() const { return f(0x14); }
    float length() const { return f(0x18); }
    float wheel_x(int w) const { return f(0x504 + 12 * w); }
    float wheel_z(int w) const { return f(0x50c + 12 * w); }
    float f(int off) const { return *reinterpret_cast<const float *>(m + off); }
};

int column_x(int p) { return guest::to_int(static_cast<float>((p + 2) * g_screen_w) * 0.125f); }

// Player p's state box in the HUD (one font unit square left of the place): cleared, then a one-letter
// text in a colour.
void draw_state_box(int p, char letter, uint8_t colour) {
    const int u = g_font_size, x = u * 4 + column_x(p);
    const int top = g_screen_h - g_menu_border * 3 - u * 4, bottom = g_screen_h - g_menu_border * 3 - u * 2;
    for (int row = top; row < bottom; row++)
        guest::call_import(IMP_memset, g_pixels + static_cast<uint32_t>(g_pitch * row + x), 0xf1, u);
    guest::Local<char[2]> s;
    (*s)[0] = letter;
    (*s)[1] = 0;
    guest::Local<MsString> t;
    t->init();
    t->assign(*s, letter ? 1 : 0);
    draw_text_at(t.get(), u * 4 + column_x(p), g_screen_h - g_menu_border * 3 - u * 4, colour, 0);
    t->destroy();
}

// Crew member k's place beside car c while the car is not being serviced: 3 to the side, at the front, the
// middle or the back.
Vec3 beside_car(const Car &c, int k) {
    const Vec3 &P = c.pos(), &R = c.right(), &F = c.fwd();
    static constexpr struct { float right, fwd; } kPlace[6] = {{-3, 3}, {3, -3}, {3, 1.5f}, {-3, 1.5f}, {3, -1.5f}, {-3, -1.5f}};
    if (k >= 6) {
        const float r = k == 6 ? -3.0f : 3.0f;
        return Vec3(P.x + R.x * r, P.y + R.y * r, P.z + R.z * r);
    }
    const float r = kPlace[k].right, f = kPlace[k].fwd;
    return Vec3((P.x + R.x * r) + F.x * f, (P.y + R.y * r) + F.y * f, (P.z + R.z * r) + F.z * f);
}

// A car leaves box b: the crew walks back to the car's sides, the box counts down before it is free
// again. A car low on fuel that has laps to go is marked "F" in the HUD.
void car_leaves(PitBox &b, int p, const Car &c) {
    b.state = 100;
    for (int k = 0; k < 8; k++) b.crew_target[k] = beside_car(c, k);
    for (auto &t : b.crew_timer) t = 0;
    const uint32_t fuel_lap = static_cast<uint32_t>(g_race_options.b0a) * static_cast<uint32_t>(g_track_length);
    if (1 < g_race_laps - static_cast<int>(c.laps()) && g_leader_laps < g_race_laps &&
        static_cast<int>(c.fuel()) <
            guest::to_int(static_cast<float>(static_cast<double>(fuel_lap)) * 1.8f)) {
        c.state() = 1;
        draw_state_box(p, 'F', 0xf4);
    }
}

// Car p and the crew of box b: a crew member inside the car's ellipsoid is pushed out and slows the car.
void car_hits_crew(PitBox &b, int p, const Car &c, const Model &m, int touched[8]) {
    for (int k = 0; k < 8; k++) {
        Vec3 &cp = b.crew_pos[k], &cv = b.crew_vel[k];
        const float x = cp.x;
        float dy = cp.y - c.pos().y;
        float dx = x - c.pos().x;
        float dz = cp.z - c.pos().z;
        float d2 = dy * dy + dx * dx + dz * dz;
        if (m.length() * m.length() <= d2) continue;
        const Vec3 &R = c.right(), &U = c.up(), &F = c.fwd();
        const float a = (R.y * dy + R.x * dx + R.z * dz) / m.width();
        const float h = (U.y * dy + U.x * dx + U.z * dz) / m.height();
        const float l = (F.x * dx + dy * F.y + F.z * dz) / m.length();
        if (1.0f <= h * h + a * a + l * l) continue;
        d2 = d2 + 0.1f;
        const float gx = g_gravity.x * 0.05f;
        dy = (dy / d2) * 3.0f;
        dx = (dx / d2) * 3.0f;
        dz = (dz / d2) * 3.0f;
        const float py = ((c.vel().y - cv.y) * 0.1f + dy) - g_gravity.y * 0.05f;
        const float pz = ((c.vel().z - cv.z) * 0.1f + dz) - g_gravity.z * 0.05f;
        const float px = ((c.vel().x - cv.x) * 0.1f + dx) - gx;
        cp.x = x - g_gravity.x * 0.0005f;
        cp.y = cp.y - g_gravity.y * 0.0005f;
        cp.z = cp.z - g_gravity.z * 0.0005f;
        cv.z = pz + cv.z;
        cv.x = px + cv.x;
        cv.y = py + cv.y;
        const Vec3 &v = c.vel();
        const float speed = guest_sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (static_cast<float>(c.dust()) * speed <= 100.0f) {
            c.dust() = static_cast<uint8_t>(c.dust() + 1);
        } else {
            Particle &q = particle(g_unknown_4a4794);
            q.type = 8;
            q.life = static_cast<uint8_t>(guest::to_int(speed * 5.0f));
            q.pos = cp;
            q.vel = cv;
            c.dust() = 0;
            next_particle();
        }
        float brake = 1.0f - (400.0f / c.mass()) / (speed + 2.0f);
        if (brake < 0.0f) brake = 0.0f;
        c.vel().x = brake * c.vel().x;
        c.vel().y = brake * c.vel().y;
        c.vel().z = c.vel().z * brake;
        if (b.state == p) touched[k] = 1;
    }
}

// Where crew member k works on car c and the direction to it (from the crew member).
void work_target(PitBox &b, int k, const Car &c, const Model &m, Vec3 &dir) {
    const Vec3 &P = c.pos(), &R = c.right(), &F = c.fwd(), &cp = b.crew_pos[k];
    Vec3 &t = b.crew_target[k];
    if (k < 2) {  // jacks: in front of / behind the car
        const float l = k == 0 ? m.length() : -m.length();
        t = Vec3(P.x + F.x * l, P.y + F.y * l, P.z + F.z * l);
        dir = Vec3(P.x - cp.x, P.y - cp.y, P.z - cp.z);
    } else if (k < 6) {  // tyres: beside the wheel, outside
        const float wx = m.wheel_x(k - 2), wz = m.wheel_z(k - 2);
        const float pz = P.z + R.z * wx;
        const float x = P.x + wx * R.x + wz * F.x;
        const float y = P.y + R.y * wx + F.y * wz;
        const float z = pz + F.z * wz;
        if (k == 2 || k == 4)
            t = Vec3(x + R.x * 0.5f, y + R.y * 0.5f, z + R.z * 0.5f);
        else
            t = Vec3(x - R.x * 0.5f, y - R.y * 0.5f, z - R.z * 0.5f);
        dir = Vec3(x - cp.x, y - cp.y, z - cp.z);
    } else {  // fuel, repairs: beside the car
        const float w = k == 6 ? -m.width() : m.width();
        t = Vec3(P.x + w * R.x, P.y + R.y * w, P.z + R.z * w);
        dir = Vec3(P.x - cp.x, P.y - cp.y, P.z - cp.z);
    }
}

// A jack under the car: lifts it where it stands too low above the ground.
void jack(PitBox &b, int k, const Car &c, const Model &m) {
    const Vec3 &P = c.pos(), &U = c.up();
    const float ox = b.crew_pos[k].x - P.x, oy = b.crew_pos[k].y - P.y, oz = b.crew_pos[k].z - P.z;
    const float u = U.y * oy + U.x * ox + U.z * oz;
    const float qx = (P.x - u * U.x) + ox;
    const float qz = (P.z - U.z * u) + oz;
    const float qy = (P.y - U.y * u) + oy;
    const float lift = ((terrain_height(Vec3(qx, 0.0f, qz)) + m.height()) + 0.5f) - qy;
    if (0.0f < lift) {
        const float k2 = lift * 0.2f;
        c.vel().y = lift * 0.5f + c.vel().y;
        c.vel().x = c.vel().x * 0.9f;
        c.vel().y = c.vel().y * 0.9f;
        c.vel().z = c.vel().z * 0.9f;
        const float px = c.pos().x, pz = c.pos().z, ii = c.inv_inertia(), az = c.ang().z;
        c.ang().y = c.ang().y + ii * 0.0f;
        c.ang().x = ii * ((qz - pz) * k2) + c.ang().x;
        c.ang().z = az + ii * ((px - qx) * k2);
    }
    b.crew_timer[k] = b.crew_timer[2 + 2 * k] == 0xff && b.crew_timer[3 + 2 * k] == 0xff ? 0xff : 1;
}

// A tyre changed: the car's tyre state moves towards new by the share of this tyre among those left.
void tyre_done(PitBox &b, int k, const Car &c) {
    b.crew_timer[k] = 0xff;
    int left = 0;
    for (int j = 2; j < 6; j++)
        if (b.crew_timer[j] != 0xff) left++;
    c.tyres() = static_cast<uint16_t>((0xffff - c.tyres()) / (left + 1) + c.tyres());
}

// Crew member k at work on car c (arrived at its place, or walking up).
void work(PitBox &b, int k, const Car &c, const Model &m) {
    uint8_t &timer = b.crew_timer[k];
    const float tx = b.crew_target[k].x - b.crew_pos[k].x, tz = b.crew_target[k].z - b.crew_pos[k].z;
    if (0.1f <= tz * tz + tx * tx) {
        if (k < 2) timer = 0;
        return;
    }
    if (k < 2) {
        jack(b, k, c, m);
    } else if (k < 4) {
        if (timer < 0xfb) {
            if (b.crew_timer[0] == 1) timer = static_cast<uint8_t>(timer + 1);
        } else {
            tyre_done(b, k, c);
        }
    } else if (k < 6) {
        if (timer > 0xfa) tyre_done(b, k, c);
        else if (b.crew_timer[1] == 1) timer = static_cast<uint8_t>(timer + 1);
    } else if (k == 6) {
        if (c.fuel() < 0xff7f) {
            c.fuel() = static_cast<uint16_t>(c.fuel() + 0x80);
        } else {
            c.fuel() = 0xffff;
            timer = 0xff;
        }
    } else {
        if (timer < 0x32) {
            timer = static_cast<uint8_t>(timer + 1);
        } else {  // a spark, and one step of damage less
            Particle &q = particle(g_unknown_4a4794);
            *reinterpret_cast<uint16_t *>(&q) = 0x1402;
            q.pos = Vec3(b.crew_pos[k].x + b.facing_x[k], b.crew_pos[k].y + 0.0f, b.crew_pos[k].z + b.facing_z[k]);
            q.vel = Vec3();
            next_particle();
            c.damage() = static_cast<uint16_t>((c.damage() - 1) / 0x19c8 * 0x19c8);
            timer = 0;
        }
        if (c.damage() == 0) timer = 0xff;
    }
}

void box_step(PitBox &b, int bi) {
    int touched[8] = {};
    for (int p = 0; p < g_player_count; p++) {
        const Car c(static_cast<uint32_t>(p));
        const Model m(static_cast<uint32_t>(p));
        const float dx = c.pos().x - b.centre.x, dy = c.pos().y - b.centre.y, dz = c.pos().z - b.centre.z;
        const float d2 = dx * dx + dy * dy + dz * dz;
        const bool ours = !g_pit_shared || p == bi;
        if (100.0f <= d2) {
            if (b.state == p && ours) car_leaves(b, p, c);
            continue;
        }
        if (ours) {
            if (25.0f <= d2 || c.spin() <= -3.0f || 3.0f <= c.spin()) {
                if (b.state == p) car_leaves(b, p, c);
            } else if (b.state > 5) {  // stopped in a free box: claimed
                b.state = static_cast<uint8_t>(p);
                if (c.tyres() > 65000) {
                    for (int j = 2; j < 6; j++) b.crew_timer[j] = 0xff;
                }
                if (c.state() < 2) {
                    c.state() = 0;
                    draw_state_box(p, static_cast<char>(c.place() + '1'), 0xf5);
                }
            }
        }
        car_hits_crew(b, p, c, m, touched);
    }

    if (b.state == 7) {
        b.state = 6;
        pit_crew_home(b);
    } else if (b.state > 6) {
        b.state = static_cast<uint8_t>(b.state - 1);
    }
    const uint32_t ci = b.state;
    const Car c(ci);
    const Model m(ci);
    if (ci < 6 && c.damage() != 0 && b.crew_timer[7] == 0xff) b.crew_timer[7] = 0;
    for (int k = 0; k < 8; k++) {
        Vec3 &cp = b.crew_pos[k], &cv = b.crew_vel[k];
        // keep to the ground
        Vec3 ground(cp.x, cp.y - 0.7f, cp.z), normal;
        terrain_sample(&ground, &normal);
        const float gx = ground.x - cp.x;
        const float gy = (ground.y - cp.y) + 0.7f;
        const float gz = ground.z - cp.z;
        if (1e-4f < normal.y * gy + normal.x * gx + normal.z * gz) {
            touched[k] = 1;
            const float f = cv.y * normal.y + normal.x * cv.x + cv.z * normal.z;
            cv.x = cv.x - normal.x * f;
            cv.y = cv.y - normal.y * f;
            cv.z = cv.z - normal.z * f;
        }
        if (touched[k] == 1) {  // on the ground: walks
            Vec3 dir;
            cp.x = cp.x + gx;
            cp.y = cp.y + gy;
            cp.z = cp.z + gz;
            if (b.crew_timer[k] == 0xff) b.crew_target[k] = beside_car(c, k);
            else if (ci < 6) {
                work_target(b, k, c, m, dir);
            }
            const float h2 = dir.z * dir.z + dir.x * dir.x;
            if (1e-4f < h2) {
                const float len = guest_sqrtf(h2);
                dir.z = dir.z / len;
                b.facing_x[k] = dir.x / len;
                b.facing_z[k] = dir.z;
            }
            const float tz = b.crew_target[k].z - cp.z, tx = b.crew_target[k].x - cp.x;
            const float t2 = tz * tz + tx * tx;
            if (0.1f < t2) {
                const float s = 10.0f / guest_sqrtf(t2);
                cv.x = s * tx + cv.x;
                cv.y = cv.y + s * 0.0f;
                cv.z = cv.z + s * tz;
            }
            cv.x = cv.x * 0.5f;
            cv.y = cv.y * 0.5f;
            cv.z = cv.z * 0.5f;
        }
        if (ci < 6 && b.crew_timer[k] != 0xff) work(b, k, c, m);
        cv.x = cv.x * 0.98f;
        cv.y = cv.y * 0.98f;
        cv.z = cv.z * 0.98f;
        cp.x = cv.x / 100.0f + cp.x;
        cp.y = cp.y + cv.y / 100.0f;
        cp.z = cp.z + cv.z / 100.0f;
        cv.x = cv.x + g_gravity.x / 100.0f;
        cv.y = cv.y + g_gravity.y / 100.0f;
        cv.z = cv.z + g_gravity.z / 100.0f;
    }
}
}  // namespace

void pit_crew_home(PitBox &b) {
    const Vec3 &C = b.centre, &H = b.heading;
    const Vec3 side(H.z * 3.0f, 0.0f, -H.x * 3.0f);
    // along the heading, across it (in units of `side`)
    static constexpr struct { float along, across; } kHome[8] = {
        {2.0f, 0.0f}, {-3.0f, 1.0f}, {1.5f, 1.0f}, {1.5f, -1.0f}, {-1.5f, 1.0f}, {-1.5f, -1.0f}, {0.0f, -1.0f}, {0.0f, 1.0f},
    };
    for (int j = 0; j < 8; j++) {
        b.crew_target[j] = Vec3((C.x + H.x * kHome[j].along) + side.x * kHome[j].across,
                                (C.y + H.y * kHome[j].along) + side.y * kHome[j].across,
                                (C.z + H.z * kHome[j].along) + side.z * kHome[j].across);
        if (j == 0) {
            b.facing_x[j] = -H.x;
            b.facing_z[j] = -H.z;
        } else if (kHome[j].across > 0.0f) {
            b.facing_x[j] = -H.z;
            b.facing_z[j] = H.x;
        } else {
            b.facing_x[j] = H.z;
            b.facing_z[j] = -H.x;
        }
    }
}

void pit_crews_step() {
    for (int bi = 0; bi < g_pit_count; bi++) box_step(g_pits.get()[bi], bi);
}
