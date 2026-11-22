// The static track scene drawn into the background surface: the terrain, the four side walls of the
// track's block, its corner posts, the objects and the water plane. The race view (race_setup_view)
// and the menu's track preview (0x491870) both draw it.
//
// Screen positions of points behind the camera keep their previous value, as in the original; the water
// plane's fallbacks are the walls' last screen positions (and, for two coordinates, the walls' depths).
#include <cstdint>
#include <cstring>

#include "game/fmath.h"
#include "game/ini.h"
#include "game/objects.h"
#include "game/render.h"
#include "game/track.h"
#include "game/video.h"
#include "game/race_state.h"
#include "game/menu.h"
#include "game/race_options.h"
#include "game/selected_tracks.h"
#include "game/text.h"
#include "game/ghost.h"
#include "orig/render.h"
#include "bridge/imports.h"

namespace {
struct Pt {
    float x, y, z;
};

// Projects a point relative to the eye: depth always, screen x / y only in front of the camera.
void project(float x, float y, float z, Pt &p) {
    const Vec3 &R = g_view_right, &D = g_view_down, &F = g_view_forward;
    p.z = y * F.y + x * F.x + z * F.z;
    if (1e-4f < p.z) {
        p.x = ((y * R.y + x * R.x + z * R.z) / p.z + 0.5f) * g_start_light_depth;
        p.y = ((y * D.y + x * D.x + z * D.z) / p.z + g_screen_aspect * 0.5f) * g_start_light_depth;
    }
}

void triangle(uint8_t colour, const Pt &a, const Pt &b, const Pt &c, const Vec3 &wa, const Vec3 &wb, const Vec3 &wc) {
    const float sa[3] = {a.x, a.y, a.z}, sb[3] = {b.x, b.y, b.z}, sc[3] = {c.x, c.y, c.z};
    const float pa[3] = {wa.x, wa.y, wa.z}, pb[3] = {wb.x, wb.y, wb.z}, pc[3] = {wc.x, wc.y, wc.z};
    triangle_for_each_pixel(colour, sa, sb, sc, pa, pb, pc);
}

float height(int i) { return static_cast<float>(g_height_map.get()[i]) * 0.05f; }
// the ground at grid point i, but not below the water
float wall_top(int i) {
    float h = height(i);
    if (h < g_water_level) h = g_water_level;
    return h;
}

// The terrain: each grid cell as four triangles meeting at its centre.
void draw_terrain() {
    g_pixel_fn = 0x44ec90;  // terrain_pixel
    const float cell = g_height_cell;
    const Vec3 &e = g_view_eye;
    Pt a = {}, b = {}, c = {}, d = {}, m = {};
    for (int i = 0; i < 63; i++) {
        for (int j = 0; j < 63; j++) {
            const int k = j * 64 + i;
            const Vec3 wa(static_cast<float>(i) * cell - e.x, height(k) - e.y, static_cast<float>(j) * cell - e.z);
            const Vec3 wb(static_cast<float>(i + 1) * cell - e.x, height(k + 1) - e.y, static_cast<float>(j) * cell - e.z);
            const Vec3 wc(static_cast<float>(i) * cell - e.x, height(k + 64) - e.y, static_cast<float>(j + 1) * cell - e.z);
            const Vec3 wd(static_cast<float>(i + 1) * cell - e.x, height(k + 65) - e.y, static_cast<float>(j + 1) * cell - e.z);
            const Vec3 wm((((wb.x + wa.x) + wc.x) + wd.x) * 0.25f, (((wb.y + wa.y) + wc.y) + wd.y) * 0.25f,
                          (((wb.z + wa.z) + wc.z) + wd.z) * 0.25f);
            project(wa.x, wa.y, wa.z, a);
            project(wb.x, wb.y, wb.z, b);
            project(wc.x, wc.y, wc.z, c);
            project(wd.x, wd.y, wd.z, d);
            project(wm.x, wm.y, wm.z, m);
            triangle(0, a, b, m, wa, wb, wm);
            triangle(0, c, a, m, wc, wa, wm);
            triangle(0, b, d, m, wb, wd, wm);
            triangle(0, d, c, m, wd, wc, wm);
        }
    }
}

// The side walls: strips along the track's edges, 2 above the ground (or the water), 0.8 thick. Each
// step along an edge computes the cross-sections of both opposite walls (4 points each) and joins them
// to the previous step's.
struct Walls {
    Pt prev[8] = {}, cur[8] = {};
    Vec3 wprev[8], wcur[8];
    float near_z_low = 0.0f, far_z_out = 0.0f;  // two of the cross-section's coordinates (see water())

