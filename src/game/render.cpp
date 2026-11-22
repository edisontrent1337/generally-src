// The race view's software renderer (see render.h).
#include "game/render.h"

#include <bit>
#include <utility>

#include "game/race_state.h"
#include "game/text.h"
#include "game/track.h"
#include "game/fmath.h"
#include "game/video.h"
#include "orig/sdl.h"

// A filled disc centred at screen (x, y), at view distance z, of `radius` (scaled by screen width / z):
// pixels are drawn where the depth buffer holds something farther (or always). Rows come from the disc
// width table; a row outside it ends the disc.
void draw_disc(uint8_t color, bool always, float x, float y, float z, float radius) {
    if (!(z > 0.1f)) return;
    float r = static_cast<float>(g_screen_w) / z * radius;
    int depth = guest::to_int(z / g_depth_scale);
    int top = guest::to_int(y - r) + 1;
    int bottom = guest::to_int(y + r) + 1;
    if (top < 0) top = 0;
    if (bottom > g_screen_h) bottom = g_screen_h;
    float t = ((static_cast<float>(top) - y) / r + 1.0f) * 50.0f;
    float step = 50.0f / r;
    const float *widths = &guest::at<float>(g_disc_widths);
    for (int row = top; row < bottom; row++, t += step) {
        int i = guest::to_int(t);
        if (i < 0 || i > 99) return;
        float half = widths[i] * r;
        int left = guest::to_int(x - half) + 1;
        int right = guest::to_int(x + half) + 1;
        if (left < 0) left = 0;
        if (right > g_screen_w) right = g_screen_w;
        uint8_t *pixels = &guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(g_pitch * row));
        const uint16_t *zbuf = &guest::at<uint16_t>(g_depth_buf + static_cast<uint32_t>(g_screen_w * row) * 2);
        for (int col = left; col < right; col++)
            if (always || depth < zbuf[col]) pixels[col] = color;
    }
}

// Uncovers a disc: inside a disc centred at screen (x, y), at view distance z, of `radius` (scaled by
// g_start_light_depth / z), pixels whose depth buffer entry is farther than z get their colour back from
// the unshaded track image. Used where something above the track (smoke, an object) is cut out again.
void undraw_disc(float x, float y, float z, float radius) {
    if (!(z > 0.0001f)) return;
    float r = g_start_light_depth / z * radius;
    int depth = guest::to_int((z - 1.0f) / g_depth_scale);
    int top = guest::to_int(y - r) + 1;
    int bottom = guest::to_int(y + r) + 1;
    if (top < 0) top = 0;
    if (bottom > g_screen_h) bottom = g_screen_h;
    float t = ((static_cast<float>(top) - y) / r + 1.0f) * 50.0f;
    float step = 50.0f / r;
    const float *widths = &guest::at<float>(g_disc_widths);
    for (int row = top; row < bottom; row++, t += step) {
        float half = widths[guest::to_int(t)] * r;
        int left = guest::to_int(x - half) + 1;
        int right = guest::to_int(x + half) + 1;
        if (left < 0) left = 0;
        if (right > g_screen_w) right = g_screen_w;
        uint8_t *pixels = &guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(g_pitch * row));
        const uint8_t *image = &guest::at<uint8_t>(g_track_image + static_cast<uint32_t>(g_screen_w * row));
        const uint16_t *zbuf = &guest::at<uint16_t>(g_depth_buf + static_cast<uint32_t>(g_screen_w * row) * 2);
        for (int col = left; col < right; col++)
            if (depth < zbuf[col]) pixels[col] = image[col];
    }
}

// Restores the unshaded track image over a projected triangle (0x418fd0): (x, y) screen position and z
// view distance of three corners; pixels whose depth buffer entry lies behind the nearest corner get
// the track image back. The corners are sorted by y (top, middle, bottom) and each row is filled
// between the long edge (top to bottom) and the short edge on its other side.
void undraw_triangle(float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz) {
    float z = (bz > az && cz > az) ? az : (cz > bz ? bz : cz);
    int depth = guest::to_int((z - 0.5f) / g_depth_scale);
    struct P { float x, y; } a{ax, ay}, b{bx, by}, c{cx, cy}, top, mid, bot;
    if (by > ay) {
        if (ay > cy) top = c, mid = a, bot = b;
        else if (cy > by) top = a, mid = b, bot = c;
        else top = a, mid = c, bot = b;
    } else {
        if (by > cy) top = c, mid = b, bot = a;
        else if (cy > ay) top = b, mid = a, bot = c;
        else top = b, mid = c, bot = a;
    }
    int it = guest::to_int(top.y), im = guest::to_int(mid.y), ib = guest::to_int(bot.y);
    float s_tm = 0.0f, s_tb = 0.0f, s_mb = 0.0f;  // dx/dy of the edges
    if (im != it) s_tm = (mid.x - top.x) / (mid.y - top.y);
    if (ib != it) s_tb = (bot.x - top.x) / (bot.y - top.y);
    if (ib != im) s_mb = (bot.x - mid.x) / (bot.y - mid.y);
    int y = it + 1;
    if (y < 0) y = 0;
    int y_mid = im + 1, y_end;
    if (g_screen_h < y_mid) {
        y_mid = g_screen_h;
        y_end = g_screen_h;
    } else {
        y_end = ib + 1;
        if (g_screen_h < y_end) y_end = g_screen_h;
    }
    auto edge = [](int row, const P &from, float slope) {
        return guest::to_int((static_cast<float>(row) - from.y) * slope + from.x) + 1;
    };
    auto span = [&](int row, int left, int right) {
        if (left < 0) left = 0;
        if (right > g_screen_w) right = g_screen_w;
        uint8_t *pixels = &guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(g_pitch * row));
        const uint8_t *image = &guest::at<uint8_t>(g_track_image + static_cast<uint32_t>(g_screen_w * row));
        const uint16_t *zbuf = &guest::at<uint16_t>(g_depth_buf + static_cast<uint32_t>(g_screen_w * row) * 2);
        for (int col = left; col < right; col++)
            if (depth < zbuf[col]) pixels[col] = image[col];
    };
    float cross = (mid.x - top.x) * (top.y - bot.y) + (bot.x - top.x) * (mid.y - top.y);
    if (0.0f > cross) {  // the long edge is on the left
        for (; y < y_mid; y++) span(y, edge(y, top, s_tb), edge(y, top, s_tm));
        for (; y < y_end; y++) span(y, edge(y, top, s_tb), edge(y, mid, s_mb));
    } else {
        for (; y < y_mid; y++) span(y, edge(y, top, s_tm), edge(y, top, s_tb));
        for (; y < y_end; y++) span(y, edge(y, mid, s_mb), edge(y, top, s_tb));
    }
}

