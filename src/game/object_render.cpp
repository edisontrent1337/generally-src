// The track's objects drawn into the static scene (0x44fa10): boxes (box_draw_pixels), triangles
// (triangle_for_each_pixel) and, for the shadows, triangles of restored track image (undraw_triangle).
// Most objects stand on the ground: the object's height is set to the terrain below it and the frame
// is upright (H its heading, G = E x H normalised, E = H x G). Some take the terrain's slope along
// their heading.
//
// The world points W and their screen positions P persist from object to object, as in the original:
// a point behind the camera keeps the screen position the slot had before.
#include "game/objects.h"

#include "game/fmath.h"
#include "game/menu.h"
#include "game/race_state.h"
#include "game/render.h"
#include "game/track.h"
#include "bridge/imports.h"

namespace {
int rand_int() { return static_cast<int>(guest::call_import(IMP_rand)); }

float next_random() {
    g_random_index = static_cast<uint8_t>(g_random_index + 1);
    return guest::at<float>(g_random_table + g_random_index * 4u);
}

// the original's colour table (four rows of five)
constexpr uint8_t kColours[4][5] = {
    {0x00, 0x2d, 0x3c, 0x6e, 0x69}, {0x4b, 0x4b, 0x4b, 0x3c, 0x3c}, {0x5a, 0x6e, 0x94, 0x1e, 0xf8},
    {0x00, 0xf1, 0x2d, 0x78, 0x9c}};

struct Draw {
    Vec3 W[16], P[16], W2[4], P2[4];
    Vec3 E, G, H;  // up, side, heading of the object being drawn
    TrackObject *o = nullptr;

    // sets the object on the ground and the upright frame
    void stand() {
        o->pos.y = terrain_height(o->pos);
        H = o->heading;
        frame();
    }
    void frame() {
        G = Vec3(H.z * E.y - E.z * H.y, E.z * H.x - H.z * E.x, H.y * E.x - H.x * E.y);
        const float len = guest_sqrtf(G.y * G.y + G.x * G.x + G.z * G.z) + 0.001f;
        G /= len;
        E = Vec3(G.z * H.y - G.y * H.z, G.x * H.z - G.z * H.x, G.y * H.x - G.x * H.y);
    }
    // the heading tilted to the terrain's slope over +-half along it (rise / divisor * factor; written
    // back to the object), the object at the mean height of the two ends
    void slope(float half, float divisor, float factor, bool floats = false) {
        const Vec3 &p = o->pos, &hd = o->heading;
        W[0] = p + hd * half;
        W[1] = p - hd * half;
        float h0 = terrain_height(W[0]), h1 = terrain_height(W[1]);
        if (floats) {  // on the water, not below its surface
            if (g_water_level > h0) h0 = g_water_level;
            if (g_water_level > h1) h1 = g_water_level;
        }
        o->heading.y = (h0 - h1) / divisor * factor;
        const Vec3 v = o->heading;
        const float len = guest_sqrtf(v.x * v.x + v.y * v.y + v.z * v.z) + 0.001f;
        o->heading = Vec3(v.x / len, v.y / len, v.z / len);
        o->pos.y = (h1 + h0) * 0.5f;
        H = o->heading;
        frame();
    }

    static void project(Vec3 &w, Vec3 &p) {
        const Vec3 &F = g_view_forward, &R = g_view_right, &D = g_view_down;
        p.z = w.x * F.x + w.y * F.y + w.z * F.z;
        if (p.z > 1e-4f) {
            p.x = ((w.x * R.x + w.y * R.y + w.z * R.z) / p.z + 0.5f) * g_start_light_depth;
            p.y = ((w.x * D.x + w.y * D.y + w.z * D.z) / p.z + g_screen_aspect * 0.5f) * g_start_light_depth;
        }
    }
    // world points relative to the eye, then all projected
    void place(int k, const Vec3 &v) { W[k] = v - g_view_eye; }
    void project_all(int n) {
        for (int k = 0; k < n; k++) project(W[k], P[k]);
    }
    // W[k] made relative to the eye and projected
    void view(int k) {
        W[k] = W[k] - g_view_eye;
        project(W[k], P[k]);
    }
    // W[k] dropped onto the terrain, made relative to the eye and projected
    void view_ground(int k) {
        W[k] = Vec3(W[k].x, terrain_height(W[k]), W[k].z) - g_view_eye;
        project(W[k], P[k]);
    }
    void ground(int to, int from) { W[to] = Vec3(W[from].x, terrain_height(W[from]), W[from].z); }

    void tri(uint8_t colour, int a, int b, int c) {
        const float sa[3] = {P[a].x, P[a].y, P[a].z}, sb[3] = {P[b].x, P[b].y, P[b].z}, sc[3] = {P[c].x, P[c].y, P[c].z};
        const float wa[3] = {W[a].x, W[a].y, W[a].z}, wb[3] = {W[b].x, W[b].y, W[b].z}, wc[3] = {W[c].x, W[c].y, W[c].z};
        triangle_for_each_pixel(colour, sa, sb, sc, wa, wb, wc);
    }
    void undraw(int a, int b, int c) {
        undraw_triangle(P[a].x, P[a].y, P[a].z, P[b].x, P[b].y, P[b].z, P[c].x, P[c].y, P[c].z);
    }
    void box(uint8_t colour, const Vec3 &c, const Vec3 &a, const Vec3 &b, const Vec3 &h) {
        box_draw_pixels(colour, c, a, b, h);
    }
    // the shadow under a footprint of half sizes along G and H: four ground corners, two triangles
    void shadow(int first, float g, float h) {
        const Vec3 &p = o->pos;
        W[first] = (p + G * g) + H * h;
        W[first + 1] = (p + G * g) - H * h;
        W[first + 2] = (p - G * g) + H * h;
        W[first + 3] = (p - G * g) - H * h;
    }

    // the footprint's corners (0..3) and their ground points (4..7), shadow drawn between the latter
    void ground_shadow(float g, float h) {
        shadow(0, g, h);
        for (int k = 0; k < 4; k++) ground(4 + k, k);
        for (int k = 0; k < 8; k++) view(k);
        undraw(4, 5, 6);
        undraw(5, 6, 7);
    }