    void step_x(int i);
    void step_z(int j);
    void join(bool along_x);
};

// Cross-sections at x = i (the walls at z < 0 and z > size).
void Walls::step_x(int i) {
    const Vec3 &e = g_view_eye;
    const float x = static_cast<float>(i) * g_height_cell - e.x;
    const float y0 = 0.0f - e.y;
    const float zn = 0.0f - e.z;
    const float yn = (wall_top(i) + 2.0f) - e.y;
    near_z_low = -0.8f - e.z;
    const float zf_out = (g_track_size + 0.8f) - e.z;
    far_z_out = zf_out;
    const float yf = (wall_top(i + 0xfc0) + 2.0f) - e.y;
    const float zf = g_track_size - e.z;
    wcur[0] = Vec3(x, y0, zn);
    wcur[1] = Vec3(x, yn, zn);
    wcur[2] = Vec3(x, yn, near_z_low);
    wcur[3] = Vec3(x, y0, near_z_low);
    wcur[4] = Vec3(x, y0, zf_out);
    wcur[5] = Vec3(x, yf, zf_out);
    wcur[6] = Vec3(x, yf, zf);
    wcur[7] = Vec3(x, y0, zf);
    for (int k = 0; k < 8; k++) project(wcur[k].x, wcur[k].y, wcur[k].z, cur[k]);
}

// Cross-sections at z = j (the walls at x < 0 and x > size).
void Walls::step_z(int j) {
    const Vec3 &e = g_view_eye;
    const float z = static_cast<float>(j) * g_height_cell - e.z;
    const float y0 = 0.0f - e.y;
    const float xn = 0.0f - e.x;
    const float yn = (wall_top(j * 64) + 2.0f) - e.y;
    const float xn_out = -0.8f - e.x;
    const float yf = (wall_top(j * 64 + 0x3f) + 2.0f) - e.y;
    const float xf_out = (g_track_size + 0.8f) - e.x;
    const float xf = g_track_size - e.x;
    wcur[0] = Vec3(xn, y0, z);
    wcur[1] = Vec3(xn, yn, z);
    wcur[2] = Vec3(xn_out, yn, z);
    wcur[3] = Vec3(xn_out, y0, z);
    wcur[4] = Vec3(xf_out, y0, z);
    wcur[5] = Vec3(xf_out, yf, z);
    wcur[6] = Vec3(xf, yf, z);
    wcur[7] = Vec3(xf, y0, z);
    for (int k = 0; k < 8; k++) project(wcur[k].x, wcur[k].y, wcur[k].z, cur[k]);
}

// The quads between the previous and the current cross-sections, two triangles each (wound for the side
// they face).
void Walls::join(bool along_x) {
    for (int k = 0; k < 3; k++) {
        for (int w = 0; w < 8; w += 4) {
            const int a = w + k, b = w + k + 1;
            if (along_x) {
                triangle(0x90, cur[a], prev[a], prev[b], wcur[a], wprev[a], wprev[b]);
                triangle(0x90, cur[a], prev[b], cur[b], wcur[a], wprev[b], wcur[b]);
            } else {
                triangle(0x90, prev[a], cur[a], prev[b], wprev[a], wcur[a], wprev[b]);
                triangle(0x90, prev[b], cur[a], cur[b], wprev[b], wcur[a], wcur[b]);
            }
        }
    }
}

// A corner post: a box 0.8 wide, as high as the ground (or water) there plus 1, centred above it.
void post(float x, float z, int i) {
    const float y = wall_top(i) * 0.5f + 1.0f;
    box_draw_pixels(0x90, Vec3(x, y, z), Vec3(0.4f, 0.0f, 0.0f), Vec3(0.0f, y, 0.0f), Vec3(0.0f, 0.0f, 0.4f));
}

// The water plane over the whole track.
void draw_water(const Walls &w) {
    g_pixel_fn = 0x44f470;  // water_pixel
    const Vec3 &e = g_view_eye;
    const float y = g_water_level - e.y;
    const float x0 = 0.0f - e.x, x1 = g_track_size - e.x;
    const float z0 = 0.0f - e.z, z1 = g_track_size - e.z;
    Pt a, b, c, d;
    a.x = w.prev[0].x, a.y = w.near_z_low;
    b.x = w.prev[1].x, b.y = w.prev[1].y;
    c.x = w.prev[2].x, c.y = w.prev[2].y;
    d.x = w.prev[3].x, d.y = w.far_z_out;
    project(x0, y, z0, a);
    project(x1, y, z0, b);
    project(x0, y, z1, c);
    project(x1, y, z1, d);
    triangle(0, c, a, b, Vec3(x0, y, z1), Vec3(x0, y, z0), Vec3(x1, y, z0));
    triangle(0, b, d, c, Vec3(x1, y, z0), Vec3(x1, y, z1), Vec3(x0, y, z1));
}
}  // namespace

