#include "game/car_physics.h"

#include "game/fmath.h"
#include "game/race_options.h"
#include "game/race_state.h"

namespace {
// Fields of a car's state (g_car_states[i], 0x194 bytes) used here.
struct CarView {
    uint8_t *s;
    float &f(int off) const { return *reinterpret_cast<float *>(s + off); }
    Vec3 &v(int off) const { return *reinterpret_cast<Vec3 *>(s + off); }
    Vec3 &pos() const { return v(0x58); }      // centre
    Vec3 &vel() const { return v(0x64); }      // velocity
    Vec3 &spin() const { return v(0x70); }     // angular velocity
    Vec3 &axis(int k) const { return v(0x7c + 0xc * k); }  // orientation: right, up, forward
    float &inv_inertia() const { return f(0xc4); }
    float &mass() const { return f(0xc8); }
    float &grip() const { return f(0xd0); }
    uint16_t &damage() const { return *reinterpret_cast<uint16_t *>(s + 0xe2); }
};
}  // namespace

// The direction from the car's centre towards d (relative), in the ellipsoid's unit-sphere space and back:
// (R2 a2 + (R0 a0 + R1 a1)) with a_k = (d . R_k) / half_k; not normalised.
static Vec3 ellipsoid_direction(const CarView &c, const float *half, float dx, float dy, float dz) {
    const Vec3 &R0 = c.axis(0), &R1 = c.axis(1), &R2 = c.axis(2);
    float a2 = ((dx * R2.x + dy * R2.y) + dz * R2.z) / half[2];
    float n2x = R2.x * a2, n2y = R2.y * a2, n2z = R2.z * a2;
    float a1 = ((dx * R1.x + dy * R1.y) + dz * R1.z) / half[1];
    float a0 = ((dx * R0.x + dy * R0.y) + dz * R0.z) / half[0];
    return Vec3(n2x + (R0.x * a0 + a1 * R1.x), n2y + (R0.y * a0 + R1.y * a1), n2z + (R0.z * a0 + R1.z * a1));
}