    // the ramps' footprint: 10 points at G +-6 and H 15, 8, 0, -8, -15, shadowed as a strip
    void ramp_shadow() {
        const Vec3 &p = o->pos;
        const float along[5] = {15.0f, 8.0f, 0.0f, -8.0f, -15.0f};
        for (int k = 0; k < 5; k++) {
            if (k == 2) {
                W[4] = p + G * 6.0f;
                W[5] = p - G * 6.0f;
            } else if (along[k] > 0.0f) {
                W[2 * k] = (p + G * 6.0f) + H * along[k];
                W[2 * k + 1] = (p - G * 6.0f) + H * along[k];
            } else {
                W[2 * k] = (p + G * 6.0f) - H * -along[k];
                W[2 * k + 1] = (p - G * 6.0f) - H * -along[k];
            }
        }
        for (int k = 0; k < 10; k++) view_ground(k);
        for (int k = 0; k < 8; k++) undraw(k, k + 1, k + 2);
    }

    // a leaf of four points W[0..3]: their ground points, both faces and the shadow
    void frond(bool reversed = false) {
        for (int k = 0; k < 4; k++) ground(4 + k, k);
        for (int k = 0; k < 8; k++) view(k);
        tri(0x9c, 0, 1, 2);
        tri(0x9c, 1, 2, 3);
        if (reversed) {
            tri(0x9c, 2, 1, 0);
            tri(0x9c, 3, 2, 1);
        } else {
            tri(0x9c, 1, 0, 2);
            tri(0x9c, 2, 1, 3);
        }
        undraw(4, 5, 6);
        undraw(5, 6, 7);
    }

    // a building: a base down to y = 0, then floors 2.6 apart up to `height` above the ground, a roof
    void tower(float half, float height) {
        stand();
        const Vec3 &p = o->pos;
        const float base = p.y;
        box(0x90, Vec3(p.x, base * 0.5f, p.z), G * half, E * (base * 0.5f), H * half);
        float floor = base;
        if (base + height > floor) {
            do {
                box(0x90, Vec3(p.x, floor + 0.6f, p.z), G * half, E * 0.6f, H * half);
                box(0xb8, Vec3(p.x, floor + 1.9f, p.z), G * half, E * 0.7f, H * half);
                floor = floor + 2.6f;
            } while (base + height > floor);
        }
        box(0x90, Vec3(p.x, floor + 0.6f, p.z), G * half, E * 0.6f, H * half);
    }

    // the grandstand's spectators along a row r, from x while x < limit (random gaps); each a head, a body
    // and legs in random colours (one of 2 per player + 11)
    void crowd(float r, float x, float limit) {
        const Vec3 &p = o->pos;
        const float base = (((p.y + 1.0f) + r * 0.5f) * 0.5f) * 2.0f;
        const int colours = g_player_count * 2 + 11;
        auto colour = [&] {
            const int k = guest::to_int(next_random() * static_cast<float>(colours));
            return static_cast<uint8_t>(static_cast<uint8_t>(k << 2) - 0x6c);
        };
        auto at = [&](float along, float up) {
            const float k = along - r;
            return Vec3(p.x + H.x * k, (base + up) + H.y * k, p.z + H.z * k) + G * x;
        };
        while (limit > x) {
            box(colour(), at(5.3f, 0.95f), G * 0.15f, E * 0.15f, H * 0.15f);
            box(colour(), at(5.2f, 0.4f), G * 0.3f, E * 0.4f, H * 0.2f);
            box(colour(), at(5.4f, -0.2f), G * 0.5f, E * 0.3f, H * 0.4f);
            x = (next_random() * 0.8f + 0.8f) + x;
        }
    }
    // a box centred at height y above the row r's position along H, `side` along G off the middle
    void row_box(float r, float y, float side, const Vec3 &a, const Vec3 &b) {
        const Vec3 &p = o->pos;
        const float k = 5.0f - r;
        Vec3 c(p.x + H.x * k, y + H.y * k, p.z + H.z * k);
        if (side > 0.0f) c = c + G * side;
        if (side < 0.0f) c = c - G * -side;
        box(0x90, c, a, b, H * 0.5f);
    }

    // `count` random leaves around (height hc, radius rc) above the object, kept inside an ellipse (the
    // radius error counted twice) of squared size `limit`, each with its shadow
    void leaves(int count, float ha, float hb, float ra, float hc, float rc, float limit, float spread, uint8_t colour) {
        const Vec3 &p = o->pos;
        for (int n = 0; n < count; n++) {
            const float r1 = next_random(), r2 = next_random(), r3 = next_random();
            float h = r2 * ha + hb, rad = r3 * ra;
            const float q = (rad - rc) * 4.0f * (rad - rc) + (h - hc) * (h - hc);
            if (q > limit) {
                const float scale = guest_sqrtf(limit / q);
                h = (h - hc) * scale + hc;
                rad = (rad - rc) * scale + rc;
            }
            const Vec3 base = p + E * h;
            const float c = guest_cosf(r1 * 6.2832f) * rad, sn = guest_sinf(r1 * 6.2832f) * rad;
            W[0] = base + Vec3(c + 0.5f, next_random() * spread, sn);
            W[1] = base + Vec3(c - 0.5f, next_random() * spread, sn + 0.5f);
            W[2] = base + Vec3(c - 0.5f, next_random() * spread, sn - 0.5f);
            for (int k = 0; k < 3; k++) ground(3 + k, k);
            for (int k = 0; k < 6; k++) view(k);
            tri(colour, 1, 0, 2);
            tri(colour, 0, 1, 2);
            undraw(3, 4, 5);
        }
    }