void track_scene_draw(bool objects) {
    guest::call_cdecl(0x497d10, g_depth_buf, 0xffff, g_screen_h * g_screen_w * 2);  // memset
    if (g_background) guest::call_import(IMP_SDL_FillRect, g_background, 0u, 0x64u);
    lock_surface(SURF_BACKGROUND);
    draw_terrain();

    g_pixel_fn = 0x44e910;  // flat_pixel
    Walls w;
    w.step_x(0);
    for (int i = 1; i < 64; i++) {
        std::memcpy(w.prev, w.cur, sizeof w.cur);
        std::memcpy(w.wprev, w.wcur, sizeof w.wcur);
        w.step_x(i);
        w.join(true);
    }
    const float near_z_low = w.near_z_low, far_z_out = w.far_z_out;
    w.step_z(0);
    for (int j = 1; j < 64; j++) {
        std::memcpy(w.prev, w.cur, sizeof w.cur);
        std::memcpy(w.wprev, w.wcur, sizeof w.wcur);
        w.step_z(j);
        w.join(false);
    }
    w.near_z_low = near_z_low;
    w.far_z_out = far_z_out;

    post(-0.4f, -0.4f, 0);
    post(g_track_size + 0.4f, -0.4f, 0x3f);
    post(-0.4f, g_track_size + 0.4f, 0xfc0);
    post(g_track_size + 0.4f, g_track_size + 0.4f, 0xfff);
    if (objects) guest::call_regs(0x44fa10, 0, 0);
    draw_water(w);
}

namespace {
uint32_t malloc_guest(uint64_t n) {  // 0x496c14; a size that overflowed asks for 0xffffffff bytes
    return guest::call_cdecl(0x496c14, n > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(n));
}
void free_global(uint32_t &p) {
    if (p) {
        guest::call_cdecl(0x496bd7, p);  // free
        p = 0;
    }
}
}  // namespace

int track_preview_render(const char *name, int w, int h) {
    guest::call_import(IMP_srand, guest::call_import(IMP_SDL_GetTicks));
    free_global(g_random_table);
    g_random_table = malloc_guest(0x400);
    for (int i = 0; i < 256; i++) {
        const int r = static_cast<int>(guest::call_import(IMP_rand));
        guest::at<float>(g_random_table + 4u * static_cast<uint32_t>(i)) = static_cast<float>(r) * 3.0517578125e-05f;
    }
    free_global(g_depth_buf);
    g_depth_buf = malloc_guest(static_cast<uint64_t>(static_cast<uint32_t>(g_screen_h * g_screen_w)) * 2);
    free_global(g_track_image);
    g_track_image = malloc_guest(static_cast<uint32_t>(g_screen_h * g_screen_w));
    g_start_light_depth = static_cast<float>(w);
    *reinterpret_cast<uint32_t *>(&g_depth_scale) = 0x3c160000;
    g_screen_aspect = static_cast<float>(h) / g_start_light_depth;
    free_global(g_tile_map);
    free_global(g_height_map.a);
    free_global(g_shade_dark);
    free_global(g_shade_light);
    g_tile_map = malloc_guest(0x40000);
    g_height_map.a = malloc_guest(0x1000);
    g_shade_dark = malloc_guest(0x30);
    g_shade_light = malloc_guest(0x30);
    shade_tables_init();
    guest::Local<MsString> path;
    path->init();
    path->assign(name, static_cast<uint32_t>(std::strlen(name)));
    const int loaded = track_preview_load(path.get());
    path->destroy();
    if (loaded == -1) {
        free_global(g_objects.a);
        free_global(g_shade_light);
        free_global(g_shade_dark);
        free_global(g_height_map.a);
        free_global(g_tile_map);
        free_global(g_track_image);
        free_global(g_depth_buf);
        free_global(g_random_table);
        return -1;
    }
    track_scene_draw(g_cfg_faster_preview == 0);
    unlock_surface();
    if (g_objects) {
        guest::op_delete(g_objects.get(), sizeof(TrackObject));
        g_objects = nullptr;
    }
    free_global(g_shade_light);
    free_global(g_shade_dark);
    free_global(g_height_map.a);
    free_global(g_tile_map);
    free_global(g_track_image);
    free_global(g_depth_buf);
    free_global(g_random_table);
    return 0;
}