// Shows the frame: with the start lights on (g_start_light 0..3), first draws the three lights in the top
// left corner (lit up to g_start_light while it is at most 2).
void frame_present() {
    if (g_start_light >= 0) {
        lock_surface(1);
        int f = g_font_size;
        if (g_back) {
            guest::Local<SdlRect> r;
            r->x = 0;
            r->y = 0;
            r->w = static_cast<uint16_t>(f << 4);
            r->h = static_cast<uint16_t>(f * 4);
            sdl::FillRect(g_back, r.get(), 0xf0);
        }
        float u = static_cast<float>(f);
        float y_outer = u * 2.0f - 0.5f, y_inner = u * 1.7f - 0.5f, y_spot = u * 1.4f - 0.5f;
        float z = g_start_light_depth;
        int light = g_start_light;
        for (int i = 0; i < 3; i++) {
            float x = static_cast<float>(f * 4 * (i + 1)) - 0.5f;
            bool lit = i <= light && light <= 2;
            draw_disc(0xf1, true, x, y_outer, z, u * 1.8f);
            draw_disc(lit ? 0xf4 : 1, true, x, y_outer, z, u * 1.5f);
            draw_disc(lit ? 0xf3 : 10, true, x, y_inner, z, u);
            draw_disc(lit ? 0xf2 : 0x19, true, x, y_spot, z, u * 0.3f);
        }
        if (g_locked) {
            g_pitch = 0;
            g_pixels = 0;
            sdl::UnlockSurface(g_locked);
            g_locked = SdlSurfaceRef{};
        }
    }
    blit_surface(1, 0);
    guest::call_import(IMP_SDL_Flip, sdl::GetVideoSurface());
}

// The shading ranges of the 48 colour groups: a pixel of group g is shaded between palette index
// g_shade_dark[g] (shadow) and g_shade_light[g] (full light). Groups 0..7 are greys, 8..39 the
// palette's colour ramps, 40..47 a fixed pair.
void shade_tables_init() {
    static const uint8_t kLight[48] = {
        12,  29,  27,  25,  23,  21,  19,  17,  44,  42,  40,  38,  59,  57,  55,  53,
        74,  73,  72,  71,  89,  87,  85,  83,  104, 102, 100, 98,  109, 108, 107, 106,
        114, 113, 112, 111, 119, 118, 117, 116, 26,  26,  26,  26,  26,  26,  26,  26,
    };
    static const uint8_t kDark[48] = {
        1,   8,   7,   6,   5,   4,   3,   2,   30,  30,  30,  30,  48,  47,  46,  45,
        63,  62,  61,  60,  75,  75,  75,  75,  93,  92,  91,  90,  106, 106, 105, 105,
        111, 111, 110, 110, 116, 116, 115, 115, 18,  18,  18,  18,  18,  18,  18,  18,
    };
    uint8_t *light = &guest::at<uint8_t>(g_shade_light), *dark = &guest::at<uint8_t>(g_shade_dark);
    for (int g = 0; g < 48; g++) {
        dark[g] = kDark[g];
        light[g] = kLight[g];
    }
}

