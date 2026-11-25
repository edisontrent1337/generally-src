// Collisions with the track's objects: of a loose object (ball, 0x42f640) and of a car (0x420560).
//
// Every object type has a fixed shape built from planes, lines (edges) and spheres (corners) in the
// object's frame: h its heading, n the horizontal normal to it and s = h x n, which points up. A shape is
// only tested when the ball or car is within reach of the object's centre (horizontally); its parts are
// tested where the offset from the centre (along h, n and s) makes them the nearest feature. Both
// functions share the shapes; a car's collisions also have a material per part (friction, damping,
// bounce; drivable faces take the car's grip and give a surface code). The arithmetic follows the
// original's operation order exactly, including its multiplications by zero.
#include "game/objects.h"

#include "game/car_physics.h"
#include "game/fmath.h"
#include "game/race_state.h"

namespace {
float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// the vertical component as the original computes it (dot with (0, 1, 0))
float up_dot(const Vec3 &d) { return d.x * 0.0f + d.y + d.z * 0.0f; }

struct Material {
    float friction, damping, bounce;
    uint8_t grip = 0;     // when set: the friction is the car model's float at this offset
    uint8_t surface = 0;  // planes only
};
constexpr Material kSolid{1.0f, 1.5f, 1.0f};
constexpr Material kGrip{0.0f, 0.3f, 0.03f, 0x68};         // drivable faces
constexpr Material kDeck{0.0f, 0.3f, 0.03f, 0x68, 2};      // decks of types 17, 18
constexpr Material kSlope{0.0f, 0.3f, 0.03f, 0x88, 3};     // the bridge's ramp

// by type: edges and faces, corner spheres (cars), the ball's mass against the corners (loose objects)
struct TypeMaterial {
    Material body, ball;
    float mass;
};
constexpr Material kPole{1.5f, 1.0f, 1.0f}, kPoleTop{1.0f, 0.2f, 0.0f}, kPost{1.5f, 1.2f, 1.0f},
    kDead{1.0f, 0.1f, 0.01f}, kFirm{1.0f, 1.2f, 1.0f};
constexpr TypeMaterial kMaterials[37] = {
    {kSolid, kSolid, 1.0f},  // (unused)
    {kSolid, kSolid, 1.0f},
    {{1.0f, 0.05f, 0.05f}, {1.0f, 0.05f, 0.05f}, 1.0f},
    {kDead, kDead, 1.0f},
    {kPole, kPoleTop, 1.0f},
    {kPole, kPoleTop, 1.0f},
    {kPole, kPoleTop, 1.0f},
    {kPost, kPost, 1.5f},
    {kPost, kPost, 1.5f},
    {kSolid, {1.0f, 0.3f, 0.03f}, 1.0f},
    {kSolid, {1.0f, 0.03f, 0.03f}, 1.0f},
    {kSolid, kSolid, 1.0f},
    {kSolid, kSolid, 1.0f},
    {kSolid, kSolid, 1.0f},
    {kSolid, kSolid, 1.0f},
    {kSolid, kSolid, 1.0f},
    {kGrip, kGrip, 1.0f},
    {kSolid, kSolid, 1.0f},
    {kSolid, kSolid, 1.0f},
    {kPole, kPoleTop, 1.0f},
    {kPole, kPoleTop, 1.0f},
    {{1.5f, 0.2f, 0.2f}, {1.5f, 0.2f, 0.2f}, 1.5f},
    {kFirm, kFirm, 1.0f},
    {{1.5f, 0.5f, 0.5f}, {1.5f, 0.5f, 0.5f}, 1.5f},
    {{1.5f, 1.5f, 1.0f}, {1.5f, 1.5f, 1.0f}, 1.5f},
    {kDead, kDead, 1.0f},
    {kPost, kPost, 1.5f},
    {kSolid, kSolid, 1.0f},
    {kSolid, kSolid, 1.0f},
    {kSolid, kSolid, 1.0f},
    {kFirm, kFirm, 1.0f},
    {kSolid, kSolid, 1.0f},
    {kDead, kDead, 1.0f},
    {kDead, kDead, 1.0f},  // (no shape)
    {kDead, kDead, 1.0f},
    {kDead, kDead, 1.0f},
    {kDead, kDead, 1.0f},
};

// what both kinds of contact share: the object hit, the frame and the materials in use
struct ContactBase {
    TrackObject &o;      // the object that may be hit
    Vec3 d;              // ball or car - object
    Vec3 &last_heading;  // heading of the last object whose frame was set up, (0, 0, 0) before
    const TypeMaterial *m = nullptr;
    Vec3 h, n, s;
    float along = 0.0f, across = 0.0f, up = 0.0f;  // d along h, n and s

