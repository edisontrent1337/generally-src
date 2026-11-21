#include "bridge/guest.h"
#include "game/video.h"

using namespace abi;

REWRITE(46c490, "video_init", 2, 0, RET_AL) { ret_bool(c, video_init((arg(c, 1) & 0xff) != 0)); }
REWRITE(46c710, "video_set_mode", 2, 0, RET_AL) {
    ret_bool(c, video_set_mode(static_cast<int>(ECX), static_cast<int>(EDX), (arg(c, 1) & 0xff) != 0));
}
REWRITE(46cd30, "lock_surface", 0, 0, RET_NONE) { lock_surface(static_cast<int>(ECX)); }
REWRITE(46cd90, "unlock_surface", 0, 0, RET_NONE) { unlock_surface(); }
REWRITE(46c8d0, "blit_surface", 0, 0, RET_AL) { ret_bool(c, blit_surface(static_cast<int>(ECX), static_cast<int>(EDX))); }
REWRITE(46c950, "blit_background", 6, 0, RET_AL) {
    ret_bool(c, blit_background(static_cast<int16_t>(arg(c, 1)), static_cast<int16_t>(arg(c, 2)),
                                static_cast<int16_t>(arg(c, 4)), static_cast<int16_t>(arg(c, 5))));
}
REWRITE(46c7c0, "fill_rect_corners", 3, 0, RET_NONE) {
    fill_rect_corners(ECX, static_cast<int16_t>(EDX), static_cast<int16_t>(arg(c, 0)), static_cast<int16_t>(arg(c, 1)),
                      static_cast<int16_t>(arg(c, 2)));
}
REWRITE(46c810, "fill_rect", 3, 0, RET_NONE) {
    fill_rect(ECX, static_cast<int16_t>(EDX), static_cast<int16_t>(arg(c, 0)), static_cast<uint16_t>(arg(c, 1)),
              static_cast<uint16_t>(arg(c, 2)));
}
REWRITE(46c860, "fill_box", 0, 0, RET_NONE) { fill_box(ECX, edx<Box>(c)); }
REWRITE(46cdd0, "palette_store", 0, 0, RET_NONE) { palette_store(ecx<uint32_t>(c)); }
REWRITE(46ce70, "palette_load", 0, 0, RET_NONE) { palette_load(ecx<uint32_t>(c)); }
REWRITE(46c550, "display_mode_greater", 2, 0, RET_AL) {
    ret_bool(c, display_mode_greater(argp<uint32_t>(c, 0), argp<uint32_t>(c, 1)));
}
REWRITE(46c570, "display_modes", 0, 0, RET_EAX) { ret_ptr(c, display_modes(ecx<MsVector<DisplayMode>>(c))); }
REWRITE(47d250, "show_background", 0, 0, RET_NONE) { show_background(); }
REWRITE(46cc70, "screenshot_save", 1, 0, RET_NONE) { screenshot_save(ecx<MsString>(c), EDX, arg(c, 0)); }
REWRITE(401bf0, "surface_has_alpha", 0, 0, RET_AL) { ret_bool(c, surface_has_alpha(ECX)); }
REWRITE(401d10, "surface_fit", 1, 0, RET_EAX) { ret(c, surface_fit(ECX, static_cast<int>(EDX), static_cast<int>(arg(c, 0)))); }
REWRITE(471e50, "screenshot_take", 0, 0, RET_NONE) { screenshot_take(ECX, EDX); }
REWRITE(46b580, "palette_reset", 0, 0, RET_NONE) { palette_reset(); }