// Repaints a track tile whose terrain type changed (tyre marks, dirt), 0x40f2f0: the tile centre on the
// terrain is projected and a disc of the tile's projected size is filled where the depth buffer shows
// the terrain at that distance and the pixel is ground (palette index below 0x78). The colour is the
// terrain type's dark shade: in the unshaded track image and, where the screen pixel was unshaded too,
// on screen; elsewhere the screen gets the light-to-dark blend for the terrain normal's angle to the
// view. Types from 0x28 on are noisy: a random amount (3x a table entry, minus 1) is added.
void track_redraw_tile(int tx, int tz) {
    uint32_t type = guest::at<uint8_t>(g_tile_map + static_cast<uint32_t>(tz * 0x200 + tx));
    Vec3 normal(0.0f, 0.0f, 0.0f);
    Vec3 p;
    p.y = 0.0f;
    p.x = (static_cast<float>(tx) + 0.5f) * g_tile_size;
    p.z = (static_cast<float>(tz) + 0.5f) * g_tile_size;
    guest::Local<Vec3> lp, ln;
    *lp = p;
    *ln = normal;
    terrain_sample(lp.get(), ln.get());
    p = *lp;
    normal = *ln;

    const Vec3 &f = g_view_forward, &e = g_view_eye, &r = g_view_right, &d = g_view_down;
    float dx = p.x - e.x, dy = p.y - e.y, dz = p.z - e.z;
    float depth = f.y * dy + f.x * dx + f.z * dz;
    if (!(depth > 0.0001f)) return;
    float scale = g_start_light_depth;  // projection: pixels per unit at distance 1
    float cx = ((r.y * dy + r.x * dx + r.z * dz) / depth + 0.5f) * scale;
    float cy = ((d.y * dy + d.x * dx + d.z * dz) / depth + g_screen_aspect * 0.5f) * scale;
    // brightness: the normal against the direction from a point above the eye
    float uy = dy - depth;
    float facing = normal.y * uy + normal.x * dx + normal.z * dz;
    float t = facing / (guest_sqrtf(uy * uy + dx * dx + dz * dz) + 0.001f);
    if (t > 0.0f) t = 0.0f;
    float light = guest_sqrtf(1.0f - t * t);

    float radius = g_track_size * 0.71f * 0.001953125f * scale / depth;
    int y = guest::to_int((cy - radius) + 1.0f);
    int y_end = guest::to_int((radius + cy) + 1.0f);
    if (y < 0) y = 0;
    if (y_end > g_screen_h) y_end = g_screen_h;
    if (y >= y_end) return;
    float r2 = radius * radius;
    float cx1 = cx + 1.0f;
    const uint8_t dark = guest::at<uint8_t>(g_shade_dark + type), bright = guest::at<uint8_t>(g_shade_light + type);
    for (; y < y_end; y++) {
        float dyy = static_cast<float>(y) - cy;
        float half = guest_sqrtf(r2 - dyy * dyy);
        int x = guest::to_int((cx - half) + 1.0f);
        int x_end = guest::to_int(cx1 + half);
        if (x < 0) x = 0;
        if (x_end > g_screen_w) x_end = g_screen_w;
        if (x >= x_end) continue;
        uint8_t *image = &guest::at<uint8_t>(g_track_image + static_cast<uint32_t>(g_screen_w * y));
        uint8_t *screen = &guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(g_pitch * y));
        const uint16_t *zrow = &guest::at<uint16_t>(g_depth_buf + static_cast<uint32_t>(g_screen_w * y) * 2);
        int ztest = guest::to_int((depth - 0.1f) / g_depth_scale);
        for (; x < x_end; x++) {
            if (ztest > zrow[x] || screen[x] >= 0x78) continue;
            if (screen[x] == image[x]) {
                if (type < 0x28) {
                    image[x] = screen[x] = dark;
                } else {
                    float n = guest::at<float>(g_random_table + 4u * ++g_random_index);
                    image[x] = screen[x] = static_cast<uint8_t>(guest::to_int((n * 3.0f + static_cast<float>(dark)) - 1.0f));
                }
            } else {
                uint8_t k = ++g_random_index;
                float n = guest::at<float>(g_random_table + 4u * k);
                float c = static_cast<float>(static_cast<int>(dark) - static_cast<int>(bright)) * light + static_cast<float>(bright);
                if (type < 0x28) {
                    screen[x] = static_cast<uint8_t>(guest::to_int(c + n));
                    image[x] = dark;
                } else {
                    screen[x] = static_cast<uint8_t>(guest::to_int((c + n * 3.0f) - 1.0f));
                    image[x] = static_cast<uint8_t>(guest::to_int((n * 3.0f + static_cast<float>(dark)) - 1.0f));
                }
            }
        }
    }
}

// Per-pixel callback of the water surface (0x44f470, set in the pointer at 0x4a4738 before the water
// plane is drawn): plane normal n and a point q on it relative to the eye, screen pixel (x, y). The view
// ray through the pixel is cut with the plane; if the hit is in front of the depth buffer, the water's
// wave normal there comes from a 16x16 patch of g_random_table used as a height field (bilinear slopes
// over one world unit). If the view ray reflected off it points downwards, what is already drawn shows
// through darkened: ground colours become colour / 5 + 0x78 (the water shades), water shades get the
// low bits 3, in the track image too. Otherwise the pixel is sky-coloured (0xf8 minus the reflection's
// steepness) and its depth is recorded.
void water_pixel(float nx, float ny, float nz, float qx, float qy, float qz, int x, int y) {
    const Vec3 &f = g_view_forward, &e = g_view_eye, &r = g_view_right, &d = g_view_down;
    float u = static_cast<float>(x) / g_start_light_depth - 0.5f;
    float v = static_cast<float>(y) / g_start_light_depth - g_screen_aspect * 0.5f;
    float dx = (u * r.x + f.x) + v * d.x;
    float dz = (u * r.z + f.z) + v * d.z;
    float dy = (u * r.y + f.y) + v * d.y;
    int row = g_screen_w * y;
    int index = row + x;
    uint16_t &zbuf = guest::at<uint16_t>(g_depth_buf + static_cast<uint32_t>(index) * 2);
    float dist = (ny * qy + nx * qx + nz * qz) / (dy * ny + dx * nx + dz * nz);
    float z = dist / g_depth_scale;
    if (guest::to_int(z) >= zbuf || zbuf >= 0xffff) return;

    // wave normal at the hit point
    float hx = dx * dist + e.x, hz = dz * dist + e.z;
    int ix = guest::to_int(hx), iz = guest::to_int(hz);
    float fx = hx - static_cast<float>(ix), fz = hz - static_cast<float>(iz);
    const float *h = &guest::at<float>(g_random_table);
    int r0 = (iz % 16) * 16, r1 = ((iz + 1) % 16) * 16, r2 = ((iz + 2) % 16) * 16;
    int c0 = ix % 16, c1 = (ix + 1) % 16, c2 = (ix + 2) % 16;
    float gx = (h[r0 + c2] - h[r0 + c1]) * fx * (1.0f - fz);
    gx = gx + (h[r0 + c1] - h[r0 + c0]) * (1.0f - fx) * (1.0f - fz);
    gx = gx + (h[r1 + c1] - h[r1 + c0]) * (1.0f - fx) * fz;
    gx = gx + (h[r1 + c2] - h[r1 + c1]) * fx * fz;
    gx = gx * 0.25f;
    float gz = (h[r2 + c0] - h[r1 + c0]) * (1.0f - fx) * fz + (h[r1 + c0] - h[r0 + c0]) * (1.0f - fx) * (1.0f - fz);
    gz = gz + (h[r1 + c1] - h[r0 + c1]) * fx * (1.0f - fz);
    gz = gz + (h[r2 + c1] - h[r1 + c1]) * fx * fz;
    gz = gz * 0.25f;
    float len = guest_sqrtf((gx * gx + 1.0f) + gz * gz);
    float wy = 1.0f / len, wx = -gx / len, wz = -gz / len;

    // reflect the (normalized) view ray
    float dl = guest_sqrtf((dy * dy + dx * dx) + dz * dz) + 0.001f;
    dx = dx / dl;
    dy = dy / dl;
    dz = dz / dl;
    float dot = (wy * dy + wx * dx) + wz * dz;
    float rx = dx - wx * 2.0f * dot, ry = dy - wy * 2.0f * dot, rz = dz - wz * 2.0f * dot;
    uint8_t &image = guest::at<uint8_t>(g_track_image + static_cast<uint32_t>(row + x));
    uint8_t &screen = guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(g_pitch * y + x));
    if ((wy * ry + wx * rx) + wz * rz + 0.005f > ry) {
        if (screen < 0x78) {
            screen = static_cast<uint8_t>(screen / 5 + 0x78);
            image = static_cast<uint8_t>(image / 5 + 0x78);
        } else {
            screen = static_cast<uint8_t>((screen & 0xfc) + 3);
            image = static_cast<uint8_t>((image & 0xfc) + 3);
        }
        return;
    }
    uint8_t c = static_cast<uint8_t>(0xf8 - guest::to_int(ry * -6.5f * ry));
    image = c;
    screen = c;
    zbuf = static_cast<uint16_t>(guest::to_int(z));
}