// The response shared by the collision tests: n is the unit contact normal (from the obstacle to the
// car), q a point on the obstacle's centre (line), r its radius. The contact point is where the
// ellipsoid's surface normal is n; if it is inside the obstacle the car is pushed out (by depth *
// bounce) and gets an impulse there: the normal part grows with the penetration (spring `bounce`) and is
// damped by the closing speed; the tangential (friction) part is limited by sqrt(0.2 * normal) *
// max_friction. Hard hits are recorded in the impact list (g_unknown_4a4774: sparks and sounds) and add
// damage. out = the impulse applied (unchanged when there is none).
static void contact_response(const CarView &c, const float *half, float nx, float ny, float nz, float qx, float qy, float qz,
                             float radius, float bounce, float max_friction, float damping, Vec3 *out) {
    const Vec3 &R0 = c.axis(0), &R1 = c.axis(1), &R2 = c.axis(2);
    // the contact point on the ellipsoid, relative to the centre
    float b0 = ((R0.x * nx + R0.y * ny) + R0.z * nz) * half[0];
    float b1 = ((R1.x * nx + R1.y * ny) + R1.z * nz) * half[1];
    float b2 = ((R2.x * nx + R2.y * ny) + R2.z * nz) * half[2];
    float blen = guest_sqrtf((b1 * b1 + b0 * b0) + b2 * b2);
    float c2 = half[2] * b2, c0 = half[0] * b0, c1 = half[1] * b1;
    float ey = (R2.y * c2 + (R0.y * c0 + R1.y * c1)) / blen;
    float ez = (R2.z * c2 + (R0.z * c0 + R1.z * c1)) / blen;
    float ex = (R2.x * c2 + (R0.x * c0 + R1.x * c1)) / blen;

    // penetration along the normal and the contact point's velocity
    float tx = ((c.pos().x - ex) - (nx * radius + qx)) * nx;
    float py = (c.pos().y - ey) - (ny * radius + qy);
    float tz = ((c.pos().z - ez) - (nz * radius + qz)) * nz;
    const Vec3 &w = c.spin();
    float ux = c.vel().x + (w.y * ez - w.z * ey);
    float uy = c.vel().y + (w.z * ex - w.x * ez);
    float uz = c.vel().z + (w.x * ey - w.y * ex);
    float vn = (uy * ny + ux * nx) + uz * nz;
    float depth = (py * ny + tx) + tz;
    if (!(0.0f > depth)) return;
    float push = (bounce - 1.0f) * depth * bounce * 100.0f - vn * damping;
    if (!(push > 0.0f)) return;

    float h = ((R1.x * ex + R1.y * ey) + R1.z * ez) * 0.8f;  // the lever arm loses most of its up part
    depth = depth * bounce;
    ey = ey - R1.y * h;
    ex = ex - R1.x * h;
    ez = ez - R1.z * h;
    c.pos().x = c.pos().x - depth * nx;
    c.pos().y = c.pos().y - depth * ny;
    c.pos().z = c.pos().z - depth * nz;

    // friction: against the tangential velocity, limited
    float ox = vn * nx - ux, oy = vn * ny - uy, oz = vn * nz - uz;
    out->x = ox;
    out->y = oy;
    out->z = oz;
    float tlen = guest_sqrtf((ox * ox + oy * oy) + oz * oz);
    float normal = guest_sqrtf(push * 0.2f);
    float limit = normal * max_friction;
    if (tlen > limit) {
        float k = limit / tlen;
        out->x = ox * k;
        out->y = oy * k;
        out->z = oz * k;
    }
    float fx = out->x + push * nx;
    float fy = push * ny + out->y;
    float fz = out->z + push * nz;
    out->x = fx;
    out->y = fy;
    out->z = fz;

    // the impulse: split between linear and angular by the lever arm
    const float I = c.inv_inertia();
    float f2 = (fy * fy + fx * fx) + fz * fz;
    float proj = ((fx * ex + fy * ey) + fz * ez) / (f2 + 0.0001f);
    float pex = ex - fx * proj, pey = ey - fy * proj, pez = ez - fz * proj;
    float s = 1.0f / ((pey * pey + pex * pex + pez * pez) * I + 1.0f);
    float Is = I * s;
    c.spin().x = (fz * ey - fy * ez) * Is + c.spin().x;
    c.spin().y = (fx * ez - fz * ex) * Is + c.spin().y;
    c.spin().z = (fy * ex - fx * ey) * Is + c.spin().z;
    c.vel().x = fx * s + c.vel().x;
    c.vel().z = fz * s + c.vel().z;
    c.vel().y = fy * s + c.vel().y;
    float energy = s * s * f2;
    if (energy > 50.0f) {
        g_impact_energy = (energy - 50.0f) * c.mass() * 10000.0f + g_impact_energy;
        uint16_t idx = g_unknown_4a4794;
        uint8_t *rec = &guest::at<uint8_t>(g_unknown_4a4774 + 0x1cu * idx);
        rec[0] = 2;
        rec[1] = static_cast<uint8_t>(0x32 - static_cast<uint8_t>(guest::to_int(5000.0f / (energy + 50.0f))));
        Vec3 &at = *reinterpret_cast<Vec3 *>(rec + 4);
        at.x = c.pos().x - ex;
        at.y = c.pos().y - ey;
        at.z = c.pos().z - ez;
        *reinterpret_cast<Vec3 *>(rec + 0x10) = c.vel();
        g_unknown_4a4794 = static_cast<int>(idx) + 1 < static_cast<int>(g_race_options.w1e) ? static_cast<uint16_t>(idx + 1) : 0;
        float hit = static_cast<float>(g_race_options.b0b) * energy;
        if (hit > 5000.0f) {
            int sum = guest::to_int(hit) + c.damage();
            c.damage() = static_cast<uint16_t>(sum < 0xffff ? sum : 0xffff);
            c.s[3] = 0;
        }
    }
    // the upward part of the push lifts the car (summed per frame)
    float up = ((R1.y * ny + R1.x * nx) + R1.z * nz) * normal * s;
    if (up > 0.0f) {
        c.f(0xb4) = up + c.f(0xb4);
        c.f(0xb8) = ((R1.x * ex + R1.y * ey) + R1.z * ez) * up + c.f(0xb8);
    }
}

