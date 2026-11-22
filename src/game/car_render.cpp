// Drawing the cars, pit crews and ghost into the race view (0x40f7f0, g_draw_view when gr.ini
// TyreAutoSort is on): shadows first (the unshaded track image restored under each car, its wheels and
// the crew), then cars and crew members far to near, each car's triangles interleaved with its wheels by
// depth, and last the objects of type '$'.
//
// Several values carry over between the parts as in the original, where they share variables: the
// projected vertices (P, kept when a vertex is behind the camera), the wheels' screen positions and the
// "last screen position" used for discs behind the camera (which the shading and the shadow plane also
// write their normals into).
#include <cstdint>
#include <utility>

#include "game/fmath.h"
#include "game/ghost.h"
#include "game/menu.h"
#include "game/objects.h"
#include "game/race_options.h"
#include "game/race_state.h"
#include "game/render.h"
#include "game/track.h"

namespace {
struct Pt {
    float x, y, z;
};

struct CarRef {
    uint8_t *s;
    explicit CarRef(int i) : s(g_car_states[i].raw) {}
    Vec3 &v(int off) const { return *reinterpret_cast<Vec3 *>(s + off); }
    Vec3 &pos() const { return v(0x58); }
    Vec3 &right() const { return v(0x7c); }
    Vec3 &up() const { return v(0x88); }
    Vec3 &fwd() const { return v(0x94); }
    Vec3 &point(int i) const { return v(0xe8 + 12 * i); }  // collision points, then the wheels' contacts
    int32_t race_time() const { return *reinterpret_cast<int32_t *>(s + 0x10); }
};

const float *model_f(const uint8_t *m, int off) { return reinterpret_cast<const float *>(m + off); }
const Vec3 &model_v(const uint8_t *m, int off) { return *reinterpret_cast<const Vec3 *>(m + off); }

// The race view's projection of a point relative to the eye; screen x / y only in front of the camera.
struct View {
    const Vec3 &R = g_view_right, &D = g_view_down, &F = g_view_forward;
    float K = g_start_light_depth;
    // y, x, z order (the original's for most points)
    bool project(float x, float y, float z, Pt &p) const {
        p.z = y * F.y + x * F.x + z * F.z;
        return screen(x, y, z, p);
    }
    bool screen(float x, float y, float z, Pt &p) const {
        if (!(1e-4f < p.z)) return false;
        p.x = ((y * R.y + x * R.x + z * R.z) / p.z + 0.5f) * K;
        p.y = ((y * D.y + x * D.x + z * D.z) / p.z + g_screen_aspect * 0.5f) * K;
        return true;
    }
};

// Interpolation of a wheel towards the crew member carrying it (tyre change): timers 0x33..0x64 lift it
// off, 0x65..0xc8 carry it, 0xc9..0xfa put the new one on; 0xfb.. is done.
// Returns 0 (stays), 1 (at the crew member) or 2 (interpolated by *f).
int tyre_carry(uint8_t t, float &f) {
    if (t >= 0xfb) return 0;
    if (t >= 0xc9) {
        f = static_cast<float>(0xfa - t) * 0.02f;
        return 2;
    }
    if (t >= 0x65) return 1;
    if (t > 0x32) {
        f = static_cast<float>(t - 0x32) * 0.02f;
        return 2;
    }
    return 0;
}

int box_of_car(int p) {  // the last pit box whose state is car p, else g_pit_count
    int b = g_pit_count;
    for (int i = 0; i < g_pit_count; i++)
        if (g_pits.get()[i].state == p) b = i;
    return b;
}

struct Renderer {
    View view;
    Pt P[30] = {};       // projected vertices
    float shade[30] = {};
    float wheel[15] = {};  // the wheels' screen positions (x, y, z) x 5; the shadow plane uses 3..11
    int under[5] = {};     // per wheel: 1 under water, 2 above
    float last_x = 0.0f, last_y = 0.0f;
    int order[56] = {};
    float depth[57] = {};

    Pt &wheel_pt(int k) { return *reinterpret_cast<Pt *>(&wheel[3 * k]); }