// Calls the per-pixel function in g_pixel_fn (0x4a4738) for each pixel of a projected triangle
// (0x44e120): a, b, c are the corners on screen (x, y, view distance), wa, wb, wc the same corners in
// world space relative to the eye. Nothing is drawn if a corner is not in front (distance <= 0.0001)
// or the triangle is degenerate. The function gets the triangle's unit normal ((wc - wb) x (wa - wb)),
// wa, the pixel and `color` (passed through). Rows are filled from the topmost corner between the long
// edge (top to bottom) and the two short ones; which side the long edge is on follows from the order
// the corners come in.
void triangle_for_each_pixel(uint32_t color, const float a[3], const float b[3], const float c[3], const float wa[3],
                             const float wb[3], const float wc[3]) {
    float ex = wc[0] - wb[0], ey = wc[1] - wb[1], ez = wc[2] - wb[2];
    float fx = wa[0] - wb[0], fy = wa[1] - wb[1], fz = wa[2] - wb[2];
    float nx = ez * fy - ey * fz;
    float nz = ey * fx - ex * fy;
    float ny = ex * fz - ez * fx;
    if (!(a[2] > 0.0001f) || !(b[2] > 0.0001f) || !(c[2] > 0.0001f)) return;
    float len = guest_sqrtf((nx * nx + ny * ny) + nz * nz);
    if (!(len > 0.0001f)) return;
    nx = nx / len;
    ny = ny / len;
    nz = nz / len;

    const float *v[3] = {a, b, c};
    int top, mid, bot;
    bool long_left;  // the corners (top, mid, bottom) are an odd permutation of (a, b, c)
    if (a[1] > b[1]) {
        if (c[1] > a[1]) top = 1, mid = 0, bot = 2, long_left = true;
        else if (b[1] > c[1]) top = 2, mid = 1, bot = 0, long_left = true;
        else top = 1, mid = 2, bot = 0, long_left = false;
    } else {
        if (a[1] > c[1]) top = 2, mid = 0, bot = 1, long_left = false;
        else if (c[1] > b[1]) top = 0, mid = 1, bot = 2, long_left = false;
        else top = 0, mid = 2, bot = 1, long_left = true;
    }
    auto row_of = [](float y) {
        int r = guest::to_int(y + 1.0f);
        return r < 0 ? 0 : (r > g_screen_h ? g_screen_h : r);
    };
    const float *T = v[top], *M = v[mid], *B = v[bot];
    int y_top = row_of(T[1]), y_mid = row_of(M[1]), y_bot = row_of(B[1]);
    // x where an edge from p to q crosses row y
    auto edge = [](int y, const float *p, const float *q) {
        return guest::to_int(((static_cast<float>(y) - p[1]) * (q[0] - p[0])) / (q[1] - p[1]) + p[0] + 1.0f);
    };
    auto span = [&](int y, int left, int right) {
        if (left < 0) left = 0;
        if (right > g_screen_w) right = g_screen_w;
        for (int x = left; x < right; x++)
            guest::call_cdecl(g_pixel_fn, std::bit_cast<uint32_t>(nx), std::bit_cast<uint32_t>(ny), std::bit_cast<uint32_t>(nz),
                              std::bit_cast<uint32_t>(wa[0]), std::bit_cast<uint32_t>(wa[1]), std::bit_cast<uint32_t>(wa[2]),
                              static_cast<uint32_t>(x), static_cast<uint32_t>(y), color);
    };
    if (y_mid - y_top > 0 && y_top < y_mid)
        for (int y = y_top; y < y_mid; y++)
            long_left ? span(y, edge(y, T, B), edge(y, T, M)) : span(y, edge(y, T, M), edge(y, T, B));
    if (y_bot - y_mid > 0 && y_mid < y_bot)
        for (int y = y_mid; y < y_bot; y++)
            long_left ? span(y, edge(y, T, B), edge(y, M, B)) : span(y, edge(y, M, B), edge(y, T, B));
}