// Collision of car `car` (an ellipsoid with the half sizes of its model, +0x10) with a sphere at q
// (0x42d3d0); see contact_response. Returns out: the impulse applied (0 if none).
Vec3 *car_sphere_collide(Vec3 *out, int car, float bounce, float qx, float qy, float qz, float radius, float friction,
                         float damping) {
    CarView c{g_car_states[car].raw};
    const float *half = reinterpret_cast<const float *>(g_race_models[car] + 0x10);  // half sizes x, y, z
    out->x = 0;
    float max_friction = c.grip() * friction;
    float dx = c.pos().x - qx, dy = c.pos().y - qy, dz = c.pos().z - qz;
    out->y = 0;
    out->z = 0;
    float reach = half[2] + radius;
    if (!(reach * reach > (dy * dy + dx * dx) + dz * dz)) return out;
    Vec3 n = ellipsoid_direction(c, half, dx, dy, dz);
    float len = guest_sqrtf((n.y * n.y + n.x * n.x) + n.z * n.z) + 0.001f;
    contact_response(c, half, n.x / len, n.y / len, n.z / len, qx, qy, qz, radius, bounce, max_friction, damping, out);
    return out;
}

// Collision of car `car` with a cylinder of the given radius around the line through p along d
// (0x42de90; d need not be unit length); see contact_response.
void car_cylinder_collide(int car, float px, float py, float pz, float dx, float dy, float dz, float radius, float friction,
                          float damping, float bounce) {
    CarView c{g_car_states[car].raw};
    const float *half = reinterpret_cast<const float *>(g_race_models[car] + 0x10);
    float max_friction = c.grip() * friction;
    float dlen = guest_sqrtf((dy * dy + dx * dx) + dz * dz) + 0.001f;
    float ux = dx / dlen, uy = dy / dlen, uz = dz / dlen;
    // the centre relative to the nearest point of the line
    float rx = c.pos().x - px, ry = c.pos().y - py, rz = c.pos().z - pz;
    float t = (ry * uy + rx * ux) + rz * uz;
    rx = rx - t * ux;
    ry = ry - t * uy;
    rz = rz - t * uz;
    float reach = half[2] + radius;
    if (!(reach * reach > (ry * ry + rx * rx) + rz * rz)) return;
    Vec3 n = ellipsoid_direction(c, half, rx, ry, rz);
    float k = (n.y * uy + n.x * ux) + n.z * uz;  // without its part along the line
    n.x = n.x - k * ux;
    n.y = n.y - k * uy;
    n.z = n.z - k * uz;
    float len = guest_sqrtf((n.y * n.y + n.x * n.x) + n.z * n.z) + 0.001f;
    guest::Local<Vec3> impulse;
    contact_response(c, half, n.x / len, n.y / len, n.z / len, px, py, pz, radius, bounce, max_friction, damping, impulse.get());
}