    void sort_by_depth(int n);
    void ghost_shadow();
    void car_shadow(int p);
    void crew_shadows();
    void car(int p);
    void car_triangle(int p, const uint8_t *m, int t);
    void crew_member(int entry);
    void flag_objects();
};

// Cars and crew members (entries players + 8 per box) by distance along the view, far to near.
void Renderer::sort_by_depth(int n) {
    const int players = g_player_count;
    for (int i = 0; i < n; i++) {
        Vec3 p;
        if (i < players) {
            p = CarRef(i).pos();
        } else {
            const int j = i - players;
            p = g_pits.get()[j / 8].crew_pos[j % 8];
        }
        const float dz = p.z - g_view_eye.z;
        order[i] = i;
        const float dx = p.x - g_view_eye.x, dy = p.y - g_view_eye.y;
        depth[i + 1] = view.F.y * dy + view.F.x * dx + view.F.z * dz;
        for (int u = i; u > 0; u--) {
            const float d = depth[u + 1];
            if (d <= depth[u]) break;
            depth[u + 1] = depth[u];
            depth[u] = d;
            order[u] = order[u - 1];
            order[u - 1] = i;
            shade[0] = d;
        }
    }
}

// The ghost's last frame is undrawn: its triangles and wheels at the pose of the best lap.
void Renderer::ghost_shadow() {
    guest::Local<Vec3> a, b, c, d;
    if (!ghost_get(g_unknown_4a47f8 - CarRef(0).race_time(), a.get(), b.get(), c.get(), d.get())) return;
    const uint8_t *m = g_record_car[1];
    const Vec3 &e = g_view_eye;
    for (int i = 0; i < m[0xbc]; i++) {
        const Vec3 &v = model_v(m, 0x180 + 12 * i);
        const float x = (((a->x + v.x * b->x) + v.y * c->x) + v.z * d->x) - e.x;
        const float y = (((v.z * d->y + a->y) + v.x * b->y) + v.y * c->y) - e.y;
        const float z = (((v.z * d->z + a->z) + v.x * b->z) + v.y * c->z) - e.z;
        view.project(x, y, z, P[i]);
    }
    for (int t = 0; 3 * t < m[0xbe]; t++) {
        const Pt &u = P[m[0xc1 + 3 * t]], &v = P[m[0xc2 + 3 * t]], &w = P[m[0xc3 + 3 * t]];
        undraw_triangle(u.x, u.y, u.z, v.x, v.y, v.z, w.x, w.y, w.z);
    }
    for (int k = 0; k < 4; k++) {
        const Vec3 &v = model_v(m, 0x504 + 12 * k);
        const float x = (((a->x + b->x * v.x) + c->x * v.y) + d->x * v.z) - e.x;
        const float y = (((a->y + b->y * v.x) + c->y * v.y) + d->y * v.z) - e.y;
        const float z = (((a->z + b->z * v.x) + c->z * v.y) + v.z * d->z) - e.z;
        Pt p;
        p.z = y * view.F.y + x * view.F.x + z * view.F.z;
        if (view.screen(x, y, z, p)) {
            last_x = p.x;
            last_y = p.y;
        }
        undraw_disc(last_x, last_y, p.z, *model_f(m, 0x1c + 4 * k));
    }
}

// Car p's shadow: its collision points dropped onto the plane through its wheel contacts, the shadow
// triangles and the wheels (a wheel being changed follows the crew member carrying it, at ground level).
void Renderer::car_shadow(int p) {
    const CarRef c(p);
    const uint8_t *m = g_race_models[p];
    const int nw = m[0xbd];
    const Vec3 &W0 = c.point(nw), &W1 = c.point(nw + 1), &W2 = c.point(nw + 2), &W3 = c.point(nw + 3);
    const float e1x = W1.x - W0.x, e1y = W1.y - W0.y, e1z = W1.z - W0.z;
    const float e3x = W3.x - W0.x, e3y = W3.y - W0.y;
    const float e2z = W2.z - W0.z, e2y = W2.y - W0.y;
    const float e3z = W3.z - W0.z;
    const float e2x = W2.x - W0.x;
    wheel[3] = e1x, wheel[4] = e1y, wheel[5] = e1z;
    wheel[6] = e2x, wheel[7] = e2y, wheel[8] = e2z;
    wheel[9] = e3x, wheel[10] = e3y, wheel[11] = e3z;
    const float k = e2x * e3y - e2y * e3x;
    float nx = (e1z * e3y - e1y * e3z) + (e2y * e3z - e2z * e3y);
    float ny = (e1x * e3z - e1z * e3x) + (e2z * e3x - e2x * e3z);
    float nz = (e1y * e3x - e1x * e3y) + k;
    const float len = guest_sqrtf(nx * nx + ny * ny + nz * nz) + 1e-4f;
    nx = nx / len;
    ny = ny / len;
    nz = nz / len;
    last_x = nx;
    last_y = ny;
    const float cx = (((W0.x + W1.x) + W2.x) + W3.x) * 0.25f;
    const float cy = (((W0.y + W1.y) + W2.y) + W3.y) * 0.25f;
    const float cz = (((W0.z + W1.z) + W2.z) + W3.z) * 0.25f;
    const Vec3 &R = c.right(), &U = c.up(), &F = c.fwd(), &pos = c.pos();
    for (int i = 0; i < nw; i++) {
        const Vec3 &q = model_v(m, 0x450 + 12 * i);
        Vec3 &s = c.point(i);
        s.x = ((pos.x + R.x * q.x) + U.x * q.y) + F.x * q.z;
        s.y = ((pos.y + R.y * q.x) + U.y * q.y) + F.y * q.z;
        s.z = ((pos.z + R.z * q.x) + U.z * q.y) + F.z * q.z;
        s.y = ((cz - s.z) * nz + (cx - s.x) * nx) / ny + cy;
    }
    const int box = box_of_car(p);
    for (int i = 0; i < nw; i++) {
        const Vec3 &s = c.point(i);
        view.project(s.x - g_view_eye.x, s.y - g_view_eye.y, s.z - g_view_eye.z, P[i]);
    }
    for (int t = 0; t < m[0xbf]; t += 3) {
        const Pt &u = P[m[0x161 + t]], &v = P[m[0x162 + t]], &w = P[m[0x163 + t]];
        undraw_triangle(u.x, u.y, u.z, v.x, v.y, v.z, w.x, w.y, w.z);
    }
    for (int k = 0; k < 4; k++) {
        Vec3 &W = c.point(nw + k);
        if (box < g_pit_count) {
            const PitBox &b = g_pits.get()[box];
            const Vec3 &C = b.crew_pos[2 + k];
            float f;
            switch (tyre_carry(b.crew_timer[2 + k], f)) {
            case 1:
                W = C;
                break;
            case 2:
                W.x = W.x + (C.x - W.x) * f;
                W.z = (C.z - W.z) * f + W.z;
                W.y = W.y + (C.y - W.y) * f;
                break;
            default:
                break;
            }
            W.y = terrain_height(W);
        }
        const float x = W.x - g_view_eye.x, y = W.y - g_view_eye.y, z = W.z - g_view_eye.z;
        Pt &wp = wheel_pt(k);
        wp.z = x * view.F.x + y * view.F.y + z * view.F.z;
        if (1e-4f < wp.z) {
            wp.x = ((x * view.R.x + y * view.R.y + z * view.R.z) / wp.z + 0.5f) * view.K;
            wp.y = ((x * view.D.x + y * view.D.y + z * view.D.z) / wp.z + g_screen_aspect * 0.5f) * view.K;
        }
        undraw_disc(wp.x, wp.y, wp.z, *model_f(m, 0x1c + 4 * k));
    }
}

// The crew members' shadows: a disc on the ground under each.
void Renderer::crew_shadows() {
    for (int bi = 0; bi < g_pit_count; bi++) {
        for (int k = 0; k < 8; k++) {
            const Vec3 &q = g_pits.get()[bi].crew_pos[k];
            const float gy = terrain_height(q) - g_view_eye.y;
            const float dz = q.z - g_view_eye.z, dx = q.x - g_view_eye.x;
            const float z = view.F.y * gy + view.F.x * dx + view.F.z * dz;
            if (1e-4f < z) {
                last_x = ((view.R.y * gy + view.R.x * dx + view.R.z * dz) / z + 0.5f) * view.K;
                last_y = ((view.D.y * gy + view.D.x * dx + view.D.z * dz) / z + g_screen_aspect * 0.5f) * view.K;
            }
            undraw_disc(last_x, last_y, z, 0.5f);
        }
    }
}

void Renderer::car_triangle(int p, const uint8_t *m, int t) {
    const uint8_t a = m[0xc1 + t], b = m[0xc2 + t], c = m[0xc3 + t];
    const uint8_t colour = m[0x139 + t / 3];
    shaded_triangle(colour < 8 ? static_cast<uint8_t>(colour + (p + 0x18) * 8) : colour, false, P[a].x, P[a].y, P[a].z,
                    P[b].x, P[b].y, P[b].z, P[c].x, P[c].y, P[c].z, shade[a], shade[b], shade[c]);
}

// Car p: wheels (lifted by the jacks, carried by the tyre crew), vertices with their shading, then the
// triangles and wheels far to near.
void Renderer::car(int p) {
    const CarRef c(p);
    const uint8_t *m = g_race_models[p];
    const Vec3 &R = c.right(), &U = c.up(), &F = c.fwd();
    const float px = c.pos().x - g_view_eye.x, py = c.pos().y - g_view_eye.y, pz = c.pos().z - g_view_eye.z;
    const int box = box_of_car(p);
    for (int k = 0; k < 4; k++) {
        const Vec3 &w = model_v(m, 0x504 + 12 * k);
        float x = ((R.x * w.x + px) + U.x * w.y) + F.x * w.z;
        float z = ((R.z * w.x + pz) + U.z * w.y) + F.z * w.z;
        float y = ((R.y * w.x + py) + U.y * w.y) + F.y * w.z;
        if (box < g_pit_count) {
            const Vec3 &C = g_pits.get()[box].crew_pos[2 + k];
            float f;
            switch (tyre_carry(g_pits.get()[box].crew_timer[2 + k], f)) {
            case 1:
                x = C.x - g_view_eye.x;
                y = C.y - g_view_eye.y;
                z = C.z - g_view_eye.z;
                break;
            case 2: {
                const float ny = ((C.y - g_view_eye.y) - y) * f + y;
                const float nx = ((C.x - g_view_eye.x) - x) * f + x;
                const float nz = ((C.z - g_view_eye.z) - z) * f + z;
                x = nx, y = ny, z = nz;
                break;
            }
            default:
                break;
            }
        }
        const float ground = (c.point(m[0xbd] + k).y - g_view_eye.y) + *model_f(m, 0x1c + 4 * k);
        if (y < ground) y = ground;
        Pt &wp = wheel_pt(k);
        view.project(x, y, z, wp);
        under[k] = (g_water_level <= g_view_eye.y + y) + 1;
    }
    const bool fifth = m[0xc0] == 0;
    if (fifth) {
        const Vec3 &w = model_v(m, 0x534);
        const float x = ((w.x * R.x + px) + U.x * w.y) + w.z * F.x;
        const float y = ((R.y * w.x + py) + U.y * w.y) + F.y * w.z;
        const float z = ((R.z * w.x + pz) + U.z * w.y) + F.z * w.z;
        view.project(x, y, z, wheel_pt(4));
        under[4] = (g_water_level <= y + g_view_eye.y) + 1;
    }
    const int fifth_under = under[4];
    for (int i = 0; i < m[0xbc]; i++) {
        const Vec3 &v = model_v(m, 0x180 + 12 * i);
        const float z = ((R.z * v.x + pz) + U.z * v.y) + F.z * v.z;
        const float x = ((v.x * R.x + px) + U.x * v.y) + F.x * v.z;
        const float y = ((R.y * v.x + py) + U.y * v.y) + F.y * v.z;
        if (!view.project(x, y, z, P[i])) continue;
        if (g_water_level <= y + g_view_eye.y) {
            const Vec3 &n = model_v(m, 0x2e8 + 12 * i);
            const float ry = y - P[i].z;
            last_x = n.x * R.x + n.y * U.x + F.x * n.z;
            last_y = R.y * n.x + U.y * n.y + F.y * n.z;
            const float wz = R.z * n.x + U.z * n.y + F.z * n.z;
            const float len = guest_sqrtf(ry * ry + x * x + z * z);
            const float s = (last_y * ry + last_x * x + wz * z) / (len + 0.001f);
            shade[i] = 0.0f <= s ? 0.0f : s * s * 2.99f;
        } else {
            shade[i] = 3.99f;
        }
    }
    float tdepth[64];
    for (int t = 0; t < m[0xbe]; t += 3)
        tdepth[t / 3] = (P[m[0xc2 + t]].z + P[m[0xc1 + t]].z + P[m[0xc3 + t]].z) / 3.0f;
    // the wheels far to near
    uint8_t ord[5] = {0, 1, 2, 3, 4};
    const int count = fifth ? 5 : 4;
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            Pt &a = wheel_pt(i), &b = wheel_pt(j);
            if (a.z < b.z) {
                std::swap(ord[i], ord[j]);
                std::swap(a, b);
            }
        }
    }
    for (int i = 0; i < count; i++) {
        const int w = ord[i];
        const Pt &wp = wheel_pt(i);
        for (int t = 0; t < m[0xbe]; t += 3) {
            if (wp.z <= tdepth[t / 3]) {
                tdepth[t / 3] = -1.0f;
                car_triangle(p, m, t);
            }
        }
        uint8_t colour;
        float radius;
        if (w == 4) {
            colour = static_cast<uint8_t>((fifth_under == 1) * 2 - 0x3b + p * 8);
            radius = 0.3f;
        } else {
            colour = static_cast<uint8_t>((under[w] == 1) * 2 - 0x57);
            radius = *model_f(m, 0x1c + 4 * w);
        }
        draw_disc(colour, false, wp.x, wp.y, wp.z, radius);
    }
    for (int t = 0; t < m[0xbe]; t += 3)
        if (0.0f < tdepth[t / 3] || tdepth[t / 3] == 0.0f) car_triangle(p, m, t);
}

