// SDL 1.2, called through the original's import table (the runtime provides SDL in guest form).
// Surfaces stay guest objects; rewritten code refers to them by guest address.
#pragma once
#include "bridge/guest.h"

using SdlSurfaceRef = uint32_t;  // SDL_Surface* in guest memory

struct SdlRect {
    int16_t x, y;
    uint16_t w, h;
};
// SDL_Rect from ints, truncated to the 16-bit fields like the original's stores
inline SdlRect rect(int x, int y, int w, int h) {
    return {static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<uint16_t>(w), static_cast<uint16_t>(h)};
}

struct SdlColor {
    uint8_t r, g, b, unused;
};
// the parts of SDL_Surface / SDL_PixelFormat the game touches
struct SdlPixelFormat {
    uint32_t palette;
    uint8_t bits_per_pixel;
    uint8_t bytes_per_pixel;
};
struct SdlSurface {
    uint32_t flags;
    ptr32<SdlPixelFormat> format;
    int32_t w, h;
    uint16_t pitch;
    uint16_t pad;
    uint32_t pixels;
    int32_t offset;
};

enum : uint32_t { SDL_HWPALETTE = 0x20000000, SDL_FULLSCREEN = 0x80000000 };
enum : uint32_t { SDL_LOGPAL = 1, SDL_PHYSPAL = 2 };

namespace sdl {
inline SdlSurfaceRef GetVideoSurface() { return guest::call_import(IMP_SDL_GetVideoSurface); }
inline SdlSurfaceRef SetVideoMode(int w, int h, int bpp, uint32_t flags) {
    return guest::call_import(IMP_SDL_SetVideoMode, w, h, bpp, flags);
}
inline SdlSurfaceRef CreateRGBSurface(uint32_t flags, int w, int h, int bpp) {
    return guest::call_import(IMP_SDL_CreateRGBSurface, flags, w, h, bpp, 0u, 0u, 0u, 0u);
}
inline void FreeSurface(SdlSurfaceRef s) { guest::call_import(IMP_SDL_FreeSurface, s); }
inline int FillRect(SdlSurfaceRef s, SdlRect *r, uint32_t color) {
    return static_cast<int>(guest::call_import(IMP_SDL_FillRect, s, r, color));
}
inline int UpperBlit(SdlSurfaceRef src, SdlRect *sr, SdlSurfaceRef dst, SdlRect *dr) {
    return static_cast<int>(guest::call_import(IMP_SDL_UpperBlit, src, sr, dst, dr));
}
inline int LockSurface(SdlSurfaceRef s) { return static_cast<int>(guest::call_import(IMP_SDL_LockSurface, s)); }
inline void UnlockSurface(SdlSurfaceRef s) { guest::call_import(IMP_SDL_UnlockSurface, s); }
inline int SetPalette(SdlSurfaceRef s, uint32_t flags, SdlColor *colors, int first, int n) {
    return static_cast<int>(guest::call_import(IMP_SDL_SetPalette, s, flags, colors, first, n));
}
inline SdlSurface *surface(SdlSurfaceRef s) { return &guest::at<SdlSurface>(s); }
}  // namespace sdl