// Per-pixel callback of the ground around the track (0x44ec90, set in g_pixel_fn; arguments as for
// water_pixel). The view ray through the pixel is cut with the plane; if the hit is in front of the
// depth buffer, its depth is recorded. The slope there comes from the 64x64 height map (bilinear
// differences, one-sided at the borders, as in the original including the odd z slope at the low z
// border); the terrain type from the four nearest track tiles, weighted by distance with equal types
// pooled (outside the tile map: the tile under the point). Shading as track_redraw_tile: the light to
// dark blend for the slope's angle to the view, plus noise; types from 0x28 on are noisy in the
// unshaded track image too.
void terrain_pixel(float nx, float ny, float nz, float qx, float qy, float qz, int x, int y) {
    const Vec3 &f = g_view_forward, &e = g_view_eye, &r = g_view_right, &d = g_view_down;
    float v = static_cast<float>(y) / g_start_light_depth - g_screen_aspect * 0.5f;
    float u = static_cast<float>(x) / g_start_light_depth - 0.5f;
    int index = y * g_screen_w + x;
    uint16_t &zbuf = guest::at<uint16_t>(g_depth_buf + static_cast<uint32_t>(index) * 2);
    float dz = (f.z + u * r.z) + v * d.z;
    float dy = (u * r.y + f.y) + v * d.y;
    float dx = (u * r.x + f.x) + v * d.x;
    float dist = (ny * qy + nx * qx + nz * qz) / (dy * ny + dx * nx + dz * nz);
    float z = dist / g_depth_scale;
    if (guest::to_int(z) >= zbuf) return;
    zbuf = static_cast<uint16_t>(guest::to_int(z));
    float hx = e.x + dx * dist, hz = e.z + dz * dist;

    // slope from the height map
    const float cell = g_height_cell;
    float gx_f = hx / cell - 0.5f, gz_f = hz / cell - 0.5f;
    int ix = guest::to_int(gx_f), iz = guest::to_int(gz_f);
    float fx = gx_f - static_cast<float>(ix), fz = gz_f - static_cast<float>(iz);
    const uint8_t *h = g_height_map.get() + iz * 64 + ix;
    auto H = [&](int k) { return static_cast<float>(static_cast<int>(h[k])); };
    auto D = [&](int a, int b) { return static_cast<float>(static_cast<int>(h[a]) - static_cast<int>(h[b])); };
    float sx;
    if (ix < 0) sx = D(2, 1) * (1.0f - fz) + D(0x42, 0x41) * fz;
    else if (ix > 61) sx = D(0x41, 0x40) * fz + D(1, 0) * (1.0f - fz);
    else {
        sx = D(2, 1) * fx * (1.0f - fz) + D(1, 0) * (1.0f - fx) * (1.0f - fz);
        sx = sx + D(0x41, 0x40) * (1.0f - fx) * fz;
        sx = sx + D(0x42, 0x41) * fx * fz;
    }
    sx = sx * 0.05f / cell;
    float sz;
    if (iz < 0) sz = D(0x80, 0x40) * (1.0f - fx) + (H(0x81) * fz - H(0x41)) * fx;
    else if (iz > 61) sz = D(0x40, 0) * (1.0f - fx) + D(0x41, 1) * fx;
    else {
        sz = D(0x80, 0x40) * fz * (1.0f - fx) + D(0x40, 0) * (1.0f - fz) * (1.0f - fx);
        sz = sz + D(0x41, 1) * (1.0f - fz) * fx;
        sz = sz + D(0x81, 0x41) * fz * fx;
    }
    sz = sz * 0.05f / cell;
    float len = guest_sqrtf(sz * sz + (sx * sx + 1.0f));
    float wx = -sx / len, wy = 1.0f / len, wz = -sz / len;
    float uy = dy - 1.0f;
    float dot = (wy * uy + wx * dx) + wz * dz;
    float shade = dot / (guest_sqrtf((uy * uy + dx * dx) + dz * dz) + 0.001f);
    if (shade > 0.0f) shade = 0.0f;

    // terrain type
    float tx = hx / g_tile_size, tz = hz / g_tile_size;
    float px = tx - 0.5f, pz = tz - 0.5f;
    int itx = guest::to_int(px), itz = guest::to_int(pz);
    uint32_t type;
    const uint8_t *tiles = &guest::at<uint8_t>(g_tile_map);
    if (static_cast<uint32_t>(itx) > 0x1fe || static_cast<uint32_t>(itz) > 0x1fe) {
        type = tiles[guest::to_int(tz) * 512 + guest::to_int(tx)];
    } else {
        float ax = px - static_cast<float>(itx), az = pz - static_cast<float>(itz);
        const uint8_t *t0 = tiles + itz * 512 + itx;
        uint8_t T[4] = {t0[0], t0[1], t0[0x200], t0[0x201]};
        float W[4] = {(1.0f - ax) * (1.0f - az), (1.0f - az) * ax, (1.0f - ax) * az, az * ax};
        for (int i = 0; i < 3; i++)
            for (int j = i + 1; j < 4; j++)
                if (T[j] == T[i]) W[i] = W[j] + W[i];
        float best = W[0];
        uint8_t pick = T[0];
        if (W[1] > best) pick = T[1], best = W[1];
        if (W[2] > best) pick = T[2], best = W[2];
        if (W[3] > best) pick = T[3];
        type = pick;
    }
    const uint8_t dark = guest::at<uint8_t>(g_shade_dark + type), light = guest::at<uint8_t>(g_shade_light + type);
    uint8_t &screen = guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(g_pitch * y + x));
    uint8_t &image = guest::at<uint8_t>(g_track_image + static_cast<uint32_t>(index));
    uint8_t k = ++g_random_index;
    float n = guest::at<float>(g_random_table + 4u * k);
    float s = guest_sqrtf(1.0f - shade * shade);
    float c = s * static_cast<float>(static_cast<int>(dark) - static_cast<int>(light)) + static_cast<float>(light);
    if (type < 0x28) {
        screen = static_cast<uint8_t>(guest::to_int(c + n));
        image = dark;
    } else {
        screen = static_cast<uint8_t>(guest::to_int((c + n * 3.0f) - 1.0f));
        image = static_cast<uint8_t>(guest::to_int((n * 3.0f + static_cast<float>(dark)) - 1.0f));
    }
}

