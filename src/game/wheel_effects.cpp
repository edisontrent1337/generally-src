// A wheel's spray over the drawn race frame (0x40ef10).
#include <cstdint>

#include "game/fmath.h"
#include "game/menu.h"
#include "game/race_state.h"
#include "game/render.h"
#include "game/video.h"

// Projects wheel w of car p and walks the screen disc of radius `size` (world units) around it, row by
// row (half widths from guest_sqrtf), clipped to the screen. Per pixel, by the wheel's surface (car +0xe4):
//   3 (water): one random number per pixel; where it is below `strength` and the depth buffer puts the
//      pixel within -3 .. +1 of the wheel's depth (in depth units), a light pixel (>= 0x90) becomes the
//      spray colour 0x94 + its low two bits, and the unshaded track image there 0x94
//   2 (dust): where the pixel is not in front of the wheel, its unshaded colour is looked up among dark
//      ramp entries 1..7; the first match i moves the pixel trunc((8 - i) * strength) steps along the
//      ramp, scaling its offset between dark[i] and light[i] onto the new entry's range
void wheel_spray(int p, int w, float size, float strength) {
    const uint8_t *car = g_car_states[p].raw;
    const int idx = g_race_models[p][0xbd] + w;
    const float *at = reinterpret_cast<const float *>(car + 0xe8 + idx * 0xc);
    const Vec3 eye = g_view_eye, fwd = g_view_forward, down = g_view_down, right = g_view_right;
    const float dx = at[0] - eye.x, dy = at[1] - eye.y, dz = at[2] - eye.z;
    const float depth = (dy * fwd.y + dx * fwd.x) + dz * fwd.z;
    if (!(depth > 0.0001f)) return;
    const float scale = g_start_light_depth;  // projection scale (pixels at distance 1)
    const float sx = (((dy * right.y + dx * right.x) + dz * right.z) / depth + 0.5f) * scale;
    const float sy = (((dy * down.y + dx * down.x) + dz * down.z) / depth + g_screen_aspect * 0.5f) * scale;
    const float r = scale / depth * size;
    const float near = depth - 0.5f;
    int y = guest::to_int((sy - r) + 1.0f);
    if (y < 0) y = 0;
    int y_end = guest::to_int((sy + r) + 1.0f);
    if (y_end > g_screen_h) y_end = g_screen_h;
    if (y >= y_end) return;
    const int width = g_screen_w;
    const float sx1 = sx + 1.0f, r2 = r * r;
    int row = width * y;
    uint32_t line = g_pixels + g_pitch * static_cast<uint32_t>(y);
    for (; y < y_end; y++) {
        const float fy = static_cast<float>(y) - sy;
        const float half = guest_sqrtf(r2 - fy * fy);
        int x = guest::to_int((sx - half) + 1.0f);
        if (x < 0) x = 0;
        int x_end = guest::to_int(sx1 + half);
        if (x_end > width) x_end = width;
        const uint16_t *zp = &guest::at<uint16_t>(g_depth_buf + static_cast<uint32_t>(row + x) * 2);
        for (; x < x_end; x++, zp++) {
            uint8_t &pix = guest::at<uint8_t>(line + static_cast<uint32_t>(x));
            uint8_t &plain = guest::at<uint8_t>(g_track_image + static_cast<uint32_t>(row + x));
            const uint8_t surface = car[0xe4 + w];
            if (surface == 3) {
                g_random_index = static_cast<uint8_t>(g_random_index + 1);
                if (strength > guest::at<float>(g_random_table + g_random_index * 4u)) {
                    const float s = g_depth_scale;
                    if (guest::to_int((near - 3.0f) / s) <= *zp && *zp < guest::to_int((near + 1.0f) / s) && pix >= 0x90) {
                        pix = static_cast<uint8_t>((pix & 3) + 0x94);
                        plain = 0x94;
                    }
                    continue;
                }
            }
            if (surface == 2 && guest::to_int(near / g_depth_scale) <= *zp) {
                const uint8_t *dark = &guest::at<uint8_t>(g_shade_dark), *light = &guest::at<uint8_t>(g_shade_light);
                for (int i = 1; i < 8; i++) {
                    if (plain != dark[i]) continue;
                    const int k = guest::to_int(static_cast<float>(8 - i) * strength);
                    if (k > 0) {
                        const uint8_t base = dark[k + i];
                        pix = static_cast<uint8_t>((static_cast<int>(light[k + i]) - base) * (static_cast<int>(pix) - dark[i]) /
                                                       (static_cast<int>(light[i]) - dark[i]) +
                                                   base);
                        plain = dark[k + i];
                    }
                    break;
                }
            }
        }
        row += width;
        line += g_pitch;
    }
}