// Collision of car `car` with the plane through p with normal n, thickened by t on both sides
// (0x42ea10; n need not face the car: it is flipped to). If the ellipsoid reaches into the slab, its
// deepest point is pushed out and gets an impulse as in contact_response (with the plane's own
// operation order). `surface` != 0 makes the plane a ground (1 ground, 2 sand, 3 water; 0 = a wall): the
// lift is spread over the four wheel corners (+0xa4..+0xb0) by where the contact is along the car, and
// wheels below the plane are put on it, recording the surface they touch (+0xe4..+0xe7).
void car_plane_collide(int car, uint8_t surface, float px, float py, float pz, float nx, float ny, float nz, float t, float friction,
                       float damping, float bounce) {
    CarView c{g_car_states[car].raw};
    const uint8_t *model = g_race_models[car];
    const float *half = reinterpret_cast<const float *>(model + 0x10);
    const Vec3 &R0 = c.axis(0), &R1 = c.axis(1), &R2 = c.axis(2);
    float max_friction = c.grip() * friction;
    float rx = c.pos().x - px, ry = c.pos().y - py, rz = c.pos().z - pz;
    float dist = (ny * ry + nx * rx) + nz * rz;
    if (0.0f > dist) {
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    float sz = half[2];
    if (dist > -t - sz && sz + t > dist) {
        // the ellipsoid's point deepest towards the plane (its normal is -n)
        float b0 = ((R0.y * ny + R0.x * nx) + R0.z * nz) * half[0];
        float b1 = ((R1.y * ny + R1.x * nx) + nz * R1.z) * half[1];
        float b2 = ((R2.y * ny + R2.x * nx) + R2.z * nz) * sz;
        float blen = guest_sqrtf((b1 * b1 + b0 * b0) + b2 * b2);
        float c2 = half[2] * (-b2 / blen), c0 = half[0] * (-b0 / blen), c1 = half[1] * (-b1 / blen);
        float ex = R2.x * c2 + (c0 * R0.x + R1.x * c1);
        float ey = R2.y * c2 + (R0.y * c0 + R1.y * c1);
        float ez = R2.z * c2 + (R0.z * c0 + R1.z * c1);
        float gx = (ex - nx * t) + rx, gy = (ey - ny * t) + ry, gz = (ez - nz * t) + rz;
        const Vec3 &w = c.spin();
        float ux = (w.z * ey - w.y * ez) + c.vel().x;
        float uy = c.vel().y + (w.x * ez - w.z * ex);
        float uz = c.vel().z + (w.y * ex - w.x * ey);
        float vn = -((uy * ny + ux * nx) + uz * nz);
        float depth = -((ny * gy + nx * gx) + nz * gz);
        float push = vn * damping - (bounce - 1.0f) * depth * bounce * 100.0f;
        if (depth > 0.0f && push > 0.0f) {
            float d = depth * bounce;
            c.pos().y = d * ny + c.pos().y;
            c.pos().x = d * nx + c.pos().x;
            c.pos().z = d * nz + c.pos().z;
            float oy = -ny * vn - uy, oz = -nz * vn - uz, ox = -nx * vn - ux;
            float tlen = guest_sqrtf((oy * oy + ox * ox) + oz * oz);
            float normal = guest_sqrtf(push * 0.2f);
            float limit = normal * max_friction;
            if (tlen > limit) {
                float k = limit / tlen;
                ox = ox * k;
                oy = oy * k;
                oz = oz * k;
            }
            float fy = push * ny + oy, fz = push * nz + oz, fx = push * nx + ox;
            float h = ((R1.y * ey + R1.x * ex) + ez * R1.z) * 0.8f;
            ey = ey - R1.y * h;
            ex = ex - R1.x * h;
            ez = ez - R1.z * h;
            float f2 = (fy * fy + fx * fx) + fz * fz;
            float proj = ((fy * ey + fx * ex) + fz * ez) / (f2 + 0.0001f);
            float pex = ex - proj * fx, pey = ey - proj * fy, pez = ez - proj * fz;
            const float I = c.inv_inertia();
            float s = 1.0f / (((pey * pey + pex * pex) + pez * pez) * I + 1.0f);
            float Is = I * s;
            c.spin().x = c.spin().x + (fy * ez - fz * ey) * Is;
            c.spin().y = (fz * ex - fx * ez) * Is + c.spin().y;
            c.spin().z = (fx * ey - fy * ex) * Is + c.spin().z;
            c.vel().z = s * fz + c.vel().z;
            c.vel().x = s * fx + c.vel().x;
            c.vel().y = s * fy + c.vel().y;
            float energy = f2 * s * s;
            if (energy > 50.0f) {
                g_impact_energy = (energy - 50.0f) * c.mass() * 10000.0f + g_impact_energy;
                uint16_t idx = g_unknown_4a4794;
                uint8_t *rec = &guest::at<uint8_t>(g_unknown_4a4774 + 0x1cu * idx);
                rec[0] = 2;
                rec[1] = static_cast<uint8_t>(0x32 - static_cast<uint8_t>(guest::to_int(5000.0f / (energy + 50.0f))));
                Vec3 &at = *reinterpret_cast<Vec3 *>(rec + 4);
                at.x = c.pos().x + ex;
                at.y = c.pos().y + ey;
                at.z = c.pos().z + ez;
                *reinterpret_cast<Vec3 *>(rec + 0x10) = c.vel();
                g_unknown_4a4794 = static_cast<int>(idx) + 1 < static_cast<int>(g_race_options.w1e) ? static_cast<uint16_t>(idx + 1) : 0;
                float hit = static_cast<float>(g_race_options.b0b) * energy;
                if (hit > 5000.0f) {
                    int sum = guest::to_int(hit) + c.damage();
                    c.damage() = static_cast<uint16_t>(sum < 0xffff ? sum : 0xffff);
                    c.s[3] = 0;
                }
            }
            float up = ((R1.y * ny + R1.x * nx) + R1.z * nz) * (s * normal);
            if (up > 0.0f) {
                if (surface != 0) {
                    auto along = [](float v) { return 0.0f > v ? 0.0f : (v > 1.0f ? 1.0f : v); };
                    float az = along((((R2.y * ey + R2.x * ex) + R2.z * ez) / half[2] + 1.0f) * 0.5f);
                    float ax = along((((R0.y * ey + R0.x * ex) + R0.z * ez) / half[0] + 1.0f) * 0.5f);
                    float front = az * up, back = (1.0f - az) * up;
                    c.f(0xa4) = front * ax + c.f(0xa4);
                    c.f(0xa8) = (1.0f - ax) * front + c.f(0xa8);
                    c.f(0xac) = back * ax + c.f(0xac);
                    c.f(0xb0) = back * (1.0f - ax) + c.f(0xb0);
                } else {
                    c.f(0xb4) = c.f(0xb4) + up;
                }
                c.f(0xb8) = c.f(0xb8) - ((R1.x * ex + R1.y * ey) + R1.z * ez) * up;
            }
        }
    }
    // wheels below the ground plane are put on it
    if (ny > 0.0f && surface != 0) {
        float A = nx * t + px, C = nz * t + pz, B = ny * t + py;
        Vec3 *wheel = reinterpret_cast<Vec3 *>(c.s + 0xe8) + model[0xbd];
        for (int k = 0; k < 4; k++) {
            float h = ((C - wheel[k].z) * nz + (A - wheel[k].x) * nx) / ny + B;
            if (h > wheel[k].y) {
                wheel[k].y = h;
                c.s[0xe4 + k] = surface;
            }
        }
    }
}

namespace {
// A car's contact point for the normal n (in world space, relative to the car's centre): the point of
// its ellipsoid (half sizes `half`) whose surface normal is n; sign -1 for the car on the far side.
Vec3 ellipsoid_contact(const CarView &c, const float *half, float sign, float nx, float ny, float nz) {
    const Vec3 &R0 = c.axis(0), &R1 = c.axis(1), &R2 = c.axis(2);
    float b0 = ((R0.x * nx + R0.y * ny) + R0.z * nz) * half[0];
    float b1 = ((R1.x * nx + R1.y * ny) + R1.z * nz) * half[1];
    float b2 = ((R2.x * nx + R2.y * ny) + R2.z * nz) * half[2];
    if (sign < 0.0f) b0 = -b0, b1 = -b1, b2 = -b2;
    float blen = guest_sqrtf((b1 * b1 + b0 * b0) + b2 * b2);
    float c2 = half[2] * b2, c1 = half[1] * b1, c0 = half[0] * b0;
    return Vec3(((R0.x * c0 + R1.x * c1) + R2.x * c2) / blen, ((R0.y * c0 + R1.y * c1) + R2.y * c2) / blen,
                ((R0.z * c0 + R1.z * c1) + R2.z * c2) / blen);
}

// A hard hit (energy above 50) of car c at its contact point e: the impact list entry, the crash
// sound energy and damage, as in contact_response.
void record_impact(const CarView &c, const Vec3 &e, float energy) {
    if (!(energy > 50.0f)) return;
    uint16_t idx = g_unknown_4a4794;
    uint8_t *rec = &guest::at<uint8_t>(g_unknown_4a4774 + 0x1cu * idx);
    rec[0] = 2;
    g_impact_energy = (energy - 50.0f) * c.mass() * 10000.0f + g_impact_energy;
    rec[1] = static_cast<uint8_t>(0x32 - static_cast<uint8_t>(guest::to_int(5000.0f / (energy + 50.0f))));
    Vec3 &at = *reinterpret_cast<Vec3 *>(rec + 4);
    at.x = c.pos().x - e.x;
    at.y = c.pos().y - e.y;
    at.z = c.pos().z - e.z;
    *reinterpret_cast<Vec3 *>(rec + 0x10) = c.vel();
    g_unknown_4a4794 = static_cast<int>(idx) + 1 < static_cast<int>(g_race_options.w1e) ? static_cast<uint16_t>(idx + 1) : 0;
    float hit = static_cast<float>(g_race_options.b0b) * energy;
    if (hit > 5000.0f) {
        int sum = guest::to_int(hit) + c.damage();
        c.damage() = static_cast<uint16_t>(sum < 0xffff ? sum : 0xffff);
        c.s[3] = 0;
    }
}
}  // namespace

// Collision of two cars a and b (0x41f3c0), both ellipsoids (half sizes from their models, +0x10).
// Only when their centres are closer than the sum of the two longest half sizes (+0x18): the normal n
// (towards b) is the sum of both ellipsoids' directions to the other centre; each car's contact point
// is where its surface normal is n (-n for a). If the gap along n is negative and the contact points
// approach, the cars are pushed apart (split by mass: each moves by the other's share), and both get an
// impulse at their contact point (lever arm with most of its up part removed): normal 5 - 1.5 vn,
// friction against the tangential relative velocity, limited to sqrt(-1.5 vn). Hard hits are recorded
// as in contact_response.
void car_car_collide(int a, int b) {
    CarView A{g_car_states[a].raw}, B{g_car_states[b].raw};
    const float *ha = reinterpret_cast<const float *>(&g_race_models[a][0x10]);
    const float *hb = reinterpret_cast<const float *>(&g_race_models[b][0x10]);
    float dx = B.pos().x - A.pos().x, dy = B.pos().y - A.pos().y, dz = B.pos().z - A.pos().z;
    float reach = hb[2] + ha[2];
    if (!(reach * reach > (dy * dy + dx * dx) + dz * dz)) return;

    // the normal: the directions from each centre to the other in its unit-sphere space
    auto dir = [&](const CarView &c, const float *half, Vec3 *n0, Vec3 *n1, Vec3 *n2) {
        const Vec3 &R0 = c.axis(0), &R1 = c.axis(1), &R2 = c.axis(2);
        float s2 = ((dx * R2.x + dy * R2.y) + dz * R2.z) / half[2];
        *n2 = Vec3(s2 * R2.x, R2.y * s2, R2.z * s2);
        float s1 = ((dx * R1.x + dy * R1.y) + dz * R1.z) / half[1];
        *n1 = Vec3(R1.x * s1, R1.y * s1, R1.z * s1);
        float s0 = ((dx * R0.x + dy * R0.y) + dz * R0.z) / half[0];
        *n0 = Vec3(s0 * R0.x, R0.y * s0, R0.z * s0);
    };
    Vec3 b0, b1, b2, a0, a1, a2;
    dir(B, hb, &b0, &b1, &b2);
    dir(A, ha, &a0, &a1, &a2);
    float Nx = b2.x + (b1.x + (b0.x + ((a0.x + a1.x) + a2.x)));
    float Ny = b2.y + (b1.y + (b0.y + ((a0.y + a1.y) + a2.y)));
    float Nz = b2.z + (b1.z + (b0.z + ((a0.z + a1.z) + a2.z)));
    float len = guest_sqrtf((Ny * Ny + Nx * Nx) + Nz * Nz) + 0.001f;
    const float nx = Nx / len, ny = Ny / len, nz = Nz / len;

    Vec3 ea = ellipsoid_contact(A, ha, -1.0f, nx, ny, nz);
    Vec3 eb = ellipsoid_contact(B, hb, 1.0f, nx, ny, nz);
    float gx = (B.pos().x - eb.x) - (A.pos().x - ea.x);
    float gy = (B.pos().y - eb.y) - (A.pos().y - ea.y);
    float gz = (B.pos().z - eb.z) - (A.pos().z - ea.z);
    const Vec3 &wa = A.spin(), &wb = B.spin();
    float ux = ((B.vel().x + (wb.y * eb.z - wb.z * eb.y)) - (wa.y * ea.z - wa.z * ea.y)) - A.vel().x;
    float uy = ((B.vel().y + (wb.z * eb.x - wb.x * eb.z)) - (wa.z * ea.x - wa.x * ea.z)) - A.vel().y;
    float uz = ((B.vel().z + (wb.x * eb.y - wb.y * eb.x)) - (wa.x * ea.y - wa.y * ea.x)) - A.vel().z;
    float depth = (gy * ny + gx * nx) + gz * nz;
    if (!(0.0f > depth)) return;
    float vn = (uy * ny + ux * nx) + uz * nz;
    if (!(0.0f > vn)) return;

    // lever arms lose most of their up part
    float h = ((A.axis(1).x * ea.x + A.axis(1).y * ea.y) + A.axis(1).z * ea.z) * 0.8f;
    ea = Vec3(ea.x - h * A.axis(1).x, ea.y - A.axis(1).y * h, ea.z - A.axis(1).z * h);
    h = ((B.axis(1).x * eb.x + B.axis(1).y * eb.y) + B.axis(1).z * eb.z) * 0.8f;
    eb = Vec3(eb.x - h * B.axis(1).x, eb.y - B.axis(1).y * h, eb.z - B.axis(1).z * h);

    // push apart
    float share_b = A.mass() / (B.mass() + A.mass()), share_a = 1.0f - share_b;
    float ka = share_a * depth;
    A.pos().x = A.pos().x + ka * nx;
    A.pos().y = A.pos().y + ka * ny;
    A.pos().z = A.pos().z + ka * nz;
    float kb = share_b * depth;
    B.pos().x = B.pos().x - kb * nx;
    B.pos().y = B.pos().y - kb * ny;
    B.pos().z = B.pos().z - kb * nz;

    // the impulse: friction against the tangential velocity (limited), plus the normal part
    float ox = vn * nx - ux, oy = vn * ny - uy, oz = vn * nz - uz;
    float s = vn * -1.5f;
    float tlen = guest_sqrtf((oy * oy + ox * ox) + oz * oz) + 0.001f;
    float limit = guest_sqrtf(s);
    float fx = ox, fy = oy, fz = oz;
    if (tlen > limit) {
        float k = limit / tlen;
        fx = k * ox;
        fy = k * oy;
        fz = k * oz;
    }
    float push = s + 5.0f;
    fy = fy + push * ny;
    fz = fz + push * nz;
    fx = fx + push * nx;
    float f2 = (fy * fy + fx * fx) + fz * fz;

    // car a takes -f at ea, car b +f at eb; each split between linear and angular by its lever arm
    float p = (ea.y * ny + ea.x * nx) + ea.z * nz;
    float qy = ea.y - p * ny, qx = ea.x - p * nx, qz = ea.z - p * nz;
    float sa = share_a / (((qy * qy + qx * qx) + qz * qz) * A.inv_inertia() + 1.0f);
    float Is = A.inv_inertia() * sa;
    A.spin().x = A.spin().x - Is * (fz * ea.y - fy * ea.z);
    A.spin().y = A.spin().y - Is * (fx * ea.z - fz * ea.x);
    A.spin().z = A.spin().z - Is * (fy * ea.x - fx * ea.y);
    A.vel().x = A.vel().x - fx * sa;
    A.vel().y = A.vel().y - fy * sa;
    A.vel().z = A.vel().z - fz * sa;
    record_impact(A, ea, f2 * sa * sa);

    p = (eb.y * ny + eb.x * nx) + eb.z * nz;
    qx = eb.x - p * nx, qy = eb.y - p * ny, qz = eb.z - p * nz;
    float sb = share_b / (((qy * qy + qx * qx) + qz * qz) * B.inv_inertia() + 1.0f);
    Is = B.inv_inertia() * sb;
    B.spin().x = Is * (fz * eb.y - fy * eb.z) + B.spin().x;
    B.spin().y = Is * (fx * eb.z - fz * eb.x) + B.spin().y;
    B.spin().z = Is * (fy * eb.x - fx * eb.y) + B.spin().z;
    B.vel().x = B.vel().x + fx * sb;
    B.vel().y = fy * sb + B.vel().y;
    B.vel().z = fz * sb + B.vel().z;
    record_impact(B, eb, f2 * sb * sb);
}