// Per-pixel callback of flat-coloured triangles (0x44e910; arguments as water_pixel plus the colour):
// the plane is cut with the view ray, depth-tested and its depth recorded; the brightness comes from
// the plane normal's angle to the direction from a point above the eye (not clamped here). Terrain
// colours (below 0x30) are shaded like track_redraw_tile; colours 0x98..0xa7 get a darker noisy shade,
// other colours one that is up to 3 palette steps darker. The unshaded track image gets the base colour.
void flat_pixel(float nx, float ny, float nz, float qx, float qy, float qz, int x, int y, uint8_t color) {
    const Vec3 &f = g_view_forward, &r = g_view_right, &d = g_view_down;
    float v = static_cast<float>(y) / g_start_light_depth - g_screen_aspect * 0.5f;
    float u = static_cast<float>(x) / g_start_light_depth - 0.5f;
    float dy = (r.y * u + f.y) + d.y * v;
    float dx = (r.x * u + f.x) + d.x * v;
    float dz = (r.z * u + f.z) + d.z * v;
    int row = g_screen_w * y;
    uint16_t &zbuf = guest::at<uint16_t>(g_depth_buf + static_cast<uint32_t>(row + x) * 2);
    float z = (ny * qy + nx * qx + nz * qz) / (dy * ny + dx * nx + dz * nz) / g_depth_scale;
    if (guest::to_int(z) >= zbuf) return;
    zbuf = static_cast<uint16_t>(guest::to_int(z));
    float uy = dy - 1.0f;
    float dot = (uy * ny + dx * nx) + dz * nz;
    float s = dot / (guest_sqrtf((uy * uy + dx * dx) + dz * dz) + 0.001f);
    float lit = 1.0f - s * s;
    uint8_t &screen = guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(g_pitch * y + x));
    uint8_t &image = guest::at<uint8_t>(g_track_image + static_cast<uint32_t>(row + x));
    if (color < 0x30) {
        uint8_t k = ++g_random_index;
        const uint8_t dark = guest::at<uint8_t>(g_shade_dark + color), light = guest::at<uint8_t>(g_shade_light + color);
        float c = guest_sqrtf(lit) * static_cast<float>(static_cast<int>(dark) - static_cast<int>(light)) + static_cast<float>(light);
        float n = guest::at<float>(g_random_table + 4u * k);
        if (color < 0x28) {
            screen = static_cast<uint8_t>(guest::to_int(c + n));
            image = dark;
        } else {
            screen = static_cast<uint8_t>(guest::to_int((c + n * 3.0f) - 1.0f));
            image = static_cast<uint8_t>(guest::to_int((n * 3.0f + static_cast<float>(dark)) - 1.0f));
        }
        return;
    }
    float darker;
    if (static_cast<uint8_t>(color + 0x68) <= 0xf) {  // 0x98..0xa7
        uint8_t k = ++g_random_index;
        darker = guest_sqrtf(lit) * 1.5f + guest::at<float>(g_random_table + 4u * k) * 1.5f;
    } else {
        darker = guest_sqrtf(lit) * 2.99f;
    }
    screen = static_cast<uint8_t>(color - static_cast<uint8_t>(guest::to_int(darker)) + 2);
    image = color;
}

