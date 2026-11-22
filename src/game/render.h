// The race view's software renderer: 8-bit pixels into the locked back surface with a 16-bit depth buffer.
#pragma once
#include "bridge/guest.h"
#include "game/vec3.h"

GLOBAL(uint32_t, g_depth_buf, 0x4a4784);       // uint16 per screen pixel (row length g_screen_w)
GLOBAL(uint32_t, g_disc_widths, 0x4a4f08);     // float[100]: half width of a unit disc, top to bottom
GLOBAL(float, g_depth_scale, 0x4a402c);        // view distance per depth buffer unit
GLOBAL(uint32_t, g_track_image, 0x4a476c);     // the track as drawn without shading (row length g_screen_w)
GLOBAL(float, g_start_light_depth, 0x4a4768);
GLOBAL(uint32_t, g_shade_light, 0x4a4748);     // uint8_t[48], malloc'ed by the track loader
GLOBAL(uint32_t, g_shade_dark, 0x4a478c);      // uint8_t[48]
GLOBAL(uint32_t, g_random_table, 0x4a47e4);    // float[256], rand() / 32768
GLOBAL(uint8_t, g_random_index, 0x4a475c);     // next entry of g_random_table (wraps at 256)
GLOBAL(uint32_t, g_tile_map, 0x4a4764);        // uint8_t[512][512]: terrain type of each track tile
GLOBAL(float, g_tile_size, 0x4a4778);          // world units per tile
GLOBAL(float, g_screen_aspect, 0x4a4750);      // screen height / width
GLOBAL(uint32_t, g_pixel_fn, 0x4a4738);        // per-pixel function of triangle_for_each_pixel (e.g. water_pixel)
// the race camera: screen axes (down, right), view direction and eye position
GLOBAL(Vec3, g_view_down, 0x4aaff8);
GLOBAL(Vec3, g_view_forward, 0x4ab004);
GLOBAL(Vec3, g_view_eye, 0x4ab010);
GLOBAL(Vec3, g_view_right, 0x4ab01c);

void draw_disc(uint8_t color, bool always, float x, float y, float z, float radius);
void frame_present();
void undraw_disc(float x, float y, float z, float radius);
void undraw_triangle(float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz);
void shade_tables_init();
void track_redraw_tile(int tx, int tz);
void triangle_for_each_pixel(uint32_t color, const float a[3], const float b[3], const float c[3], const float wa[3],
                             const float wb[3], const float wc[3]);
void terrain_pixel(float nx, float ny, float nz, float qx, float qy, float qz, int x, int y);
void flat_pixel(float nx, float ny, float nz, float qx, float qy, float qz, int x, int y, uint8_t color);
void water_pixel(float nx, float ny, float nz, float qx, float qy, float qz, int x, int y);
void car_preview_draw(uint32_t colour_base, const uint8_t *model, int x, int y, const float view[9], float dist);
void shaded_triangle(uint8_t colour, bool always, float x0, float y0, float z0, float x1, float y1, float z1, float x2,
                     float y2, float z2, float s0, float s1, float s2);
void box_draw(const Vec3 &c, const Vec3 &a, const Vec3 &b, const Vec3 &h);
// The cars, crews and ghost in the race view, far to near (0x40f7f0).
void race_view_draw_sorted();
void box_draw_pixels(uint8_t colour, const Vec3 &c, const Vec3 &a, const Vec3 &b, const Vec3 &h);
void wheel_spray(int p, int w, float size, float strength);
