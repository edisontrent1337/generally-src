// 8-bit palettized video: the window surface, an off-screen back buffer that everything is drawn into,
// and a second off-screen surface holding the static background (the track image during a race).
#pragma once
#include "bridge/msvc.h"
#include "orig/sdl.h"

enum Surface { SURF_SCREEN = 0, SURF_BACK = 1, SURF_BACKGROUND = 2 };

GLOBAL(SdlSurfaceRef, g_back, 0x4a4804);
GLOBAL(SdlSurfaceRef, g_background, 0x4a4c08);
GLOBAL(SdlColor[256], g_palette, 0x4a4808);
// set while a surface is locked for direct pixel access
GLOBAL(int32_t, g_pitch, 0x4a4800);
GLOBAL(uint32_t, g_pixels, 0x4a4c0c);
GLOBAL(SdlSurfaceRef, g_locked, 0x4a4c10);
GLOBAL(int32_t, g_screen_w, 0x4a4d3c);
GLOBAL(int32_t, g_screen_h, 0x4a4d1c);

// a rectangle as corner coordinates
struct Box {
    int32_t x1, y1, x2, y2;
};

bool video_init(bool windowed);
bool video_set_mode(int w, int h, bool windowed);
SdlSurfaceRef surface(int which);
void lock_surface(int which);
void unlock_surface();
bool blit_surface(int src, int dst);
void show_background();
void screenshot_save(const MsString *path, uint32_t w, uint32_t h);
void screenshot_take(uint32_t w, uint32_t h);
bool surface_has_alpha(SdlSurfaceRef s);
SdlSurfaceRef surface_fit(SdlSurfaceRef src, int w, int h);
bool blit_background(int16_t w, int16_t h, int16_t x, int16_t y);
void fill_rect_corners(uint32_t color, int16_t x1, int16_t y1, int16_t x2, int16_t y2);
void fill_rect(uint32_t color, int16_t x, int16_t y, uint16_t w, uint16_t h);
void fill_box(uint32_t color, const Box *box);
void palette_store(uint32_t *rgb);
void palette_load(const uint32_t *rgb);
void palette_reset();
bool display_mode_greater(const uint32_t *a, const uint32_t *b);

struct DisplayMode {
    int32_t w, h;
};
MsVector<DisplayMode> *display_modes(MsVector<DisplayMode> *out);