// A crew member: legs, body, chest and head as discs (the head in the car's colour for shared pits).
void Renderer::crew_member(int entry) {
    const int j = entry - g_player_count;
    const int bi = j / 8, k = j % 8;
    const PitBox &b = g_pits.get()[bi];
    const Vec3 &q = b.crew_pos[k];
    const float y0 = q.y - g_view_eye.y;
    const float x = (q.x - g_view_eye.x) + 0.0f, z = (q.z - g_view_eye.z) + 0.0f;
    static constexpr struct { float dy, radius; uint8_t colour; } kPart[3] = {{-0.3f, 0.5f, 0xa8}, {0.1f, 0.5f, 0xa9}, {0.5f, 0.4f, 0xaa}};
    for (const auto &part : kPart) {
        const float y = y0 + part.dy;
        Pt p;
        p.z = y * view.F.y + x * view.F.x + z * view.F.z;
        if (view.screen(x, y, z, p)) {
            last_x = p.x;
            last_y = p.y;
        }
        draw_disc(part.colour, false, last_x, last_y, p.z, part.radius);
    }
    const float y = y0 + 0.9f;
    Pt p;
    p.z = y * view.F.y + x * view.F.x + z * view.F.z;
    if (view.screen(x, y, z, p)) {
        last_x = p.x;
        last_y = p.y;
    }
    const uint8_t head = g_pit_shared == 1 && bi < g_player_count ? b.car : 0xae;
    draw_disc(head, false, last_x, last_y, p.z, 0.3f);
}