// A Gouraud-shaded triangle (0x4180f0) of screen points p0..p2 with depths, shades s0..s2: pixels get
// colour + the shade interpolated as a plane over the screen (truncated). Drawn only when it faces the
// camera (the cross product is negative) and lies in front of it; pixels are tested against the depth
// buffer unless `always` (which is not written). The rows are split at the middle vertex; each half is
// filled between the long edge and one short edge, each edge's x measured from its upper vertex.
void shaded_triangle(uint8_t colour, bool always, float x0, float y0, float z0, float x1, float y1, float z1, float x2,
                     float y2, float z2, float s0, float s1, float s2) {
    const float dx1 = x1 - x0, dx2 = x2 - x0, dy2 = y2 - y0;
    if ((y0 - y1) * dx2 + dx1 * dy2 >= 0.0f) return;  // facing away (a NaN goes on)
    const int depth = guest::to_int(((z1 + z0) + z2) / (g_depth_scale * 3.0f));
    if (depth <= 0) return;
    const int iy0 = guest::to_int(y0), iy1 = guest::to_int(y1), iy2 = guest::to_int(y2);
    float m01 = 0.0f, m12 = 0.0f, m02 = 0.0f;  // dx/dy of the edges
    if (iy1 != iy0) m01 = dx1 / (y1 - y0);
    if (iy2 != iy1) m12 = (x2 - x1) / (y2 - y1);
    // the shade as a plane: s = x * A + B + y * C
    float A, C;
    if (iy2 == iy0) {
        A = (s2 - s0) / dx2;
        C = ((s1 - s0) - dx1 * A) / (y1 - y0);
    } else {
        m02 = dx2 / dy2;
        float k = (y1 - y0) / dy2;
        A = ((s1 - s0) - (s2 - s0) * k) / (dx1 - dx2 * k);
        C = ((s2 - s0) - dx2 * A) / dy2;
    }
    const float B = (s0 - A * x0) - C * y0;

    const float X[3] = {x0, x1, x2}, Y[3] = {y0, y1, y2};
    auto slope = [&](int a, int b) { return a + b == 1 ? m01 : a + b == 3 ? m12 : m02; };
    int top, mid, bot;
    if (y1 > y0) {
        if (y0 > y2) top = 2, mid = 0, bot = 1;
        else if (y2 > y1) top = 0, mid = 1, bot = 2;
        else top = 0, mid = 2, bot = 1;
    } else {
        if (y1 > y2) top = 2, mid = 1, bot = 0;
        else if (y2 > y0) top = 1, mid = 0, bot = 2;
        else top = 1, mid = 2, bot = 0;
    }
    // the winding is fixed, so the order's parity tells on which side the short edges lie
    const bool short_left = (top == 0 && mid == 1) || (top == 1 && mid == 2) || (top == 2 && mid == 0);
    const int iyt = top == 0 ? iy0 : top == 1 ? iy1 : iy2;
    const int iym = mid == 0 ? iy0 : mid == 1 ? iy1 : iy2;
    const int iyb = bot == 0 ? iy0 : bot == 1 ? iy1 : iy2;
    int row = iyt + 1;
    if (row < 0) row = 0;
    int mid_row = iym + 1, end_row = iyb + 1;
    if (mid_row > g_screen_h) mid_row = g_screen_h;
    if (end_row > g_screen_h) end_row = g_screen_h;

    auto span = [&](int r, int ls, float lm, int rs, float rm) {  // edges given by start vertex and slope
        const float fr = static_cast<float>(r);
        int left = guest::to_int((fr - Y[ls]) * lm + X[ls]) + 1;
        int right = guest::to_int((fr - Y[rs]) * rm + X[rs]) + 1;
        if (left < 0) left = 0;
        if (right > g_screen_w) right = g_screen_w;
        uint8_t *pixels = &guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(g_pitch * r));
        const uint16_t *zbuf = &guest::at<uint16_t>(g_depth_buf + static_cast<uint32_t>(g_screen_w * r) * 2);
        for (int col = left; col < right; col++)
            if (always || depth < zbuf[col])
                pixels[col] = static_cast<uint8_t>(guest::to_int((static_cast<float>(col) * A + B) + fr * C) + colour);
    };
    const float m_long = slope(top, bot), m_upper = slope(top, mid), m_lower = slope(mid, bot);
    for (; row < mid_row; row++) {
        if (short_left) span(row, top, m_upper, top, m_long);
        else span(row, top, m_long, top, m_upper);
    }
    for (; row < end_row; row++) {
        if (short_left) span(row, mid, m_lower, top, m_long);
        else span(row, top, m_long, mid, m_lower);
    }
}

namespace {
// A box's corners c +- h +- a +- b relative to the eye (w) and projected through the race camera (x, y and
// view distance z; x and y stay 0 behind the camera): 0 (+h +a +b), 1 (+h -a +b), 2 (-h +a +b),
// 3 (-h -a +b), 4..7 the same with -b.
struct BoxCorner {
    float x, y, z;
    float w[3];
};
void box_corners(const Vec3 &c, const Vec3 &a, const Vec3 &b, const Vec3 &h, BoxCorner p[8]) {
    const Vec3 &eye = g_view_eye, &fw = g_view_forward, &rt = g_view_right, &dn = g_view_down;
    const float K = g_start_light_depth;
    for (int i = 0; i < 8; i++) {
        const bool hp = i < 2 || (i >= 4 && i < 6), ap = (i & 1) == 0, bp = i < 4;
        auto axis = [&](float cc, float aa, float bb, float hh, float e) {
            float base = hp ? cc + hh : cc - hh;
            float t = ap ? aa + base : base - aa;
            return (bp ? bb + t : t - bb) - e;
        };
        float x = axis(c.x, a.x, b.x, h.x, eye.x), y = axis(c.y, a.y, b.y, h.y, eye.y), z = axis(c.z, a.z, b.z, h.z, eye.z);
        p[i].w[0] = x;
        p[i].w[1] = y;
        p[i].w[2] = z;
        float d = (y * fw.y + x * fw.x) + z * fw.z;
        p[i].z = d;
        p[i].x = p[i].y = 0.0f;
        if (d > 0.0001f) {
            p[i].x = (((y * rt.y + x * rt.x) + z * rt.z) / d + 0.5f) * K;
            p[i].y = (((y * dn.y + x * dn.x) + z * dn.z) / d + g_screen_aspect * 0.5f) * K;
        }
    }
}
// the faces at +b, +-a and +-h (the face at -b is not drawn), two triangles each
constexpr uint8_t kBoxTriangles[10][3] = {{0, 1, 2}, {3, 2, 1}, {6, 0, 2}, {4, 0, 6}, {1, 7, 3},
                                          {1, 5, 7}, {4, 1, 0}, {5, 1, 4}, {3, 6, 2}, {3, 7, 6}};
}  // namespace

// A solid box in the race view (0x4170a0): centre c, half-axes a, b, h, drawn as ten flat triangles in
// colour 0xa8, depth tested.
void box_draw(const Vec3 &c, const Vec3 &a, const Vec3 &b, const Vec3 &h) {
    BoxCorner p[8];
    box_corners(c, a, b, h, p);
    for (const auto &tr : kBoxTriangles) {
        const BoxCorner &u = p[tr[0]], &v = p[tr[1]], &w = p[tr[2]];
        shaded_triangle(0xa8, false, u.x, u.y, u.z, v.x, v.y, v.z, w.x, w.y, w.z, 0.0f, 0.0f, 0.0f);
    }
}