    void draw(uint8_t type);
};

void Draw::draw(uint8_t type) {
    E = Vec3(0.0f, 1.0f, 0.0f);
    const Vec3 &p = o->pos;
    switch (type) {
    case 1:
    case 2:
    case 3:
    case 34:
    case 35: {  // walls and, 34 / 35, low walls with a (degenerate) shadow
        float width = 0.4f, height = 0.6f, len = 3.0f;
        uint8_t colour = 0x90;
        if (type == 2) {
            width = 0.8f;
            colour = 0x94;
        } else if (type == 3) {
            width = 0.8f;
            len = 2.0f;
            colour = 0x98;
        } else if (type == 34 || type == 35) {
            len = type == 34 ? 2.0f : 1.0f;
            width = 0.5f;
            height = 0.9f;
            colour = 0x9c;
        }
        slope(len, len, 0.5f);
        box(colour, p + E * height, G * width, E * height, H * len);
        if (type == 34 || type == 35) {
            W[0] = (p + G * 0.8f) + H * len;
            W[1] = (p - G * 0.8f) + H * len;
            W[2] = (p + G * 0.8f) + H * len;
            W[3] = (p - G * 0.8f) + H * len;
            for (int k = 0; k < 4; k++) view_ground(k);
            undraw(0, 1, 2);
            undraw(1, 2, 3);
        }
        break;
    }
    case 4: {  // a tree: a trunk and 15 leaves around the crown
        stand();
        box(0xa4, p + E * 5.0f, G * 0.3f, E * 6.0f, H * 0.3f);
        W[0] = p + E * 12.0f;
        ground(7, 0);
        view(0);
        view(7);
        float angle = 0.0f;
        for (int n = 0; n < 15; n++) {
            angle = (next_random() * 2.0f + 2.1415925f) / 15.0f + angle;
            const float ca = guest_cosf(angle), sa = guest_sinf(angle);
            const float tilt = next_random() * 2.4f - 1.3f;
            const float reach = guest_cosf(tilt) * 2.2f, rise = guest_sinf(tilt) * 2.2f;
            const Vec3 top = p + E * 12.0f, low = p + E * 10.0f;
            const Vec3 a((sa * 0.3f + ca) * reach, rise, (sa - ca * 0.3f) * reach);
            const Vec3 b((ca - sa * 0.3f) * reach, rise, (ca * 0.3f + sa) * reach);
            const Vec3 c(ca * 2.0f * reach, rise * 2.0f, sa * 2.0f * reach);
            W[1] = top + a;
            W[2] = top + b;
            W[3] = low + c;
            W[4] = top - a;
            W[5] = top - b;
            W[6] = low - c;
            for (int k = 1; k < 7; k++) ground(7 + k, k);
            for (int k = 1; k < 7; k++) view(k);
            for (int k = 8; k < 14; k++) view(k);
            tri(0x9c, 2, 0, 1);
            tri(0x9c, 1, 3, 2);
            tri(0x9c, 5, 0, 4);
            tri(0x9c, 4, 6, 5);
            tri(0x9c, 1, 0, 2);
            tri(0x9c, 2, 3, 1);
            tri(0x9c, 4, 0, 5);
            tri(0x9c, 5, 6, 4);
            undraw(7, 8, 9);
            undraw(8, 9, 10);
            undraw(7, 11, 12);
            undraw(11, 12, 13);
        }
        break;
    }
    case 5:  // a broadleaf tree
        stand();
        box(0xa4, p + E * 3.0f, G * 0.3f, E * 4.0f, H * 0.3f);
        leaves(400, 8.0f, 5.0f, 4.0f, 9.0f, 2.0f, 16.0f, 3.0f, 0xa0);
        break;
    case 6:  // a bush
        stand();
        leaves(200, 3.0f, -1.0f, 3.0f, 1.5f, 1.5f, 2.25f, 2.0f, 0x9c);
        break;
    case 7:
        stand();
        box(0xa8, p + E * 2.0f, G * 0.1f, E * 3.0f, H * 0.1f);
        place(0, ((p + E * 5.0f) + G * 1.0f) + H * 0.15f);
        place(1, ((p + E * 5.0f) - G * 1.0f) + H * 0.15f);
        place(2, (p + E * 3.0f) + H * 0.15f);
        place(3, ((p + E * 4.6f) + G * 0.6f) + H * 0.15f);
        place(4, ((p + E * 4.6f) - G * 0.6f) + H * 0.15f);
        place(5, (p + E * 3.4f) + H * 0.15f);
        project_all(6);
        tri(0xac, 4, 3, 5);
        tri(0x94, 1, 0, 2);
        tri(0xa8, 2, 0, 1);
        break;
    case 8:
        stand();
        shadow(0, 0.4f, 0.4f);
        for (int k = 0; k < 4; k++) view_ground(k);
        undraw(0, 1, 2);
        undraw(1, 2, 3);
        box(0xa8, p + E * 2.0f, G * 0.15f, E * 3.0f, H * 0.15f);
        box(0xb0, p + E * 5.3f, G * 0.4f, E * 0.3f, H * 0.4f);
        break;
    case 9: {  // a rock
        stand();
        place(0, p - H * 1.4f);
        place(1, (p + E * 0.5f) - H * 0.7f);
        place(2, (p + E * 0.55f) - H * 0.2f);
        place(3, (p + E * 0.45f) + H * 0.6f);
        place(4, p + H * 1.5f);
        place(5, (p - G * 1.2f) - H * 1.0f);
        place(6, (p - G * 0.7f) + E * 0.5f);
        place(7, (p - G * 1.2f) + H * 1.0f);
        place(8, (p + G * 1.1f) - H * 1.0f);
        place(9, (p + G * 0.6f) + E * 0.5f);
        place(10, (p + G * 1.3f) + H * 1.0f);
        project_all(11);
        static const uint8_t faces[14][3] = {{1, 5, 0}, {6, 5, 1}, {2, 6, 1}, {3, 6, 2}, {7, 6, 3}, {4, 7, 3}, {5, 6, 7},
                                             {0, 8, 1}, {1, 8, 9}, {1, 9, 2}, {2, 9, 3}, {3, 9, 10}, {3, 10, 4}, {10, 9, 8}};
        for (const auto &f : faces) tri(0x01, f[0], f[1], f[2]);
        break;
    }
    case 10: {  // a hump across the track, along its slope
        slope(0.7f, 1.4f, 1.0f);
        place(0, p - H * 0.7f);
        place(1, (p + E * 0.51f) - H * 0.57f);
        place(2, (p + E * 0.77f) - H * 0.22f);
        place(3, (p + E * 0.77f) + H * 0.22f);
        place(4, (p + E * 0.51f) + H * 0.57f);
        place(5, p + H * 0.7f);
        place(6, (p + G * 0.35f) - H * 0.4f);
        place(7, ((p + G * 0.35f) + E * 0.38f) - H * 0.28f);
        place(8, (p + G * 0.35f) + E * 0.5f);
        place(9, ((p + G * 0.35f) + E * 0.38f) + H * 0.28f);
        place(10, (p + G * 0.35f) + H * 0.4f);
        place(11, (p - G * 0.35f) - H * 0.4f);
        place(12, ((p - G * 0.35f) + E * 0.38f) - H * 0.28f);
        place(13, (p - G * 0.35f) + E * 0.5f);
        place(14, ((p - G * 0.35f) + E * 0.38f) + H * 0.28f);
        place(15, (p - G * 0.35f) + H * 0.4f);
        project_all(16);
        for (int k = 0; k < 5; k++) {  // the top strips of both sides
            tri(0x1c, k, 6 + k, k + 1);
            if (k < 4) tri(0x1c, k + 1, 6 + k, 7 + k);
        }
        for (int k = 0; k < 5; k++) {
            tri(0x1c, 11 + k, k, k + 1);
            if (k < 4) tri(0x1c, 11 + k, k + 1, 12 + k);
        }
        break;
    }
    case 11:
        stand();
        box(0x90, ((p + G * 1.0f) + E * 2.2f) - H * 1.0f, G * 0.5f, E * 1.2f, H * 0.5f);
        box(0x90, ((p - G * 1.0f) + E * 2.2f) - H * 1.0f, G * 0.5f, E * 1.2f, H * 0.5f);
        box(0xa8, (p + E * 2.0f) - H * 1.0f, G * 0.5f, E * 1.0f, H * 0.5f);
        box(0x90, (p + E * 3.2f) - H * 1.0f, G * 0.5f, E * 0.2f, H * 0.5f);
        box(0xb8, (p + E * 2.2f) + H * 0.5f, G * 1.5f, E * 1.2f, H * 1.0f);
        box(0xb4, p + E * 3.5f, G * 1.6f, E * 0.1f, H * 1.6f);
        {
            const float mid = (p.y + 1.0f) * 0.5f;
            box(0x90, Vec3(p.x, mid, p.z), G * 1.5f, E * mid, H * 1.5f);
        }
        break;
    case 12: {  // a house with a gable roof and front steps
        o->pos.y = terrain_height(o->pos) + 0.5f;
        H = o->heading;
        frame();
        place(0, ((p + H * 2.0f) + G * 5.0f) + E * 3.0f);
        place(1, ((p + H * 2.0f) - G * 5.0f) + E * 3.0f);
        place(2, ((p - H * 4.0f) + G * 5.0f) + E * 3.0f);
        place(3, ((p - H * 4.0f) - G * 5.0f) + E * 3.0f);
        place(4, ((p - H * 1.0f) + G * 5.0f) + E * 5.0f);
        place(5, ((p - H * 1.0f) - G * 5.0f) + E * 5.0f);
        place(6, ((p + H * 4.0f) - G * 1.5f) + E * 3.0f);
        place(7, ((p + H * 2.0f) - G * 1.5f) + E * 3.0f);
        place(8, ((p + H * 4.0f) + G * 1.5f) + E * 3.0f);
        place(9, ((p + H * 2.0f) + G * 1.5f) + E * 3.0f);
        place(10, (p + H * 4.0f) + E * 4.0f);
        place(11, p + E * 4.0f);
        project_all(12);
        static const uint8_t roof[11][4] = {{0xa8, 0, 1, 4}, {0xa8, 4, 1, 5},  {0xa8, 4, 3, 2},  {0xa8, 4, 5, 3},
                                            {0xbc, 2, 0, 4}, {0xbc, 1, 3, 5},  {0xa8, 6, 7, 10}, {0xa8, 10, 7, 11},
                                            {0xa8, 10, 9, 8}, {0xa8, 10, 11, 9}, {0xbc, 8, 6, 10}};
        for (const auto &f : roof) tri(f[0], f[1], f[2], f[3]);
        box(0xbc, (p + E * 2.7f) - H, G * 5.0f, E * 0.3f, H * 3.0f);
        box(0xb8, (p + E * 1.8f) + H * 0.5f, G * 5.01f, E * 0.6f, H * 0.6f);
        box(0xb8, (p + E * 1.8f) - H * 2.5f, G * 5.01f, E * 0.6f, H * 0.6f);
        box(0xb8, ((p + E * 1.8f) - H) + G * 2.8f, G * 0.6f, E * 0.6f, H * 3.01f);
        box(0xb8, ((p + E * 1.8f) - H) - G * 2.8f, G * 0.6f, E * 0.6f, H * 3.01f);
        box(0xa8, p + E * 1.1f, G * 0.5f, E * 1.1f, H * 2.1f);
        box(0xbc, (p + E * 1.2f) - H, G * 5.0f, E * 1.2f, H * 3.0f);
        box(0xbc, ((p + G * 1.3f) + E * 1.05f) + H * 3.8f, G * 0.2f, E * 1.95f, H * 0.2f);
        box(0xbc, ((p - G * 1.3f) + E * 1.05f) + H * 3.8f, G * 0.2f, E * 1.95f, H * 0.2f);
        float half = p.y * 0.5f;
        box(0x90, Vec3(p.x, half, p.z) - H * 1.0f, G * 5.0f, E * half, H * 3.0f);
        float step = 0.25f;
        while (true) {  // the steps, each 0.15 lower
            const float k = step + 2.0f;
            box(0x90, Vec3(p.x + H.x * k, half + H.y * k, p.z + H.z * k), G * 1.5f, E * half, H * 0.25f);
            step = step + 0.5f;
            if (!(2.0f > step)) break;
            half = half - 0.15f;
        }
        W[0] = (p + G * 1.5f) + H * 2.0f;
        W[1] = (p - G * 1.5f) + H * 2.0f;
        W[2] = (p + G * 1.5f) + H * 4.0f;
        W[3] = (p - G * 1.5f) + H * 4.0f;
        for (int k = 0; k < 4; k++) view_ground(k);
        undraw(0, 1, 2);
        undraw(1, 2, 3);
        break;
    }
    case 13:
        tower(6.0f, 20.0f);
        break;
    case 27:
        tower(6.0f, 10.0f);
        break;
    case 28:
        tower(3.0f, 15.0f);
        break;
    case 14: {  // a grandstand with spectators
        stand();
        W[0] = (p + G) - H * 2.0f;
        W[1] = (p - G) - H * 2.0f;
        W[2] = (p + G) - H * 5.0f;
        W[3] = (p - G) - H * 5.0f;
        for (int k = 0; k < 4; k++) view_ground(k);
        undraw(0, 1, 2);
        undraw(1, 2, 3);
        box(0xb4, (p + G * 7.8f) + E * 4.0f, G * 0.2f, E * 0.5f, H * 5.0f - E * 2.5f);
        box(0xb4, (p - G * 7.8f) + E * 4.0f, G * 0.2f, E * 0.5f, H * 5.0f - E * 2.5f);
        box(0xb4, (p + H * 4.8f) + E * 1.75f, G * 8.0f, E * 0.5f, H * 0.2f);
        box(0xb4, (p - H * 4.8f) + E * 6.25f, G * 8.0f, E * 0.5f, H * 0.2f);
        {
            const float half = (p.y + 1.25f) * 0.5f;
            box(0x90, Vec3(p.x + H.x * 4.5f, half + H.y * 4.5f, p.z + H.z * 4.5f), G * 8.0f, E * half, H * 0.5f);
        }
        float r = 1.5f;
        do {  // the front rows: full width
            const float half = ((p.y + 1.0f) + r * 0.5f) * 0.5f;
            row_box(r, half, 0.0f, G * 8.0f, E * half);
            crowd(r, next_random() * 0.8f - 7.3f, 7.5f);
            r = r + 1.0f;
        } while (3.0f > r);
        r = 3.5f;
        do {  // the middle rows: an aisle in the middle
            const float half = ((p.y + 1.0f) + r * 0.5f) * 0.5f;
            row_box(r, half, 4.5f, G * 3.5f, E * half);
            row_box(r, half, -4.5f, G * 3.5f, E * half);
            const float aisle = ((p.y + 3.5f) - r * 0.5f) * 0.5f;
            row_box(r, aisle, 0.0f, G * 1.0f, E * aisle);
            crowd(r, next_random() * 0.8f - 7.3f, -1.5f);
            crowd(r, next_random() * 0.8f + 1.7f, 7.5f);
            r = r + 1.0f;
        } while (7.0f > r);
        r = 7.3f;
        do {  // the top rows
            const float half = ((p.y + 1.0f) + r * 0.5f) * 0.5f;
            row_box(r, half, 4.5f, G * 3.5f, E * half);
            row_box(r, half, -4.5f, G * 3.5f, E * half);
            row_box(r, half * 2.0f - 0.5f, 0.0f, G * 1.0f, E * 0.5f);
            crowd(r, next_random() * 0.8f - 7.5f, 7.5f);
            r = r + 1.0f;
        } while (10.0f > r);
        break;
    }
    case 15: {  // a boat
        slope(4.0f, 8.0f, 1.0f, true);
        W[0] = (p + G * 0.5f) + H * 5.0f;
        W[1] = (p - G * 0.5f) + H * 5.0f;
        W[2] = (p + G * 2.0f) + H * 2.0f;
        W[3] = (p - G * 2.0f) + H * 2.0f;
        W[4] = (p + G * 2.0f) - H * 5.0f;
        W[5] = (p - G * 2.0f) - H * 5.0f;
        for (int k = 0; k < 6; k++) view_ground(k);
        for (int k = 0; k < 4; k++) undraw(k, k + 1, k + 2);
        place(0, ((p + H * 5.0f) + G * 0.5f) + E);
        place(1, ((p + H * 5.0f) - G * 0.5f) + E);
        place(2, ((p + H * 2.0f) + G * 2.0f) + E);
        place(3, ((p + H * 2.0f) - G * 2.0f) + E);
        place(4, ((p - H * 5.0f) + G * 2.0f) + E);
        place(5, ((p - H * 5.0f) - G * 2.0f) + E);
        place(6, ((p + H * 2.0f) + G * 1.0f) - E);
        place(7, ((p + H * 2.0f) - G * 1.0f) - E);
        place(8, ((p - H * 5.0f) + G * 1.0f) - E);
        place(9, ((p - H * 5.0f) - G * 1.0f) - E);
        project_all(10);
        static const uint8_t hull[14][3] = {{2, 0, 1}, {2, 1, 3}, {2, 3, 4}, {4, 3, 5}, {0, 6, 1}, {6, 7, 1}, {0, 2, 6},
                                            {3, 1, 7}, {2, 4, 6}, {5, 3, 7}, {6, 4, 8}, {5, 7, 9}, {5, 9, 4}, {4, 9, 8}};
        for (const auto &f : hull) tri(0xb0, f[0], f[1], f[2]);
        box(0xb0, (p + E * 1.3f) - H * 0.8f, G * 1.5f, E * 0.3f, H * 3.0f);
        box(0xb8, (p + E * 1.9f) - H * 1.3f, G * 1.5f, E * 0.3f, H * 2.5f);
        box(0xb8, (p + E * 1.9f) - H * 0.8f, G * 1.5f, E * 0.3f - H * 0.5f, H * 2.5f);
        box(0xb0, (p + E * 2.3f) - H * 1.3f, G * 1.5f, E * 0.1f, H * 2.5f);
        place(0, ((p + H * 0.5f) + G * 1.2f) + E * 2.4f);
        place(1, ((p + H * 0.5f) - G * 1.2f) + E * 2.4f);
        place(2, ((p - H * 3.5f) + G * 1.2f) + E * 2.4f);
        place(3, ((p - H * 3.5f) - G * 1.2f) + E * 2.4f);
        place(4, ((p - H * 0.5f) + G * 1.0f) + E * 3.0f);
        place(5, ((p - H * 0.5f) - G * 1.0f) + E * 3.0f);
        place(6, ((p - H * 1.5f) + G * 1.0f) + E * 3.0f);
        place(7, ((p - H * 1.5f) - G * 1.0f) + E * 3.0f);
        project_all(8);
        static const uint8_t cabin[12][3] = {{0, 1, 4}, {4, 1, 5}, {2, 0, 4}, {2, 4, 6}, {1, 3, 5}, {7, 3, 5},
                                             {1, 0, 4}, {1, 4, 5}, {0, 2, 4}, {4, 2, 6}, {3, 1, 5}, {3, 7, 5}};
        for (const auto &f : cabin) tri(0xb8, f[0], f[1], f[2]);
        break;
    }
    case 16: {  // a pontoon on the water
        o->pos.y = g_water_level;
        H = o->heading;
        frame();
        W[0] = ((p + G * 1.5f) + H * 4.0f) - E;
        W[1] = ((p - G * 1.5f) + H * 4.0f) - E;
        W[2] = (p + G * 1.5f) - E;
        W[3] = (p - G * 1.5f) - E;
        W[4] = ((p + G * 1.5f) - H * 4.0f) - E;
        W[5] = ((p - G * 1.5f) - H * 4.0f) - E;
        for (int k = 0; k < 6; k++) {  // the shadow's corners, not below the ground
            const float ground_y = terrain_height(W[k]);
            if (!(W[k].y <= ground_y)) W[k].y = ground_y;
            view(k);
        }
        for (int k = 0; k < 4; k++) undraw(k, k + 1, k + 2);
        box(0xa4, p, G * 1.5f, E * 0.4f, H * 4.0f);
        break;
    }
    case 17: {  // a jump: two ramps between side rails
        slope(16.0f, 32.0f, 1.0f);
        ramp_shadow();
        place(0, ((p + H * 8.0f) + G * 6.0f) + E * 2.5f);
        place(1, ((p + H * 8.0f) - G * 6.0f) + E * 2.5f);
        place(2, ((p - H * 8.0f) + G * 6.0f) + E * 2.5f);
        place(3, ((p - H * 8.0f) - G * 6.0f) + E * 2.5f);
        place(4, ((p + H * 20.0f) + G * 6.0f) - E * 1.25f);
        place(5, ((p + H * 20.0f) - G * 6.0f) - E * 1.25f);
        place(6, ((p - H * 20.0f) + G * 6.0f) - E * 1.25f);
        place(7, ((p - H * 20.0f) - G * 6.0f) - E * 1.25f);
        project_all(8);
        box(0x90, (p + G * 6.0f) + E * 3.1f, G * 0.4f, E * 0.6f, H * 8.0f);
        box(0x90, (p - G * 6.0f) + E * 3.1f, G * 0.4f, E * 0.6f, H * 8.0f);
        box(0x90, ((p + G * 6.0f) + E * 1.225f) + H * 14.0f, G * 0.4f, E * 0.6f, H * 6.0f - E * 1.875f);
        box(0x90, ((p - G * 6.0f) + E * 1.225f) + H * 14.0f, G * 0.4f, E * 0.6f, H * 6.0f - E * 1.875f);
        box(0x90, ((p + G * 6.0f) + E * 1.225f) - H * 14.0f, G * 0.4f, E * 0.6f, H * 6.0f + E * 1.875f);
        box(0x90, ((p - G * 6.0f) + E * 1.225f) - H * 14.0f, G * 0.4f, E * 0.6f, H * 6.0f + E * 1.875f);
        for (int k = 0; k < 4; k++) {  // posts down to y = 0 under the top's corners
            const Vec3 w = W[k] + g_view_eye;
            const float half = w.y / E.y * 0.5f;
            box(0x90, w - E * half, G * 0.4f, E * half, H * 0.4f);
        }
        tri(0x01, 2, 0, 1);
        tri(0x01, 1, 3, 2);
        tri(0x01, 0, 4, 5);
        tri(0x01, 5, 1, 0);
        tri(0x01, 6, 2, 3);
        tri(0x01, 3, 7, 6);
        break;
    }
    case 18: {  // a flat platform between side rails
        slope(16.0f, 32.0f, 1.0f);
        o->pos.y = o->pos.y + 0.25f;
        ramp_shadow();
        place(0, (p + H * 16.0f) + G * 6.0f);
        place(1, (p + H * 16.0f) - G * 6.0f);
        place(2, (p - H * 16.0f) + G * 6.0f);
        place(3, (p - H * 16.0f) - G * 6.0f);
        project_all(4);
        box(0x90, (p + G * 6.0f) + E * 0.6f, G * 0.4f, E * 0.6f, H * 16.0f);
        box(0x90, (p - G * 6.0f) + E * 0.6f, G * 0.4f, E * 0.6f, H * 16.0f);
        box(0x90, ((p + G * 6.0f) - E * 0.025f) + H * 18.0f, G * 0.4f, E * 0.6f, H * 2.0f - E * 0.625f);
        box(0x90, ((p - G * 6.0f) - E * 0.025f) + H * 18.0f, G * 0.4f, E * 0.6f, H * 2.0f - E * 0.625f);
        box(0x90, ((p + G * 6.0f) - E * 0.025f) - H * 18.0f, G * 0.4f, E * 0.6f, H * 2.0f + E * 0.625f);
        box(0x90, ((p - G * 6.0f) - E * 0.025f) - H * 18.0f, G * 0.4f, E * 0.6f, H * 2.0f + E * 0.625f);
        for (int k = 0; k < 4; k++) {  // posts down to y = 0
            W[4] = k < 2 ? (p + H * 8.0f) : (p - H * 8.0f);
            W[4] = k % 2 == 0 ? W[4] + G * 6.0f : W[4] - G * 6.0f;
            const float half = W[4].y * 0.5f / E.y;
            box(0x90, W[4] - E * half, G * 0.4f, E * half, H * 0.4f);
        }
        tri(0x01, 2, 0, 1);
        tri(0x01, 1, 3, 2);
        break;
    }
    case 19: {  // a palm: a trunk and fronds from height 20 down to 5
        stand();
        box(0xa4, p + E * 6.0f, G * 0.3f, E * 7.0f, H * 0.3f);
        float h = 20.0f, angle = 0.0f;
        while (true) {
            const float r1 = next_random(), r2 = next_random();
            angle = r2 * 0.5f + angle;
            const float k = (25.0f - h) * 0.25f;
            const float len = k - r1 * k * k / 25.0f;
            const float c0 = guest_cosf(angle), s0 = guest_sinf(angle);
            const float c1 = guest_cosf(angle + 0.8f), s1 = guest_sinf(angle + 0.8f);
            W[0] = p + E * h;
            W[1] = (W[0] + (G * c0 + H * s0) * (len * 0.5f)) - E * 2.0f;
            W[2] = (W[0] + (G * c1 + H * s1) * (len * 0.5f)) - E * 2.0f;
            W[3] = (W[0] + (G * ((c1 + c0) * 0.5f) + H * ((s1 + s0) * 0.5f)) * len) - E * 3.0f;
            frond();
            angle = angle + 0.8f;
            if (!(h - 0.2f > 5.0f)) break;
            h = h - 0.2f;
        }
        break;
    }
    case 20: {  // a fir: a trunk and branches in rings from height 18 down to 6
        stand();
        box(0xa4, p + E * 7.0f, G * 0.3f, E * 8.0f, H * 0.3f);
        float h = 18.0f, v = -6.0f;
        do {
            W[0] = p + E * h;
            const float ca = guest_cosf(guest::at<float>(g_random_table + g_random_index * 4u) * 6.2832f);
            const float sa = guest_sinf(guest::at<float>(g_random_table + g_random_index * 4u) * 6.2832f);
            g_random_index = static_cast<uint8_t>(g_random_index + 2);
            const float r = guest::at<float>(g_random_table + g_random_index * 4u);
            const float len = (r * 0.3f + 0.6f) - v * v / 60.0f;
            const float a = sa * len, b = len * ca;
            h = h - len * 0.2f;
            v = 12.0f - h;
            const Vec3 lift = E * ((v - r * 2.0f) * 0.1f);
            W[1] = ((W[0] + G * (b * 3.5f)) + H * (a * 3.5f)) + lift;
            W[2] = W[1];
            W[3] = ((W[0] + G * (b * 4.5f)) + H * (a * 4.5f)) + lift;
            W[4] = (G * a - H * b) + E * ((1.0f - r * 2.0f) * len);
            W[1] = W[1] + W[4];
            W[2] = W[2] - W[4];
            frond(true);
        } while (h > 6.0f);
        break;
    }
    case 21:
        stand();
        ground_shadow(0.3f, 1.7f);
        box(0x9c, p + E * 3.0f, G * 0.3f, E * 4.0f, H * 0.3f);
        box(0x9c, (p + E * 3.5f) + H * 1.0f, G * 0.3f, E * 0.3f, H * 0.7f);
        box(0x9c, (p + E * 2.5f) - H * 1.0f, G * 0.3f, E * 0.3f, H * 0.7f);
        box(0x9c, (p + E * 4.5f) + H * 1.4f, G * 0.3f, E * 1.0f, H * 0.3f);
        box(0x9c, (p + E * 3.5f) - H * 1.4f, G * 0.3f, E * 1.0f, H * 0.3f);
        break;
    case 22:
        stand();
        ground_shadow(0.3f, 9.0f);
        box(0xb0, p + E * 10.0f, G * 0.2f, E * 1.9f, H * 9.0f);
        box(0x01, (p + E * 5.0f) + H * 9.0f, G * 0.4f, E * 7.0f, H * 0.4f);
        box(0x01, (p + E * 5.0f) - H * 9.0f, G * 0.4f, E * 7.0f, H * 0.4f);
        break;
    case 23:
        stand();
        ground_shadow(0.2f, 3.0f);
        box(0xa4, p + E * 2.0f, G * 0.15f, E * 0.15f, H * 3.0f);
        box(0xa4, p + E * 1.3f, G * 0.15f, E * 0.15f, H * 3.0f);
        box(0xa4, p + E * 0.6f, G * 0.15f, E * 0.15f, H * 3.0f);
        box(0xa4, (p + E * 0.5f) + H * 2.0f, G * 0.2f, E * 2.0f, H * 0.2f);
        box(0xa4, (p + E * 0.5f) - H * 2.0f, G * 0.2f, E * 2.0f, H * 0.2f);
        break;
    case 24:
        stand();
        box(0x90, p, G * 0.7f, E * 1.2f, H * 0.7f);
        box(0x90, p + E * 1.6f, G * 0.4f, E * 0.4f, H * 0.4f);
        box(0x14, p + E * 2.0f, G * 0.6f, E * 0.1f, H * 0.6f);
        box(0x14, p + E * 2.25f, G * 0.3f, E * 0.15f, H * 0.3f);
        break;
    case 25:
    case 37: {  // a cone (37: the '%' marker, a different colour)
        slope(1.0f, 1.4f, 1.0f);
        place(0, p + E * 1.5f);
        const float ring[3] = {0.1f, 0.22f, 0.3f}, lift[3] = {1.0f, 0.4f, 0.15f};
        for (int r = 0; r < 3; r++) {
            const float q = ring[r], e = lift[r];
            place(1 + 4 * r, ((p - H * q) - G * q) + E * e);
            place(2 + 4 * r, ((p - H * q) + G * q) + E * e);
            place(3 + 4 * r, ((p + H * q) + G * q) + E * e);
            place(4 + 4 * r, ((p + H * q) - G * q) + E * e);
        }
        project_all(13);
        const uint8_t outer = type == 37 ? 0xb4 : 0x73;
        for (int k = 0; k < 4; k++) tri(outer, 1 + (k + 1) % 4, 0, 1 + k);
        for (int r = 0; r < 2; r++) {
            const uint8_t c = r == 0 ? 0x69 : outer;
            for (int k = 0; k < 4; k++) {
                const int a = 1 + 4 * r + (k + 1) % 4, b = 1 + 4 * r + k, lo = 5 + 4 * r;
                tri(c, a, b, lo + k);
                tri(c, lo + (k + 1) % 4, a, lo + k);
            }
        }
        box(0xa8, p, G * 0.5f, E * 0.15f, H * 0.5f);
        break;
    }
    case 26:
        stand();
        shadow(0, 0.15f, 0.8f);
        W[4] = (p + G * 0.4f) + H * 0.8f;
        W[5] = (p - G * 0.4f) + H * 0.8f;
        W[6] = (p + G * 0.4f) + H * 0.6f;
        W[7] = (p - G * 0.4f) + H * 0.6f;
        W[8] = (p + G * 0.4f) - H * 0.8f;
        W[9] = (p - G * 0.4f) - H * 0.8f;
        W[10] = (p + G * 0.4f) - H * 0.6f;
        W[11] = (p - G * 0.4f) - H * 0.6f;
        for (int k = 0; k < 12; k++) view_ground(k);
        for (int k = 0; k < 12; k += 4) {
            undraw(k, k + 1, k + 2);
            undraw(k + 1, k + 2, k + 3);
        }
        box(0xa4, p + E * 2.0f, G * 0.2f, E * 3.0f, H * 0.2f);
        box(0xa4, p + E * 5.08f, G * 0.1f, E * 0.1f, H * 0.8f);
        box(0xa4, (p + E * 5.03f) + H * 0.75f, G * 0.4f, E * 0.05f, H * 0.05f);
        box(0xa4, (p + E * 5.03f) - H * 0.75f, G * 0.4f, E * 0.05f, H * 0.05f);
        break;
    case 29:
        slope(3.0f, 3.0f, 0.5f);
        box(0xb8, p, G * 0.2f, E * 0.1f, H * 3.0f);
        box(0xb8, p + E * 0.3f, G * 0.2f, E * 0.1f, H * 3.0f);
        box(0xb8, p + E * 0.6f, G * 0.2f, E * 0.1f, H * 3.0f);
        box(0xb8, (p - H * 2.95f) - G * 0.25f, G * 0.1f, E * 0.75f, H * 0.1f);
        box(0xb8, p - G * 0.25f, G * 0.1f, E * 0.75f, H * 0.1f);
        break;
    case 30: {  // a gantry over the track with a banner on both sides
        stand();
        W2[0] = (p + H * 20.0f) + G * 1.0f;
        W2[1] = (p + H * 20.0f) - G * 1.0f;
        W2[2] = (p - H * 20.0f) + G * 1.0f;
        W2[3] = (p - H * 20.0f) - G * 1.0f;
        for (int k = 0; k < 4; k++) {
            W2[k] = W2[k] - g_view_eye;
            project(W2[k], P2[k]);
        }
        undraw_triangle(P2[0].x, P2[0].y, P2[0].z, P2[1].x, P2[1].y, P2[1].z, P2[2].x, P2[2].y, P2[2].z);
        undraw_triangle(P2[1].x, P2[1].y, P2[1].z, P2[3].x, P2[3].y, P2[3].z, P2[2].x, P2[2].y, P2[2].z);
        box(0xb8, p + E * 7.0f, G * 1.0f, E * 1.5f, H * 10.0f);
        box(0x01, (p - H * 10.0f) - G * 0.9f, G * 0.1f, E * 5.5f, H * 0.1f);
        box(0x01, (p - H * 10.0f) + G * 0.9f, G * 0.1f, E * 5.5f, H * 0.1f);
        box(0x01, (p + H * 10.0f) - G * 0.9f, G * 0.1f, E * 5.5f, H * 0.1f);
        box(0x01, (p + H * 10.0f) + G * 0.9f, G * 0.1f, E * 5.5f, H * 0.1f);
        for (int side = 0; side < 2; side++) {  // the ramps at both ends, 8 points each
            const float h = side == 0 ? -20.0f : 20.0f, m = side == 0 ? -10.0f : 10.0f;
            const int b = side * 8;
            place(b + 0, ((p + H * h) + E * 3.0f) + G * 1.0f);
            place(b + 1, ((p + H * m) + E * 8.5f) + G * 1.0f);
            place(b + 2, ((p + H * m) + E * 5.5f) + G * 1.0f);
            place(b + 3, (p + H * h) + G * 1.0f);
            place(b + 4, ((p + H * h) + E * 3.0f) - G * 1.0f);
            place(b + 5, ((p + H * m) + E * 8.5f) - G * 1.0f);
            place(b + 6, ((p + H * m) + E * 5.5f) - G * 1.0f);
            place(b + 7, (p + H * h) - G * 1.0f);
        }
        project_all(16);
        static const float size[11] = {0.3f, 0.28f, 0.26f, 0.24f, 0.22f, 0.2f, 0.18f, 0.16f, 0.14f, 0.12f, 0.1f};
        box(0x69, (p + E * 7.0f) + G * 1.01f, G * 0.01f, E * 1.1f, H * 8.0f);
        box(0x73, ((p + E * 7.25f) + G * 1.02f) - H * 5.8f, G * 0.01f, E * 0.4f, H * 1.8f);
        for (int k = 0; k < 11; k++) {  // letters
            const Vec3 base = (p + E * 7.0f) + G * 1.03f;
            box(0x01, k <= 3 ? base - H * static_cast<float>(3 - k) : base + H * static_cast<float>(k - 3), G * 0.01f,
                E * 0.8f, H * size[k]);
        }
        box(0x69, (p + E * 7.0f) - G * 1.01f, G * 0.01f, E * 1.1f, H * 8.0f);
        box(0x73, ((p + E * 7.25f) - G * 1.02f) + H * 5.8f, G * 0.01f, E * 0.4f, H * 1.8f);
        for (int k = 0; k < 11; k++) {
            const Vec3 base = (p + E * 7.0f) - G * 1.03f;
            box(0x01, k <= 3 ? base + H * static_cast<float>(3 - k) : base - H * static_cast<float>(k - 3), G * 0.01f,
                E * 0.8f, H * size[k]);
        }
        static const uint8_t faces[10][4] = {{0xb8, 0, 2, 1}, {0xb8, 0, 3, 2}, {0xb8, 5, 6, 4}, {0xb8, 6, 7, 4},
                                             {0xb8, 0, 1, 4}, {0xb8, 4, 1, 5}, {0xb8, 3, 2, 4}, {0xb8, 4, 2, 6},
                                             {0x01, 7, 3, 4}, {0x01, 4, 3, 0}};
        for (const auto &f : faces) tri(f[0], f[1], f[2], f[3]);
        static const uint8_t far_faces[10][4] = {{0xb8, 11, 8, 10}, {0xb8, 10, 8, 9}, {0xb8, 15, 14, 12},
                                                 {0xb8, 12, 14, 13}, {0xb8, 9, 8, 13}, {0xb8, 13, 8, 12},
                                                 {0xb8, 10, 11, 14}, {0xb8, 14, 11, 15}, {0x01, 8, 15, 12},
                                                 {0x01, 11, 15, 8}};
        for (const auto &f : far_faces) tri(f[0], f[1], f[2], f[3]);
        break;
    }
    case 31:
        slope(3.0f, 3.0f, 0.5f);
        break;
    case 32: {  // a bush of four boxes in random colours
        stand();
        rand_int();
        float s = 0.8f;
        if (!(rand_int() % 10 > 2)) s = 0.5f;
        box(kColours[3][rand_int() % 5], p, G * (s * 0.5f), E * (s * 0.8f), H * (s * 0.3f));
        box(kColours[2][rand_int() % 5], p + E * (s * 1.15f), G * (s * 0.6f), E * (s * 0.5f), H * (s * 0.4f));
        box(kColours[1][rand_int() % 5], p + E * (s * 2.05f), G * (s * 0.3f), E * (s * 0.3f), H * (s * 0.3f));
        box(kColours[0][rand_int() % 5], p + E * (s * 2.5f), G * (s * 0.3f), E * (s * 0.1f), H * (s * 0.3f));
        break;
    }
    case 36:
        o->unknown1c = 1;
        break;
    }
}
}  // namespace

void objects_draw() {
    Draw d;
    for (int j = 6; j < g_object_count; j++) {
        d.o = &g_objects.get()[j];
        d.draw(d.o->type);
    }
}
