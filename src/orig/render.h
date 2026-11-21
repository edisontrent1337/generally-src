// Original functions of the 3D renderer that rewritten code calls.
#pragma once
#include "game/driver.h"
#include "game/render.h"

// The menus' car preview (car_preview_draw) seen along the fixed menu view axes; scale = distance.
inline void draw_car_preview(uint32_t colour_base, const void *car, int x, int y, float scale) {
    static const float kView[9] = {-0.6396f, -0.4264f, -0.6396f, 0.0f, 0.8321f, -0.5547f, 0.6396f, -0.4264f, -0.6396f};
    car_preview_draw(colour_base, static_cast<const uint8_t *>(car), x, y, kView, scale);
}