namespace {
// Text from a guest C string (e.g. the format function's buffer) into s, drawn at (x, y).
void draw_cstr(MsString *s, uint32_t cstr, int x, int y, uint8_t colour, uint8_t shadow) {
    const char *t = &guest::at<char>(cstr);
    s->assign(t, static_cast<uint32_t>(std::strlen(t)));
    draw_text_at(s, x, y, colour, shadow);
}
uint32_t format(uint32_t fmt, int a) { return guest::call_cdecl(0x46f320, fmt, a); }
uint32_t format(uint32_t fmt, int a, int b) { return guest::call_cdecl(0x46f320, fmt, a, b); }
uint32_t format(uint32_t fmt, int a, int b, int c) { return guest::call_cdecl(0x46f320, fmt, a, b, c); }
uint32_t format_time(int t) {  // "%d%d.%d%d": seconds and hundredths of t frames
    return guest::call_cdecl(0x46f320, 0x49cdb0u, t / 1000 % 10, t / 100 % 10, t / 10 % 10, t % 10);
}
void fill_row(int row, int x, uint8_t value, int n) {
    guest::call_import(IMP_memset, g_pixels + static_cast<uint32_t>(g_pitch * row + x), value, n);
}
}  // namespace

// The race screen (0x4486c0): the scene into the background surface, then the HUD panel along the
// bottom (track name, record, lap counter, and per player the coloured frame, place or "F", lap and
// time boxes and the car), then the sky (where nothing was drawn) shaded by sixteen random light blobs.
void race_setup_view() {
    guest::Local<MsString> text, line;
    line->init();
    track_scene_draw(true);
    const int u = g_font_size, b = g_menu_border;
    int h = g_screen_h, w = g_screen_w;
    // the panel
    for (int row = h - b * 5 - u * 4; row < h; row++) {
        fill_row(row, 0, 0xf0, w);
        guest::call_import(IMP_memset, g_depth_buf + 2u * static_cast<uint32_t>(w * row), 0, w * 2);
    }
    const char *track = g_current_track >= 0 && g_current_track < g_selected_tracks.size()
                            ? g_selected_tracks[g_current_track].c_str()
                            : nullptr;
    const uint32_t slash = guest::call_import(IMP_strrchr, guest::addr(track), 0x5cu);
    text->init();
    draw_cstr(text.get(), slash + 1, guest::to_int(static_cast<float>(u) * 0.5f), h - b * 3 - u * 4, 0xf2, 0xf1);
    text->destroy();
    const int rt = g_record_time[0];
    draw_cstr(line.get(), format(0x49cd94u, rt / 100, rt / 10 % 10, rt % 10), guest::to_int(static_cast<float>(u) * 0.5f),
              h - (u + b) * 2, 0xf2, 0xf1);  // "TR: %d.%d%ds"
    {
        const char *laps = &guest::at<char>(format(0x49ccc4u, g_leader_laps + 1, g_race_laps));  // "%d/%d"
        line->assign(laps, static_cast<uint32_t>(std::strlen(laps)));
        const float fu = static_cast<float>(u);
        const int x = guest::to_int(static_cast<float>(w) * 0.25f -
                                    (static_cast<float>(static_cast<double>(line->size)) + 0.5f) * fu);
        draw_text_at(line.get(), x, h - (u + b) * 2, 0xf2, 0xf1);
    }
    for (int p = 0; p < g_player_count; p++) {
        uint8_t *car = g_car_states[p].raw;
        const int colour = 0xc5 + p * 8;
        const float fu = static_cast<float>(u);
        const int x0 = guest::to_int(static_cast<float>((p + 2) * w) * 0.125f);
        int row = h - (u + b) * 4;
        const int boxes_end = h - b * 3 - u * 2;
        // the player's frame in its colour: the upper part around the place / lap boxes, the lower part
        // under them
        for (; row < boxes_end; row++) {
            fill_row(row, x0 - b - guest::to_int(fu * -3.5f), static_cast<uint8_t>(colour - 4), (b + u * 3) * 2);
            fill_row(row, x0 - guest::to_int(fu * -5.5f), static_cast<uint8_t>(colour), u * 2);
        }
        for (; row < h - b; row++) {
            fill_row(row, x0 - b - guest::to_int(fu * -0.5f), static_cast<uint8_t>(colour - 4), u * 9 + b * 2);
            fill_row(row, x0 - guest::to_int(fu * -5.5f), static_cast<uint8_t>(colour), u * 2);
        }
        for (int r = h - b * 3 - u * 4; r < boxes_end; r++) {
            fill_row(r, x0 - guest::to_int(fu * -3.5f), 0xf1, u * 6);
            fill_row(b + u * 2 + r, x0 - guest::to_int(fu * -3.5f), 0xf1, u * 6);
        }
        // place, or "F" for a car short of fuel with laps to go
        const uint16_t laps = *reinterpret_cast<uint16_t *>(car + 0xd4);
        const uint16_t fuel = *reinterpret_cast<uint16_t *>(car + 0xe0);
        const uint32_t fuel_lap = static_cast<uint32_t>(g_race_options.b0a) * static_cast<uint32_t>(g_track_length);
        text->init();
        uint8_t place_colour;
        if (g_race_laps - laps < 2 || g_race_laps <= g_leader_laps ||
            guest::to_int(static_cast<float>(static_cast<double>(fuel_lap)) * 1.8f) <= static_cast<int>(fuel)) {
            const char *s = &guest::at<char>(format(0x49cdd8u, car[0] + 1));  // "%d"
            text->assign(s, static_cast<uint32_t>(std::strlen(s)));
            place_colour = 0xf5;
        } else {
            car[2] = 1;
            text->assign(&guest::at<char>(0x49cda4), 1);  // "F"
            place_colour = 0xf4;
        }
        draw_text_at(text.get(), u * 4 + x0, h - b * 3 - u * 4, place_colour, 0);
        text->destroy();
        // lap number and lap time (the best lap once finished)
        draw_cstr(line.get(), format(0x49cda8u, laps), x0 + u * 6, h - b * 3 - u * 4, car[2] < 2 ? 0xf2 : 0xf5, 0);  // " %2d"
        uint8_t time_colour;
        uint32_t time;
        if (car[2] < 2) {
            time = format_time(*reinterpret_cast<int32_t *>(car + 0x14));
            time_colour = p == g_unknown_4a4798 ? (p == g_unknown_4a479c ? 0xf4 : 0xf7) : 0xf2;
        } else {
            time = format_time(*reinterpret_cast<uint16_t *>(car + 0xdc));
            time_colour = p == g_unknown_4a4798 ? 0xf4 : 0xf5;
        }
        draw_cstr(line.get(), time, x0 + u * 4, h - (u + b) * 2, time_colour, 0);
        draw_car_preview(static_cast<uint32_t>(p), g_race_models[p], x0 - guest::to_int(fu * -1.75f), h - b * 4 - u * 3, 120.0f);
        w = g_screen_w;
        h = g_screen_h;
    }
    // the sky: light blobs at random places
    struct { float x, y; } blob[16];
    for (auto &bl : blob) {
        const uint8_t i = static_cast<uint8_t>(g_random_index + 1);
        g_random_index = static_cast<uint8_t>(g_random_index + 2);
        bl.x = (guest::at<float>(g_random_table + 4u * i) * 1.4f - 0.2f) * static_cast<float>(w);
        bl.y = (1.2f - guest::at<float>(g_random_table + 4u * g_random_index) * 1.2f) * static_cast<float>(h);
    }
    for (int y = 0; y < h; y++) {
        uint8_t *pixels = &guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(g_pitch * y));
        uint8_t *image = &guest::at<uint8_t>(g_track_image + static_cast<uint32_t>(w * y));
        const uint16_t *depth = &guest::at<uint16_t>(g_depth_buf + 2u * static_cast<uint32_t>(w * y));
        for (int x = 0; x < w; x++) {
            if (depth[x] != 0xffff) continue;
            float light = 0.0f;
            for (const auto &bl : blob) {
                const float dx = static_cast<float>(x) - bl.x, dy = static_cast<float>(y) - bl.y;
                light = light + 20000.0f / ((dx * 0.5f * dx + 5000.0f) + dy * 10.0f * dy);
            }
            if (6.99f < light) light = 6.99f;
            pixels[x] = static_cast<uint8_t>(guest::to_int(light) - 8);
            image[x] = 0xf8;
        }
    }
    unlock_surface();
    line->destroy();
}