// Objects of type '$': set on the ground, pointing along the slope, drawn as a box with a pyramid of
// three stacked rings on top.
void Renderer::flag_objects() {
    for (int i = 6; i < g_object_count; i++) {
        TrackObject &o = g_objects.get()[i];
        if (o.type != '$') continue;
        Vec3 &h = o.heading;
        const float front = terrain_height(Vec3(h.x + o.pos.x, 0.0f, h.z + o.pos.z));
        const float back = terrain_height(Vec3(o.pos.x - h.x, 0.0f, o.pos.z - h.z));
        const float hx = h.x;
        h.y = (front - back) / 1.4f;
        const float hl = guest_sqrtf(hx * hx + h.y * h.y + h.z * h.z) + 0.001f;
        h.x = hx / hl;
        h.z = h.z / hl;
        h.y = h.y / hl;
        const float gy = (back + front) * 0.5f + 0.15f;
        o.pos.y = gy;
        // side s = h x up, then u = s x h
        float sz = h.y * 0.0f - h.x;
        float sx = h.z - h.y * 0.0f;
        float sy = h.x * 0.0f - h.z * 0.0f;
        const float sl = guest_sqrtf(sy * sy + sx * sx + sz * sz) + 0.001f;
        sx = sx / sl;
        sy = sy / sl;
        sz = sz / sl;
        const float uz = sy * h.x - sx * h.y;
        const float uy = sx * h.z - sz * h.x;
        const float ux = sz * h.y - sy * h.z;
        const Vec3 &e = g_view_eye;
        Pt V[13];
        V[0] = {(ux * 1.5f + o.pos.x) - e.x, (uy * 1.5f + gy) - e.y, (uz * 1.5f + o.pos.z) - e.z};
        // a ring at height u * up, half widths along the heading (bh) and the side (bs)
        auto ring = [&](Pt *r, float bh, float bs, float up) {
            const float ax = sx * bs, ay = sy * bs, az = sz * bs, hx2 = h.x * bh, hy2 = h.y * bh, hz2 = h.z * bh;
            const float ex = ux * up, ey = uy * up, ez = uz * up;
            r[0] = {(((o.pos.x - hx2) - ax) + ex) - e.x, (((gy - hy2) - ay) + ey) - e.y, (((o.pos.z - hz2) - az) + ez) - e.z};
            r[1] = {(((o.pos.x - hx2) + ax) + ex) - e.x, (((gy - hy2) + ay) + ey) - e.y, (((o.pos.z - hz2) + az) + ez) - e.z};
            r[2] = {(((hx2 + o.pos.x) + ax) + ex) - e.x, (((hy2 + gy) + ay) + ey) - e.y, (((hz2 + o.pos.z) + az) + ez) - e.z};
            r[3] = {(((hx2 + o.pos.x) - ax) + ex) - e.x, (((hy2 + gy) - ay) + ey) - e.y, (((hz2 + o.pos.z) - az) + ez) - e.z};
        };
        ring(&V[1], 0.1f, 0.1f, 1.0f);
        ring(&V[5], 0.22f, 0.22f, 0.4f);
        ring(&V[9], 0.3f, 0.3f, 0.15f);
        for (Pt &v : V) {
            const float x = v.x, y = v.y, z = v.z;
            v.z = x * view.F.x + y * view.F.y + view.F.z * z;
            if (1e-4f < v.z) {
                v.x = ((view.R.y * y + view.R.x * x + view.R.z * z) / v.z + 0.5f) * view.K;
                v.y = ((view.D.y * y + view.D.x * x + view.D.z * z) / v.z + g_screen_aspect * 0.5f) * view.K;
            }
        }
        box_draw(o.pos, Vec3(sx * 0.5f, sy * 0.5f, sz * 0.5f), Vec3(ux * 0.15f, uy * 0.15f, uz * 0.15f),
                 Vec3(h.x * 0.5f, h.y * 0.5f, h.z * 0.5f));
        static constexpr uint8_t kTri[20][4] = {
            {2, 0, 1, 0xb4}, {3, 0, 2, 0xb4}, {4, 0, 3, 0xb4}, {1, 0, 4, 0xb4},  {2, 1, 5, 0x69},  {6, 2, 5, 0x69},
            {3, 2, 6, 0x69}, {7, 3, 6, 0x69}, {4, 3, 7, 0x69}, {8, 4, 7, 0x69},  {1, 4, 8, 0x69},  {5, 1, 8, 0x69},
            {6, 5, 9, 0xb4}, {10, 6, 9, 0xb4}, {7, 6, 10, 0xb4}, {11, 7, 10, 0xb4}, {8, 7, 11, 0xb4}, {12, 8, 11, 0xb4},
            {5, 8, 12, 0xb4}, {9, 5, 12, 0xb4},
        };
        for (const auto &t : kTri) {
            const Pt &a = V[t[0]], &b = V[t[1]], &c = V[t[2]];
            shaded_triangle(t[3], false, a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, 0.0f, 0.0f, 0.0f);
        }
    }
}
}  // namespace

void race_view_draw_sorted() {
    Renderer r;
    const int n = g_player_count + g_pit_count * 8;
    r.sort_by_depth(n);
    if (g_game_state < 10 && g_player_count == 1 && g_race_options.b1a) r.ghost_shadow();
    if (g_race_options.b19) {
        for (int p = 0; p < g_player_count; p++) r.car_shadow(p);
        r.crew_shadows();
    }
    for (int i = 0; i < g_player_count + g_pit_count * 8; i++) {
        const int e = r.order[i];
        if (e < g_player_count) r.car(e);
        else r.crew_member(e);
    }
    r.flag_objects();
}