    void frame() {
        h = o.heading;
        last_heading = h;
        Vec3 c(h.z - h.y * 0.0f, h.x * 0.0f - h.z * 0.0f, h.y * 0.0f - h.x);  // (0, 1, 0) x h
        float len = guest_sqrtf(dot(c, c)) + 0.001f;
        n = Vec3(c.x / len, c.y / len, c.z / len);
        s = Vec3(n.z * h.y - n.y * h.z, n.x * h.z - n.z * h.x, n.y * h.x - n.x * h.y);
        along = dot(d, h);
        across = dot(d, n);
        up = dot(d, s);
    }
    bool reach2(float r) const { return r * r > d.x * d.x + d.z * d.z; }
};

struct BallContact : ContactBase {
    int i;  // the loose object

    bool within(float reach) const { return reach2(g_objects.get()[i].radius + reach); }
    void plane(const Vec3 &p, const Vec3 &normal, float thickness, const Material & = kSolid) const {
        object_collide_plane(i, p.x, p.y, p.z, normal.x, normal.y, normal.z, thickness);
    }
    void line(const Vec3 &p, const Vec3 &dir, float thickness, const Material & = kSolid) const {
        object_collide_line(i, p.x, p.y, p.z, dir.x, dir.y, dir.z, thickness);
    }
    Vec3 sphere(const Vec3 &p, float radius, const Material & = kSolid) const {
        Vec3 impulse;
        object_collide_sphere(&impulse, i, m->mass, p.x, p.y, p.z, radius);
        return impulse;
    }
    void rod_hit(const Vec3 &) const {}
    // type 36: another ball; both are pushed apart
    void ball(const Vec3 &p) const {
        if (1.7f > up_dot(d)) line(p, Vec3(0.0f, 1.0f, 0.0f), 0.7f);
        Vec3 impulse;
        const Vec3 q = p + Vec3(0.0f, 1.0f, 0.0f) * 0.4f;
        object_collide_sphere(&impulse, i, -o.mass, q.x, q.y, q.z, 0.8f);
        o.vel -= (1.0f / (o.mass * 100.0f)) * impulse;
        if (dot(o.vel, o.vel) > 0.0f) o.moving = 1;
    }
};

struct CarContact : ContactBase {
    int car;