// The same box drawn pixel by pixel through g_pixel_fn (0x46a040), which gets `colour`.
void box_draw_pixels(uint8_t colour, const Vec3 &c, const Vec3 &a, const Vec3 &b, const Vec3 &h) {
    BoxCorner p[8];
    box_corners(c, a, b, h, p);
    for (const auto &tr : kBoxTriangles) {
        const BoxCorner &u = p[tr[0]], &v = p[tr[1]], &w = p[tr[2]];
        const float su[3] = {u.x, u.y, u.z}, sv[3] = {v.x, v.y, v.z}, sw[3] = {w.x, w.y, w.z};
        triangle_for_each_pixel(colour, su, sv, sw, u.w, v.w, w.w);
    }
}

// Small 3D view of a car model (0x443cc0): the menus' car previews (results, standings, track records,
// Driver options). view holds the camera axes as rows for the model's x, y and z (screen x, screen y,
// depth), dist moves the model away from the camera; (x, y) is the screen position of the model's
// origin. Body triangles in colours 0..7 are painted in the colours of colour_base (+ 0x18) * 8, other
// colours as they are. The four wheels (and the fifth point unless model +0xc0 is set) are drawn as
// discs and painted in between the triangles, far to near, so that each covers what lies behind it.
void car_preview_draw(uint32_t colour_base, const uint8_t *model, int x, int y, const float view[9], float dist) {
    auto F = [model](int off) { return *reinterpret_cast<const float *>(model + off); };
    const float ax = view[0], ay = view[1], az = view[2];  // model x
    const float bx = view[3], by = view[4], bz = view[5];  // model y
    const float cx = view[6], cy = view[7], cz = view[8];  // model z
    const float W = static_cast<float>(g_screen_w);
    const float near = 0.0001f;

    // the wheels (and fifth point): projected centre and depth
    struct Pt {
        float x, y, z;
    } pt[5] = {};
    const bool no_fifth = model[0xc0] != 0;
    for (int k = 0; k < (no_fifth ? 4 : 5); k++) {
        float px = F(0x504 + 12 * k), py = F(0x508 + 12 * k), pz = F(0x50c + 12 * k);
        float z = ((px * az + dist) + py * bz) + pz * cz;
        pt[k].z = z;
        if (z > near) {
            pt[k].x = ((py * bx + px * ax) + pz * cx) * W / z + static_cast<float>(x);
            pt[k].y = static_cast<float>(y) - ((py * by + px * ay) + pz * cy) * W / z;
        }
    }

    // the vertices: projected, and a shade from the angle between normal and view ray
    Pt v[30] = {};
    float shade[30] = {};  // (left unset in the original for vertices behind the camera)
    const int nv = model[0xbc];
    for (int i = 0; i < nv; i++) {
        float px = F(0x180 + 12 * i), py = F(0x184 + 12 * i), pz = F(0x188 + 12 * i);
        float X = ((px * ax + 0.0f) + py * bx) + pz * cx;
        float Y = ((px * ay + 0.0f) + py * by) + pz * cy;
        float Z = ((px * az + dist) + py * bz) + pz * cz;
        v[i].z = Z;
        if (!(Z > near)) continue;
        v[i].x = X * W / Z + static_cast<float>(x);
        v[i].y = static_cast<float>(y) - Y * W / Z;
        float nx = F(0x2e8 + 12 * i), ny = F(0x2ec + 12 * i), nz = F(0x2f0 + 12 * i);
        float NX = (nx * ax + ny * bx) + nz * cx;
        float NY = (nx * ay + ny * by) + nz * cy;
        float NZ = (nx * az + ny * bz) + nz * cz;
        float len = guest_sqrtf((X * X + Y * Y) + Z * Z);
        float s = ((NX * X + NY * Y) + NZ * Z) / (len + 0.001f);
        shade[i] = 0.0f > s ? s * s * 2.99f : 0.0f;
    }

    // the triangles' mean depth; -1 once drawn
    const int ni = model[0xbe];
    const uint8_t *idx = model + 0xc1;
    float depth[40];
    for (int t = 0; t < ni; t += 3) depth[t / 3] = ((v[idx[t + 1]].z + v[idx[t]].z) + v[idx[t + 2]].z) / 3.0f;
    auto triangle = [&](int t) {
        uint8_t c = model[0x139 + t / 3];
        if (c < 8) c = static_cast<uint8_t>(c + (colour_base + 0x18) * 8);
        const Pt &p0 = v[idx[t]], &p1 = v[idx[t + 1]], &p2 = v[idx[t + 2]];
        shaded_triangle(c, true, p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, p2.x, p2.y, p2.z, shade[idx[t]], shade[idx[t + 1]],
                        shade[idx[t + 2]]);
    };

    // the discs, far to near
    uint8_t order[5] = {0, 1, 2, 3, 4};
    const int n = no_fifth ? 4 : 5;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (pt[j].z > pt[i].z) {
                std::swap(order[i], order[j]);
                std::swap(pt[i], pt[j]);
            }
    for (int k = 0; k < n; k++) {
        const float z = pt[k].z;
        for (int t = 0; t < ni; t += 3) {
            if (z > depth[t / 3]) continue;  // nearer than this disc: later
            depth[t / 3] = -1.0f;
            triangle(t);
        }
        if (order[k] == 4) draw_disc(static_cast<uint8_t>(colour_base * 8 + 0xc5), true, pt[k].x, pt[k].y, z, 0.3f);
        else draw_disc(0xa9, true, pt[k].x, pt[k].y, z, F(0x1c + 4 * order[k]));
    }
    for (int t = 0; t < ni; t += 3)
        if (!(0.0f > depth[t / 3])) triangle(t);
}