    const uint8_t *model() const { return g_race_models[car]; }
    float friction(const Material &mt) const {
        return mt.grip ? *reinterpret_cast<const float *>(model() + mt.grip) : mt.friction;
    }
    bool within(float reach) const { return reach2(*reinterpret_cast<const float *>(model() + 0x18) + reach); }
    void plane(const Vec3 &p, const Vec3 &normal, float thickness) const { plane(p, normal, thickness, m->body); }
    void plane(const Vec3 &p, const Vec3 &normal, float thickness, const Material &mt) const {
        car_plane_collide(car, mt.surface, p.x, p.y, p.z, normal.x, normal.y, normal.z, thickness, friction(mt),
                          mt.damping, mt.bounce);
    }
    void line(const Vec3 &p, const Vec3 &dir, float thickness) const { line(p, dir, thickness, m->body); }
    void line(const Vec3 &p, const Vec3 &dir, float thickness, const Material &mt) const {
        car_cylinder_collide(car, p.x, p.y, p.z, dir.x, dir.y, dir.z, thickness, friction(mt), mt.damping, mt.bounce);
    }
    Vec3 sphere(const Vec3 &p, float radius) const { return sphere(p, radius, m->ball); }
    Vec3 sphere(const Vec3 &p, float radius, const Material &mt) const {
        Vec3 impulse;
        car_sphere_collide(&impulse, car, mt.bounce, p.x, p.y, p.z, radius, friction(mt), mt.damping);
        return impulse;
    }
    const Vec3 &velocity() const { return *reinterpret_cast<const Vec3 *>(g_car_states[car].raw + 0x64); }
    // type 32: a hard enough hit at speed throws sparks
    void rod_hit(const Vec3 &impulse) const {
        if (!(dot(impulse, impulse) > 1.0f)) return;
        const Vec3 v = velocity();
        const float speed = guest_sqrtf(dot(v, v));
        if (!(speed > 10.0f)) return;
        Particle &pt = particle(g_unknown_4a4794);
        pt.type = 8;
        pt.life = static_cast<uint8_t>(guest::to_int(speed * speed * 5.0f));
        pt.pos = o.pos + s * 1.4f;
        pt.vel = velocity();
        next_particle();
    }
    // type 36: a ball, pushed away by the car (two spheres)
    void ball(const Vec3 &p) const {
        if (1.7f > up_dot(d)) line(p, Vec3(0.0f, 1.0f, 0.0f), 0.7f);
        const Vec3 low = sphere(p + Vec3(0.0f, 1.0f, 0.0f) * 0.4f, 0.8f);
        o.vel -= low * (1.0f / (o.mass * 100.0f));
        const Vec3 high = sphere(p + Vec3(0.0f, 1.0f, 0.0f) * 1.7f, 0.5f);
        o.vel -= high * (1.0f / (o.mass * 100.0f));
        if (dot(o.vel, o.vel) > 0.0f) o.moving = 1;
    }
};

bool inside(float v, float half) { return v > -half && half > v; }

// a wall along h: half length `len`, its face planes `face` thick where |along| < `range` and the
// object is below `low` (along s); edges and corners `edge` thick; `wide` walls have two top edges
// (n * 0.4 apart) instead of one
template <class C> void wall(C &c, float range, float len, float face, float edge, float low, bool wide) {
    c.frame();
    const Vec3 &p = c.o.pos, &h = c.h, &n = c.n, &s = c.s;
    const Vec3 top = p + s * 0.8f;
    if (inside(c.along, range)) {
        if (low > c.up) c.plane(p, n, face);
        if (wide) {
            c.line(top + n * 0.4f, h, edge);
            c.line(top - n * 0.4f, h, edge);
        } else {
            c.line(top, h, edge);
        }
    }
    if (low > c.up) {
        if (wide) {
            c.line((p + h * len) + n * 0.4f, s, edge);
            c.line((p - h * len) + n * 0.4f, s, edge);
            c.line((p + h * len) - n * 0.4f, s, edge);
            c.line((p - h * len) - n * 0.4f, s, edge);
        } else {
            c.line(p + h * len, s, edge);
            c.line(p - h * len, s, edge);
        }
    }
    if (wide) {
        c.sphere((top + h * len) + n * 0.4f, edge);
        c.sphere((top - h * len) + n * 0.4f, edge);
        c.sphere((top + h * len) - n * 0.4f, edge);
        c.sphere((top - h * len) - n * 0.4f, edge);
    } else {
        c.sphere(top + h * len, edge);
        c.sphere(top - h * len, edge);
    }
}

// a vertical pole with a ball on top
template <class C> void pole(C &c, float height, float thickness, float ball) {
    const Vec3 &p = c.o.pos;
    if (height > up_dot(c.d)) c.line(p, Vec3(0.0f, 1.0f, 0.0f), thickness);
    c.sphere(p + Vec3(0.0f, height, 0.0f), ball);
}

// an upright box: footprint 2 half x 2 half (h, n), `height` tall, edges without thickness
template <class C> void box(C &c, float half, float height, bool top_faces) {
    c.frame();
    const Vec3 &p = c.o.pos, &h = c.h, &n = c.n, &s = c.s;
    if (!top_faces || height > c.up) {
        if (inside(c.across, half)) c.plane(p, h, half);
        if (inside(c.along, half)) c.plane(p, n, half);
        c.line((p + n * half) + h * half, s, 0.0f);
        c.line((p - n * half) + h * half, s, 0.0f);
        c.line((p + n * half) - h * half, s, 0.0f);
        c.line((p - n * half) - h * half, s, 0.0f);
    }
    if (!top_faces) return;
    const Vec3 top = p + s * height;
    if (inside(c.across, half)) {
        if (inside(c.along, half)) c.plane(p, s, height, kGrip);
        c.line(top + h * half, n, 0.0f, kGrip);
    } else if (inside(c.along, half)) {
        c.line(top + n * half, h, 0.0f, kGrip);
    }
    c.sphere((top + n * half) + h * half, 0.0f, kGrip);
    c.sphere((top - n * half) + h * half, 0.0f, kGrip);
    c.sphere((top + n * half) - h * half, 0.0f, kGrip);
    c.sphere((top - n * half) - h * half, 0.0f, kGrip);
}

// type 12: a ramp, rising towards +h
template <class C> void ramp(C &c) {
    c.frame();
    const Vec3 &p = c.o.pos, &h = c.h, &n = c.n, &s = c.s;
    const Vec3 crest = (p + s * 5.0f) - h;
    if (!(-4.0f > c.along)) {
        if (-1.0f > c.along) {
            const Vec3 v = s * 0.83205f - h * 0.5547f;
            if (inside(c.across, 5.0f)) c.plane(crest - v, v, 1.0f, kGrip);
            c.line(((p + s * 5.0f) + n * 5.0f) - h, h * 3.0f + s * 2.0f, 0.0f, kGrip);
            c.line(((p + s * 5.0f) - n * 5.0f) - h, h * 3.0f + s * 2.0f, 0.0f, kGrip);
            if (0.0f > (c.up - 5.0f) * 3.0f - (c.along + 1.0f) * 2.0f) c.plane(p, n, 5.0f);
        } else if (2.0f > c.along) {
            const Vec3 v = s * 0.83205f + h * 0.5547f;
            if (inside(c.across, 5.0f)) c.plane(crest - v, v, 1.0f, kGrip);
            c.line(((p + s * 5.0f) + n * 5.0f) - h, h * 3.0f - s * 2.0f, 0.0f, kGrip);
            c.line(((p + s * 5.0f) - n * 5.0f) - h, h * 3.0f - s * 2.0f, 0.0f, kGrip);
            if (0.0f > (c.up - 5.0f) * 3.0f + (c.along + 1.0f) * 2.0f) c.plane(p, n, 5.0f);
        }
    }
    if (2.5f > c.along && c.along > -1.0f) c.line(p + s * 2.5f, h, 1.5f, kGrip);
    c.sphere((p + h * 2.5f) + s * 2.5f, 1.5f, kGrip);
    if (inside(c.across, 5.0f)) {
        c.line((p + s * 3.0f) - h * 4.0f, n, 0.0f, kGrip);
        c.line((p + s * 3.0f) + h * 2.0f, n, 0.0f, kGrip);
    }
    if (3.0f > c.up) {
        if (inside(c.across, 5.0f)) c.plane(p - h, h, 3.0f);
        c.line((p + n * 5.0f) - h * 4.0f, s, 0.0f);
        c.line((p - n * 5.0f) - h * 4.0f, s, 0.0f);
        c.line((p + n * 5.0f) + h * 2.0f, s, 0.0f);
        c.line((p - n * 5.0f) + h * 2.0f, s, 0.0f);
        c.line((p + n * 1.3f) + h * 3.8f, s, 0.2f);
        c.line((p - n * 1.3f) + h * 3.8f, s, 0.2f);
    }
    const Vec3 low = p + s * 3.0f;
    c.sphere((low + n * 5.0f) - h * 4.0f, 0.0f, kGrip);
    c.sphere((low - n * 5.0f) - h * 4.0f, 0.0f, kGrip);
    c.sphere((low + n * 5.0f) + h * 2.0f, 0.0f, kGrip);
    c.sphere((low - n * 5.0f) + h * 2.0f, 0.0f, kGrip);
    c.sphere(((p + s * 5.0f) + n * 5.0f) - h, 0.0f, kGrip);
    c.sphere(((p + s * 5.0f) - n * 5.0f) - h, 0.0f, kGrip);
    c.sphere((low + n * 1.3f) + h * 3.8f, 0.2f);
    c.sphere((low - n * 1.3f) + h * 3.8f, 0.2f);
}

// type 14: a bridge over the track (deck at s 2 .. 6.5, pillars at n +-7.8)
template <class C> void bridge(C &c) {
    c.frame();
    const Vec3 &p = c.o.pos, &h = c.h, &n = c.n, &s = c.s;
    const Vec3 left = p + n * 7.8f, right = p - n * 7.8f;
    if (inside(c.across, 7.8f)) {
        if (2.0f > c.up) c.plane(p + h * 4.8f, h, 0.2f);
        if (6.5f > c.up) c.plane(p - h * 4.8f, h, 0.2f);
        c.line((p + h * 4.8f) + s * 2.0f, n, 0.2f);
        c.line((p - h * 4.8f) + s * 6.5f, n, 0.2f);
        if (inside(c.along, 4.8f)) c.plane((p + s * 1.0f) + h * 5.0f, s * 0.8944272f + h * 0.4472136f, 0.5f, kSlope);
    }
    if (inside(c.along, 4.8f)) {
        if (4.25f > c.along * 0.46875f + c.up) {
            c.plane(left, n, 0.2f);
            c.plane(right, n, 0.2f);
        }
        c.line(left + s * 4.25f, h * 2.0f - s, 0.2f);
        c.line(right + s * 4.25f, h * 2.0f - s, 0.2f);
    }
    if (2.0f > c.up) {
        c.line(left + h * 4.8f, s, 0.2f);
        c.line(right + h * 4.8f, s, 0.2f);
    }
    if (6.5f > c.up) {
        c.line(left - h * 4.8f, s, 0.2f);
        c.line(right - h * 4.8f, s, 0.2f);
    }
    c.sphere((left + h * 4.8f) + s * 2.0f, 0.2f);
    c.sphere((right + h * 4.8f) + s * 2.0f, 0.2f);
    c.sphere((left - h * 4.8f) + s * 6.5f, 0.2f);
    c.sphere((right - h * 4.8f) + s * 6.5f, 0.2f);
}

// type 17: a hump 40 long between two side walls (n +-6): flat top (s 2.4 .. 3.3) for |along| < 7,
// curved slopes to +-9, straight ramps to +-20
template <class C> void hump(C &c) {
    c.frame();
    const Vec3 &p = c.o.pos, &h = c.h, &n = c.n, &s = c.s;
    const Vec3 left = p - n * 6.0f, right = p + n * 6.0f;
    const float a = c.along;
    auto side_walls = [&] {
        c.plane(right, n, 0.4f);
        c.plane(left, n, 0.4f);
    };
    if (a > 20.0f) {
        c.sphere((left + h * 20.0f) - s * 0.45f, 0.4f);
        c.sphere((right + h * 20.0f) - s * 0.45f, 0.4f);
    } else if (a > 9.0f) {
        const Vec3 v = s * 0.95448f + h * 0.298275f;
        if (inside(c.across, 6.0f)) c.plane((p + h * 16.0f) - v * 0.25f, v, 0.25f, kDeck);
        if (0.8f > (a - 16.0f) * 0.3125f + c.up) side_walls();
        c.line((left + h * 16.0f) + s * 0.8f, s * 3.0f - h * 8.0f, 0.4f);
        c.line((right + h * 16.0f) + s * 0.8f, s * 3.0f - h * 8.0f, 0.4f);
    } else if (a > 7.0f) {
        const float k = a - 7.0f, bend = k * k * 0.078125f;
        const Vec3 w = ((s * 0.95448f + h * 0.298275f) * k + s * (9.0f - a)) * 0.5f;
        if (inside(c.across, 6.0f)) c.plane(((p + h * a) + s * (2.5f - bend)) - w * 0.25f, w, 0.25f, kDeck);
        const float top = 3.3f - bend;
        const Vec3 dir = ((h * 0.95448f - s * 0.298275f) * k + h * (9.0f - a)) * 0.5f;
        if (a > 8.0f) {
            if (top > c.up) side_walls();
        } else {
            bool below = true;
            if (top > c.up && c.up > 2.4f - bend) {
                side_walls();
                below = false;
            }
            if (below && 2.0f > c.up) {
                if (inside(c.across, 6.0f)) c.plane(p + h * 8.0f, h, 0.4f);
                c.line(left + h * 8.0f, s, 0.4f);
                c.line(right + h * 8.0f, s, 0.4f);
            }
            c.line((left + h * a) + s * (2.4f - bend), dir, 0.4f);
            c.line((right + h * a) + s * (2.4f - bend), dir, 0.4f);
        }
        c.line((left + h * a) + s * top, dir, 0.4f);
        c.line((right + h * a) + s * top, dir, 0.4f);
    } else if (a > -7.0f) {
        if (inside(c.across, 6.0f)) c.plane(p + s * 2.25f, s, 0.25f, kDeck);
        bool below = true;
        if (3.3f > c.up && c.up > 2.4f) {
            side_walls();
            below = false;
        }
        if (below && 2.0f > c.up) {
            if (inside(c.across, 6.0f)) {
                c.plane(p + h * 8.0f, h, 0.4f);
                c.plane(p - h * 8.0f, h, 0.4f);
            }
            c.line(left + h * 8.0f, s, 0.4f);
            c.line(right + h * 8.0f, s, 0.4f);
            c.line(left - h * 8.0f, s, 0.4f);
            c.line(right - h * 8.0f, s, 0.4f);
        }
        c.line(left + s * 3.3f, h, 0.4f);
        c.line(right + s * 3.3f, h, 0.4f);
        c.line(left + s * 2.4f, h, 0.4f);
        c.line(right + s * 2.4f, h, 0.4f);
    } else if (a > -9.0f) {
        const float k = -7.0f - a, bend = (a + 7.0f) * (a + 7.0f) * 0.078125f;
        const Vec3 w = ((s * 0.95448f - h * 0.298275f) * k + s * (a + 9.0f)) * 0.5f;
        if (inside(c.across, 6.0f)) c.plane(((p + h * a) + s * (2.5f - bend)) - w * 0.25f, w, 0.25f, kDeck);
        const float top = 3.3f - bend;
        const Vec3 dir = ((h * 0.95448f + s * 0.298275f) * k + h * (a + 9.0f)) * 0.5f;
        if (-8.0f > a) {
            if (top > c.up) side_walls();
        } else {
            bool below = true;
            if (top > c.up && c.up > 2.4f - bend) {
                side_walls();
                below = false;
            }
            if (below && 2.0f > c.up) {
                if (inside(c.across, 6.0f)) c.plane(p - h * 8.0f, h, 0.4f);
                c.line(left - h * 8.0f, s, 0.4f);
                c.line(right - h * 8.0f, s, 0.4f);
            }
            c.line((left + h * a) + s * (2.4f - bend), dir, 0.4f);
            c.line((right + h * a) + s * (2.4f - bend), dir, 0.4f);
        }
        c.line((left + h * a) + s * top, dir, 0.4f);
        c.line((right + h * a) + s * top, dir, 0.4f);
    } else if (a > -20.0f) {
        const Vec3 v = s * 0.95448f - h * 0.298275f;
        if (inside(c.across, 6.0f)) c.plane((p - h * 16.0f) - v * 0.25f, v, 0.25f, kDeck);
        if (0.8f > c.up - (a + 16.0f) * 0.3125f) side_walls();
        c.line((left - h * 16.0f) + s * 0.8f, s * 3.0f + h * 8.0f, 0.4f);
        c.line((right - h * 16.0f) + s * 0.8f, s * 3.0f + h * 8.0f, 0.4f);
    } else {
        c.sphere((left - h * 20.0f) - s * 0.45f, 0.4f);
        c.sphere((right - h * 20.0f) - s * 0.45f, 0.4f);
    }
}

// type 18: a flat strip 40 long between two side walls (n +-6) with ramps at the ends (|along| > 16)
template <class C> void strip(C &c) {
    c.frame();
    const Vec3 &p = c.o.pos, &h = c.h, &n = c.n, &s = c.s;
    const Vec3 left = p - n * 6.0f, right = p + n * 6.0f;
    const float a = c.along;
    auto side_walls = [&] {
        c.plane(right, n, 0.4f);
        c.plane(left, n, 0.4f);
    };
    auto deck = [&] {
        if (inside(c.across, 6.0f)) c.plane(p - s * 0.25f, s, 0.25f, kDeck);
    };
    auto flat = [&] {
        deck();
        if (0.8f > c.up) side_walls();
        c.line(left + s * 0.8f, h, 0.4f);
        c.line(right + s * 0.8f, h, 0.4f);
    };
    if (a > 20.0f) {
        c.sphere((left + h * 20.0f) - s * 0.45f, 0.4f);
        c.sphere((right + h * 20.0f) - s * 0.45f, 0.4f);
    } else if (a > 16.0f) {
        c.line((left + h * 16.0f) + s * 0.8f, s * 3.0f - h * 8.0f, 0.4f);
        c.line((right + h * 16.0f) + s * 0.8f, s * 3.0f - h * 8.0f, 0.4f);
        if (0.8f > (a - 16.0f) * 0.3125f + c.up) side_walls();
    } else if (a > 8.0f) {
        flat();
    } else if (a > -8.0f) {
        deck();
        bool below = true;
        if (0.8f > c.up && c.up > -0.1f) {
            side_walls();
            below = false;
        }
        if (below && -0.5f > c.up) {
            if (inside(c.across, 6.0f)) {
                c.plane(p + h * 8.0f, h, 0.4f);
                c.plane(p - h * 8.0f, h, 0.4f);
            }
            c.line(left + h * 8.0f, s, 0.4f);
            c.line(right + h * 8.0f, s, 0.4f);
            c.line(left - h * 8.0f, s, 0.4f);
            c.line(right - h * 8.0f, s, 0.4f);
        }
        c.line(left + s * 0.8f, h, 0.4f);
        c.line(right + s * 0.8f, h, 0.4f);
        c.line(left - s * 0.1f, h, 0.4f);
        c.line(right - s * 0.1f, h, 0.4f);
    } else if (a > -16.0f) {
        flat();
    } else if (a > -20.0f) {
        c.line((left - h * 16.0f) + s * 0.8f, s * 3.0f + h * 8.0f, 0.4f);
        c.line((right - h * 16.0f) + s * 0.8f, s * 3.0f + h * 8.0f, 0.4f);
        if (0.8f > c.up - (a + 16.0f) * 0.3125f) side_walls();
    } else {
        c.sphere((left - h * 20.0f) - s * 0.45f, 0.4f);
        c.sphere((right - h * 20.0f) - s * 0.45f, 0.4f);
    }
}

// type 30: a bridge 40 long, deck at s 5.7 .. 8.3 for |along| < 10, ramps down to the ends at +-20
template <class C> void arch(C &c) {
    c.frame();
    const Vec3 &p = c.o.pos, &h = c.h, &n = c.n, &s = c.s;
    const float a = c.along;
    const Vec3 near_end = p - h * 20.0f, far_end = p + h * 20.0f;
    if (8.5f > c.up) {
        c.line(p - h * 10.0f, s, 0.4f);
        c.line(p + h * 10.0f, s, 0.4f);
    }
    if (inside(a, 10.0f)) {
        if (8.3f > c.up && c.up > 5.7f) c.plane(p, n, 0.5f);
        if (0.3f > c.across && c.across > -0.3f) c.plane(p + s * 7.0f, s, 1.5f);
        c.line((p + s * 5.7f) + n * 0.3f, h, 0.2f);
        c.line((p + s * 8.3f) + n * 0.3f, h, 0.2f);
        c.line((p + s * 5.7f) - n * 0.3f, h, 0.2f);
        c.line((p + s * 8.3f) - n * 0.3f, h, 0.2f);
    }
    if (-10.0f > a && a > -20.0f) {
        const float k = (a + 20.0f) * 0.55f;
        if (k + 2.8f > c.up && static_cast<double>(c.up) > static_cast<double>(k) + 0.2) c.plane(p, n, 0.5f);
        if (0.3f > c.across && c.across > -0.3f) c.plane(near_end + s * 2.75f, s - h * 0.55f, 1.314f);
        const Vec3 dir = h + s * 0.55f;
        c.line((near_end + n * 0.3f) + s * 2.8f, dir, 0.2f);
        c.line((near_end - n * 0.3f) + s * 2.8f, dir, 0.2f);
        c.line((near_end + n * 0.3f) + s * 0.2f, dir, 0.2f);
        c.line((near_end - n * 0.3f) + s * 0.2f, dir, 0.2f);
    }
    if (20.0f > a && a > 10.0f) {
        const float k = (20.0f - a) * 0.55f;
        if (k + 2.8f > c.up && c.up > k + 0.2f) c.plane(p, n, 0.5f);
        if (0.3 > static_cast<double>(c.across) && static_cast<double>(c.across) > -0.3)
            c.plane(far_end + s * 2.75f, s + h * 0.55f, 1.314f);
        const Vec3 dir = h - s * 0.55f;
        c.line((far_end + n * 0.3f) + s * 2.8f, dir, 0.2f);
        c.line((far_end - n * 0.3f) + s * 2.8f, dir, 0.2f);
        // the original repeats the near end's lower edges here
        c.line((near_end + n * 0.3f) + s * 0.2f, h + s * 0.55f, 0.2f);
        c.line((near_end - n * 0.3f) + s * 0.2f, h + s * 0.55f, 0.2f);
    }
    if (3.0f > c.up) {
        c.line(near_end, s, 0.5f);
        c.line(far_end, s, 0.5f);
    }
}

template <class C> void collide(C &c, uint8_t type) {
    const Vec3 &p = c.o.pos;
    c.m = &kMaterials[type < 37 ? type : 0];
    switch (type) {
    case 1:
        if (c.within(3.0f)) wall(c, 2.6f, 2.6f, 0.4f, 0.4f, 0.8f, false);
        break;
    case 2:
        if (c.within(3.0f)) wall(c, 2.6f, 2.6f, 0.8f, 0.4f, 0.8f, true);
        break;
    case 3:
        if (c.within(2.0f)) wall(c, 1.6f, 1.6f, 0.8f, 0.4f, 0.8f, true);
        break;
    case 4:
        if (c.within(4.0f)) pole(c, 12.0f, 0.3f, 4.0f);
        break;
    case 5:
        if (c.within(4.0f)) pole(c, 10.0f, 0.3f, 4.0f);
        break;
    case 6:
        if (c.within(2.5f)) c.sphere(p + Vec3(0.0f, 0.5f, 0.0f), 3.0f);
        break;
    case 7:
    case 8:
        if (c.within(0.15f)) pole(c, 5.0f, 0.15f, 0.15f);
        break;
    case 9:
        if (c.within(1.5f)) c.sphere(p - Vec3(0.0f, 0.8f, 0.0f), 1.5f);
        break;
    case 10:
        if (c.within(0.7f)) c.sphere(p, 0.7f);
        break;
    case 11:
        if (c.within(2.25f)) box(c, 1.5f, 3.6f, true);
        break;
    case 12:
        if (c.within(6.5f)) ramp(c);
        break;
    case 13:
        if (c.within(8.5f)) box(c, 6.0f, 0.0f, false);
        break;
    case 14:
        if (c.within(9.5f)) bridge(c);
        break;
    case 15:
        if (c.within(5.5f)) {
            c.frame();
            if (c.along > -3.0f && 2.5f > c.along) c.line(p + c.s, c.h, 2.0f);
            c.sphere((p + c.h * 2.5f) + c.s, 2.0f);
            c.sphere((p - c.h * 3.0f) + c.s, 2.0f);
        }
        break;
    case 16:
        if (c.within(4.3f)) {
            c.frame();
            const Vec3 &h = c.h, &n = c.n;
            if (inside(c.along, 3.6f)) {
                if (inside(c.across, 1.1f)) c.plane(p, c.s, 0.4f);
                c.line(p + n * 1.1f, h, 0.4f);
                c.line(p - n * 1.1f, h, 0.4f);
            } else if (inside(c.across, 1.1f)) {
                c.line(p + h * 3.6f, n, 0.4f);
                c.line(p - h * 3.6f, n, 0.4f);
            }
            c.sphere((p + h * 3.6f) + n * 1.1f, 0.4f);
            c.sphere((p + h * 3.6f) - n * 1.1f, 0.4f);
            c.sphere((p - h * 3.6f) + n * 1.1f, 0.4f);
            c.sphere((p - h * 3.6f) - n * 1.1f, 0.4f);
        }
        break;
    case 17:
        if (c.within(21.0f)) hump(c);
        break;
    case 18:
        if (c.within(21.0f)) strip(c);
        break;
    case 19:
        if (c.within(5.0f)) {
            if (17.0f > up_dot(c.d)) c.line(p, Vec3(0.0f, 1.0f, 0.0f), 0.3f);
            c.sphere(p + Vec3(0.0f, 15.0f, 0.0f), 4.0f);
            c.sphere(p + Vec3(0.0f, 6.0f, 0.0f), 5.0f);
        }
        break;
    case 20:
        if (c.within(5.0f)) {
            if (15.0f > up_dot(c.d)) c.line(p, Vec3(0.0f, 1.0f, 0.0f), 0.3f);
            c.sphere(p + Vec3(0.0f, 12.0f, 0.0f), 5.0f);
        }
        break;
    case 21:
        if (c.within(1.7f)) {
            const float up = up_dot(c.d);
            if (7.0f > up) {
                const Vec3 vertical(0.0f, 1.0f, 0.0f);
                c.line(p, vertical, 0.3f);
                if (up > 3.5f && 5.5f > up) c.line(p + c.o.heading * 1.4f, vertical, 0.3f);
                if (up > 2.5f && 4.5f > up) c.line(p + c.o.heading * 1.4f, vertical, 0.3f);
                // the cross bars use whichever heading an earlier object left behind
                c.sphere((p + Vec3(0.0f, 3.5f, 0.0f)) + c.last_heading * 1.4f, 0.3f);
                c.sphere((p + Vec3(0.0f, 2.5f, 0.0f)) - c.last_heading * 1.4f, 0.3f);
            }
        }
        break;
    case 22:
        if (c.within(9.4f)) {
            c.frame();
            const Vec3 &h = c.h, &s = c.s;
            if (12.0f > c.up) {
                c.line(p - h * 9.0f, s, 0.4f);
                c.line(p + h * 9.0f, s, 0.4f);
            }
            c.sphere((p + h * 9.0f) + s * 12.0f, 0.4f);
            c.sphere((p - h * 9.0f) + s * 12.0f, 0.4f);
            if (inside(c.along, 9.0f)) {
                if (11.7f > c.up && c.up > 8.3f) c.plane(p, c.n, 0.2f);
                c.line(p + s * 11.7f, h, 0.2f);
                c.line(p + s * 8.3f, h, 0.2f);
            }
        }
        break;
    case 23:
        if (c.within(3.0f)) {
            c.frame();
            const Vec3 &h = c.h, &s = c.s;
            if (2.0f > c.up) {
                if (inside(c.along, 2.8f)) c.plane(p, c.n, 0.2f);
                c.line(p + h * 2.8f, s, 0.2f);
                c.line(p - h * 2.8f, s, 0.2f);
            }
            c.line(p + s * 2.0f, h, 0.2f);
            c.sphere((p + s * 2.0f) + h * 2.8f, 0.2f);
            c.sphere((p + s * 2.0f) - h * 2.8f, 0.2f);
        }
        break;
    case 24:
        if (c.within(0.7f)) pole(c, 1.7f, 0.7f, 0.7f);
        break;
    case 25:
        if (c.within(2.8f) && 1.7f > up_dot(c.d)) c.line(p, Vec3(0.0f, 1.0f, 0.0f), 0.7f);
        break;
    case 26:
        if (c.within(0.2f)) pole(c, 5.0f, 0.2f, 0.2f);
        break;
    case 27:
        if (c.within(8.5f)) box(c, 6.0f, 0.0f, false);
        break;
    case 28:
        if (c.within(8.5f)) box(c, 3.0f, 0.0f, false);
        break;
    case 29:
        if (c.within(3.0f)) wall(c, 3.0f, 2.6f, 0.2f, 0.2f, 0.8f, false);
        break;
    case 30:
        if (c.within(20.5f)) arch(c);
        break;
    case 31:
        if (c.within(3.0f)) {
            c.frame();
            const Vec3 &h = c.h, &s = c.s, top = p + s * 0.8f;
            if (inside(c.along, 2.6f)) {
                c.plane(p, c.n, 0.4f);
                c.line(top, h, 0.4f);
            }
            c.line(p + h * 2.6f, s, 0.4f);
            c.line(p - h * 2.6f, s, 0.4f);
            c.sphere(top + h * 2.6f, 0.4f);
            c.sphere(top - h * 2.6f, 0.4f);
        }
        break;
    case 32:
        if (c.within(1.5f)) {
            c.frame();
            if (1.5f > c.up) c.line(p, c.s, 0.5f);
            Vec3 impulse;
            impulse += c.sphere(p, 0.8f);
            impulse += c.sphere(p + c.s * 1.0f, 0.8f);
            impulse += c.sphere(p + c.s * 2.0f, 0.8f);
            c.rod_hit(impulse);
        }
        break;
    case 34:
        if (c.within(2.0f)) wall(c, 1.6f, 1.6f, 0.8f, 0.4f, 1.1f, true);
        break;
    case 35:
        if (c.within(1.0f)) wall(c, 0.8f, 1.6f, 0.8f, 0.4f, 1.1f, true);
        break;
    case 36:
        if (c.within(2.8f)) c.ball(p);
        break;
    }
}
}  // namespace

void object_collide_objects(int i) {
    Vec3 last_heading;
    for (int j = 6; j < g_object_count; j++) {
        if (j == i) continue;
        TrackObject &o = g_objects.get()[j];
        BallContact c{{o, g_objects.get()[i].pos - o.pos, last_heading, nullptr, {}, {}, {}}, i};
        collide(c, o.type);
    }
}

void car_collide_objects(int car) {
    Vec3 last_heading;
    for (int j = 6; j < g_object_count; j++) {
        TrackObject &o = g_objects.get()[j];
        const Vec3 &pos = *reinterpret_cast<const Vec3 *>(g_car_states[car].raw + 0x58);
        CarContact c{{o, pos - o.pos, last_heading, nullptr, {}, {}, {}}, car};
        collide(c, o.type);
    }
}
