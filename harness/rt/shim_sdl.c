// SDL 1.2 API (the subset GeneRally imports) implemented on SDL2, with all SDL 1.2 structures in guest
// memory using the original 32-bit layouts:
//   SDL_Surface     0 flags, 4 format*, 8 w, c h, 10 pitch(u16), 14 pixels*, 18 offset, 1c hwdata,
//                   20 clip_rect{x s16, y s16, w u16, h u16}, 28 unused1, 2c locked, 30 map*, 34 format_version, 38 refcount
//   SDL_PixelFormat 0 palette*, 4 BitsPerPixel, 5 BytesPerPixel, 6..9 Rloss.., a..d Rshift.., 10 Rmask, 14 Gmask,
//                   18 Bmask, 1c Amask, 20 colorkey, 24 alpha
//   SDL_Palette     0 ncolors, 4 colors* (SDL_Color = r,g,b,unused)
//   SDL_Event       keyboard: 0 type, 1 which, 2 state, 4 scancode, 8 sym, c mod, 10 unicode (20 bytes)
//   SDL_AudioSpec   0 freq, 4 format(u16), 6 channels, 7 silence, 8 samples(u16), c size, 10 callback, 14 userdata
//   SDL_AudioCVT    0 needed, 4 src_format, 6 dst_format, 8 rate_incr(double), 10 buf*, 14 len, 18 len_cvt,
//                   1c len_mult, 20 len_ratio(double), 28 filters[10], 50 filter_index
#include "rt.h"
#include "plat.h"
#include "check.h"
#include <SDL.h>
#include <string.h>
#include <math.h>

#define SHIM(n) static void n(CPU *c)

#define SDL1_SWSURFACE 0x00000000u
#define SDL1_SRCCOLORKEY 0x00001000u
#define SDL1_SRCALPHA 0x00010000u
#define SDL1_PREALLOC 0x01000000u
#define SDL1_HWPALETTE 0x20000000u
#define SDL1_FULLSCREEN 0x80000000u

// ------------------------------------------------------------------ surfaces
#define S_FLAGS(s) G32((s) + 0x00)
#define S_FMT(s) G32((s) + 0x04)
#define S_W(s) ((int32_t)G32((s) + 0x08))
#define S_H(s) ((int32_t)G32((s) + 0x0c))
#define S_PITCH(s) G16((s) + 0x10)
#define S_PIX(s) G32((s) + 0x14)
#define F_PAL(f) G32((f) + 0x00)
#define F_BPP(f) G8((f) + 0x04)
#define F_BYPP(f) G8((f) + 0x05)

static uint32_t alloc_format(int bpp, uint32_t rm, uint32_t gm, uint32_t bm, uint32_t am) {
    uint32_t f = gcalloc(1, 0x28);
    G8(f + 4) = (uint8_t)bpp;
    G8(f + 5) = (uint8_t)((bpp + 7) / 8);
    if (bpp > 8) {
        uint32_t m[4] = {rm, gm, bm, am};
        for (int i = 0; i < 4; i++) {
            int shift = 0, loss = 8;
            if (m[i]) {
                uint32_t x = m[i];
                while (!(x & 1)) { x >>= 1; shift++; }
                while (x & 1) { x >>= 1; loss--; }
            }
            G8(f + 6 + i) = (uint8_t)loss;
            G8(f + 0xa + i) = (uint8_t)shift;
            G32(f + 0x10 + 4 * i) = m[i];
        }
    } else {
        // palettized: SDL 1.2 gives 8-bit surfaces a 256-entry 3-3-2 "dither" palette
        int n = 1 << bpp;
        uint32_t pal = gcalloc(1, 8);
        uint32_t cols = gcalloc((uint32_t)n, 4);
        G32(pal) = (uint32_t)n;
        G32(pal + 4) = cols;
        for (int i = 0; i < n && bpp == 8; i++) {
            int r = i & 0xe0; r |= r >> 3 | r >> 6;
            int g = (i << 3) & 0xe0; g |= g >> 3 | g >> 6;
            int b = i & 0x3; b |= b << 2; b |= b << 4;
            G8(cols + 4 * i) = (uint8_t)r; G8(cols + 4 * i + 1) = (uint8_t)g; G8(cols + 4 * i + 2) = (uint8_t)b;
        }
        G8(f + 6) = G8(f + 7) = G8(f + 8) = G8(f + 9) = 8;
        F_PAL(f) = pal;
    }
    G8(f + 0x24) = 255;
    return f;
}

static uint32_t copy_format(uint32_t src) {
    uint32_t f = gcalloc(1, 0x28);
    memcpy(GP(f), GP(src), 0x28);
    if (F_PAL(src)) {
        uint32_t sp = F_PAL(src), n = G32(sp);
        uint32_t pal = gcalloc(1, 8), cols = gcalloc(n, 4);
        G32(pal) = n;
        G32(pal + 4) = cols;
        memcpy(GP(cols), GP(G32(sp + 4)), 4 * n);
        F_PAL(f) = pal;
    }
    return f;
}

static void free_format(uint32_t f) {
    if (!f) return;
    if (F_PAL(f)) { gfree(G32(F_PAL(f) + 4)); gfree(F_PAL(f)); }
    gfree(f);
}

static uint32_t new_surface(uint32_t flags, int w, int h, uint32_t fmt) {
    uint32_t s = gcalloc(1, 0x3c);
    S_FLAGS(s) = flags;
    S_FMT(s) = fmt;
    G32(s + 8) = (uint32_t)w;
    G32(s + 0xc) = (uint32_t)h;
    uint32_t pitch = ((uint32_t)w * F_BYPP(fmt) + 3) & ~3u;
    S_PITCH(s) = (uint16_t)pitch;
    S_PIX(s) = (w > 0 && h > 0) ? gcalloc(1, pitch * (uint32_t)h) : 0;
    G16(s + 0x24) = (uint16_t)w;
    G16(s + 0x26) = (uint16_t)h;
    G32(s + 0x38) = 1;
    return s;
}

static void free_surface(uint32_t s) {
    if (!s) return;
    if (--G32(s + 0x38) > 0) return;
    if (!(S_FLAGS(s) & SDL1_PREALLOC)) gfree(S_PIX(s));
    free_format(S_FMT(s));
    gfree(s);
}

// ------------------------------------------------------------------ pixel conversion helpers
static inline void get_rgba(uint32_t fmt, uint32_t px, int *r, int *g, int *b, int *a) {
    if (F_BPP(fmt) == 8) {
        uint32_t col = G32(F_PAL(fmt) + 4) + 4 * (px & 0xff);
        *r = G8(col); *g = G8(col + 1); *b = G8(col + 2); *a = 255;
        return;
    }
    int v[4];
    for (int i = 0; i < 4; i++) {
        uint32_t m = G32(fmt + 0x10 + 4 * i);
        int shift = G8(fmt + 0xa + i), loss = G8(fmt + 6 + i);
        int x = (int)((px & m) >> shift);
        v[i] = (x << loss) | ((x << loss) >> (8 - loss));
        if (!m) v[i] = 255;
    }
    if (!G32(fmt + 0x1c)) v[3] = 255;
    *r = v[0]; *g = v[1]; *b = v[2]; *a = v[3];
}
static inline uint32_t map_rgba(uint32_t fmt, int r, int g, int b, int a) {
    int v[4] = {r, g, b, a};
    uint32_t px = 0;
    for (int i = 0; i < 4; i++)
        px |= ((uint32_t)(v[i] >> G8(fmt + 6 + i)) << G8(fmt + 0xa + i)) & G32(fmt + 0x10 + 4 * i);
    return px;
}
static inline uint32_t get_px(uint32_t addr, int bypp) {
    switch (bypp) {
    case 1: return G8(addr);
    case 2: return G16(addr);
    case 3: return G8(addr) | (G8(addr + 1) << 8) | (G8(addr + 2) << 16);
    default: return G32(addr);
    }
}
static inline void put_px(uint32_t addr, int bypp, uint32_t v) {
    switch (bypp) {
    case 1: G8(addr) = (uint8_t)v; break;
    case 2: G16(addr) = (uint16_t)v; break;
    case 3: G8(addr) = (uint8_t)v; G8(addr + 1) = (uint8_t)(v >> 8); G8(addr + 2) = (uint8_t)(v >> 16); break;
    default: G32(addr) = v; break;
    }
}

// SDL_FindColor: nearest palette entry by squared RGB distance
static uint8_t find_color(uint32_t pal, int r, int g, int b) {
    uint32_t n = G32(pal), cols = G32(pal + 4);
    unsigned best = ~0u;
    uint8_t pix = 0;
    for (uint32_t i = 0; i < n; i++) {
        int rd = G8(cols + 4 * i) - r, gd = G8(cols + 4 * i + 1) - g, bd = G8(cols + 4 * i + 2) - b;
        unsigned d = (unsigned)(rd * rd + gd * gd + bd * bd);
        if (d < best) { pix = (uint8_t)i; if (!d) break; best = d; }
    }
    return pix;
}

static uint32_t pal_hash(uint32_t pal) {
    uint32_t n = G32(pal), h = 2166136261u;
    const uint8_t *p = GP(G32(pal + 4));
    for (uint32_t i = 0; i < 4 * n; i++) h = (h ^ p[i]) * 16777619u;
    return h ^ n;
}

// 3-3-2 -> destination palette (SDL 1.2 MapNto1), cached per palette contents
static uint32_t m332_hash;
static uint8_t m332[256];
static const uint8_t *map332(uint32_t dpal) {
    uint32_t h = pal_hash(dpal);
    if (h != m332_hash) {
        for (int i = 0; i < 256; i++) {
            int r = i & 0xe0; r |= r >> 3 | r >> 6;
            int g = (i << 3) & 0xe0; g |= g >> 3 | g >> 6;
            int b = i & 0x3; b |= b << 2; b |= b << 4;
            m332[i] = find_color(dpal, r, g, b);
        }
        m332_hash = h;
    }
    return m332;
}
#define RGB332(r, g, b) ((((r) >> 5) << 5) | (((g) >> 5) << 2) | ((b) >> 6))

// palette -> palette (SDL 1.2 Map1to1); returns NULL when identical
static uint32_t m11_hs, m11_hd;
static uint8_t m11[256];
static const uint8_t *map1to1(uint32_t spal, uint32_t dpal) {
    uint32_t sn = G32(spal), dn = G32(dpal);
    if (sn <= dn && !memcmp(GP(G32(spal + 4)), GP(G32(dpal + 4)), 4 * sn)) return NULL;
    uint32_t hs = pal_hash(spal), hd = pal_hash(dpal);
    if (hs != m11_hs || hd != m11_hd) {
        uint32_t cols = G32(spal + 4);
        for (uint32_t i = 0; i < sn && i < 256; i++)
            m11[i] = find_color(dpal, G8(cols + 4 * i), G8(cols + 4 * i + 1), G8(cols + 4 * i + 2));
        m11_hs = hs; m11_hd = hd;
    }
    return m11;
}

// copy/convert/blend a w*h block. `blend` = use per-pixel alpha of a 32-bit source.
static void lower_blit(uint32_t src, int sx, int sy, uint32_t dst, int dx, int dy, int w, int h, int blend) {
    uint32_t sf = S_FMT(src), df = S_FMT(dst);
    int sb = F_BYPP(sf), db = F_BYPP(df);
    uint32_t sp = S_PITCH(src), dp = S_PITCH(dst);
    uint32_t s0 = S_PIX(src) + (uint32_t)sy * sp + (uint32_t)sx * (uint32_t)sb;
    uint32_t d0 = S_PIX(dst) + (uint32_t)dy * dp + (uint32_t)dx * (uint32_t)db;
    if (sb == 1 && db == 1) {
        const uint8_t *map = map1to1(F_PAL(sf), F_PAL(df));
        for (int y = 0; y < h; y++) {
            uint8_t *s = GP(s0 + (uint32_t)y * sp), *d = GP(d0 + (uint32_t)y * dp);
            if (!map) memmove(d, s, (size_t)w);
            else for (int x = 0; x < w; x++) d[x] = map[s[x]];
        }
        return;
    }
    if (db == 1) {
        const uint8_t *map = map332(F_PAL(df));
        uint32_t dcols = G32(F_PAL(df) + 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int r, g, b, a;
                get_rgba(sf, get_px(s0 + (uint32_t)y * sp + (uint32_t)(x * sb), sb), &r, &g, &b, &a);
                uint32_t da = d0 + (uint32_t)y * dp + (uint32_t)x;
                if (blend) {
                    if (a == 0) continue;
                    if (a != 255) {
                        uint32_t dc = dcols + 4 * G8(da);
                        int dr = G8(dc), dg = G8(dc + 1), dbb = G8(dc + 2);
                        r = (((r - dr) * a + 255) >> 8) + dr;
                        g = (((g - dg) * a + 255) >> 8) + dg;
                        b = (((b - dbb) * a + 255) >> 8) + dbb;
                    }
                }
                G8(da) = map[RGB332(r, g, b)];
            }
        return;
    }
    // 32/24/16-bit destination
    int dst_has_alpha = G32(df + 0x1c) != 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int r, g, b, a;
            get_rgba(sf, get_px(s0 + (uint32_t)y * sp + (uint32_t)(x * sb), sb), &r, &g, &b, &a);
            uint32_t da = d0 + (uint32_t)y * dp + (uint32_t)(x * db);
            if (blend && a != 255) {
                if (a == 0) continue;
                int dr, dg, dbb, dA;
                get_rgba(df, get_px(da, db), &dr, &dg, &dbb, &dA);
                r = (((r - dr) * a + 255) >> 8) + dr;
                g = (((g - dg) * a + 255) >> 8) + dg;
                b = (((b - dbb) * a + 255) >> 8) + dbb;
                a = dA;
            }
            put_px(da, db, map_rgba(df, r, g, b, dst_has_alpha ? a : 255));
        }
}

static uint32_t convert_surface(uint32_t src, uint32_t fmt, uint32_t flags) {
    uint32_t d = new_surface(flags & (SDL1_SRCALPHA | SDL1_SRCCOLORKEY), S_W(src), S_H(src), copy_format(fmt));
    lower_blit(src, 0, 0, d, 0, 0, S_W(src), S_H(src), 0);
    if (flags & SDL1_SRCALPHA) S_FLAGS(d) |= SDL1_SRCALPHA;
    return d;
}

// ------------------------------------------------------------------ host video state
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static uint32_t screen;  // guest SDL_Surface of the display
static int scr_w, scr_h;
static char caption[256] = "GeneRally";
static uint32_t *rgb_buf;
static uint64_t t0_us;

// ------------------------------------------------------------------ low-latency presentation
#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>
// With vsync, CAMetalLayer keeps up to 3 drawables queued (2-3 frames of input delay). 2 is enough here:
// the game needs ~1 ms per frame.
static void renderer_low_latency(void) {
    const char *e = getenv("GRX_DRAWABLES");   // opt-in: GRX_DRAWABLES=2 (less input delay, less slack)
    if (!e) return;
    void *layer = SDL_RenderGetMetalLayer(ren);
    if (!layer) { rt_log("no metal layer"); return; }
    ((void (*)(void *, SEL, unsigned long))objc_msgSend)(layer, sel_registerName("setMaximumDrawableCount:"), (unsigned long)atoi(e));
    unsigned long n = ((unsigned long (*)(void *, SEL))objc_msgSend)(layer, sel_registerName("maximumDrawableCount"));
    rt_log("metal layer: maximumDrawableCount %lu", n);
}
#else
static void renderer_low_latency(void) {}
#endif

// frame clock: a phase-locked 60 Hz (display rate) grid fitted to the frame start times, so the time used for
// interpolation advances by exactly one refresh period per displayed frame even when the frame loop wakes up
// unevenly (vsync / compositor timing).
static double fc_period = 1000.0 / 60.0;  // ms

static int perf_ticks;

// every 5 s (GRX_PERF=0 disables): log frames, ms spent in SDL_Delay and in present
static int perf_on = -1;
static uint64_t perf_t0, perf_delay_us, perf_present_us;
static int perf_frames;
static double perf_last_ms, perf_min_iv = 1e9, perf_max_iv;
static int perf_slow;
static void perf_tick(void) {
    if (perf_on < 0) { const char *e = getenv("GRX_PERF"); perf_on = !(e && e[0] == '0'); }
    if (!perf_on) return;
    uint64_t now = plat_ticks_us();
    if (!perf_t0) perf_t0 = now;
    perf_frames++;
    double nm = now / 1000.0;
    if (perf_last_ms > 0) {
        double iv = nm - perf_last_ms;
        if (iv < perf_min_iv) perf_min_iv = iv;
        if (iv > perf_max_iv) perf_max_iv = iv;
        if (iv > fc_period * 1.5 && iv < 400) {
            perf_slow++;
            static int logged;
            if (logged < 200 && perf_last_ms > 0) {
                logged++;
                rt_log("hitch at %.0f ms: frame took %.1f ms (ticks %d, owed %u)", nm - t0_us / 1000.0, iv,
                       perf_ticks, G32(0x4a4788));
            }
        }
    }
    perf_last_ms = nm;
    if (now - perf_t0 >= 5000000) {
        double s = (now - perf_t0) / 1e6;
        rt_log("perf: %.1f fps, %.1f ticks/s, frame %.1f..%.1f ms, %d late, delay %.0f ms/s, present %.0f ms/s, game %.0f ms/s",
               perf_frames / s, perf_ticks / s, perf_min_iv, perf_max_iv, perf_slow,
               perf_delay_us / 1e3 / s, perf_present_us / 1e3 / s,
               (s * 1e6 - perf_delay_us - perf_present_us) / 1e3 / s);
        perf_min_iv = 1e9; perf_max_iv = 0; perf_slow = 0;
        perf_t0 = now; perf_frames = 0; perf_ticks = 0; perf_delay_us = perf_present_us = 0;
    }
}
static void present_inner(void);
static void present(void) {
    chk_extern("present");
    if (plat_is_det()) { present_inner(); return; }
    uint64_t t = plat_ticks_us();
    present_inner();
    static int jit = -1;
    if (jit < 0) { const char *e = getenv("GRX_JITTER"); jit = e ? atoi(e) : 0; }
    static int fakevs = -1;
    if (fakevs < 0) fakevs = getenv("GRX_FAKEVSYNC") != NULL;
    if (fakevs) {   // debug: block until the next 60 Hz boundary like a vsynced present
        uint64_t p = 16667, n = plat_ticks_us();
        uint64_t next = (n / p + 1) * p;
        while (plat_ticks_us() < next) plat_sleep_ms(0);
    }
    if (jit) plat_sleep_ms((unsigned)(rand() % (jit + 1)));   // debug: uneven wake-up after the refresh
    perf_present_us += plat_ticks_us() - t;
    perf_tick();
}
static void present_inner(void) {
    if (!screen || !tex) return;
    uint32_t pal = F_PAL(S_FMT(screen));
    uint32_t lut[256];
    uint32_t cols = G32(pal + 4);
    for (int i = 0; i < 256; i++)
        lut[i] = 0xff000000u | ((uint32_t)G8(cols + 4 * i) << 16) | ((uint32_t)G8(cols + 4 * i + 1) << 8) | G8(cols + 4 * i + 2);
    uint32_t pitch = S_PITCH(screen);
    for (int y = 0; y < scr_h; y++) {
        const uint8_t *s = GP(S_PIX(screen) + (uint32_t)y * pitch);
        uint32_t *d = rgb_buf + (size_t)y * (size_t)scr_w;
        for (int x = 0; x < scr_w; x++) d[x] = lut[s[x]];
    }
    SDL_UpdateTexture(tex, NULL, rgb_buf, scr_w * 4);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

// headless diagnostics: write the current 8-bit screen as a BMP
void sdl_debug_snapshot(const char *path) {
    if (!screen) { rt_log("snapshot: no screen"); return; }
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, scr_w, scr_h, 32, SDL_PIXELFORMAT_ARGB8888);
    uint32_t cols = G32(F_PAL(S_FMT(screen)) + 4);
    for (int y = 0; y < scr_h; y++)
        for (int x = 0; x < scr_w; x++) {
            uint8_t i = G8(S_PIX(screen) + (uint32_t)y * S_PITCH(screen) + (uint32_t)x);
            ((uint32_t *)s->pixels)[y * (s->pitch / 4) + x] =
                0xff000000u | ((uint32_t)G8(cols + 4 * i) << 16) | ((uint32_t)G8(cols + 4 * i + 1) << 8) | G8(cols + 4 * i + 2);
        }
    SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    rt_log("snapshot: %dx%d -> %s", scr_w, scr_h, path);
}

void sdl_shutdown(void) {
    if (win) { SDL_DestroyWindow(win); win = NULL; }
    SDL_Quit();
}

SHIM(sdl_Init) {
    uint32_t fl = ARG(0);
    Uint32 f2 = SDL_INIT_TIMER | SDL_INIT_EVENTS;
    if (fl & 0x10) f2 |= SDL_INIT_AUDIO;
    if (fl & 0x20) f2 |= SDL_INIT_VIDEO;
    if (fl & 0x200) f2 |= SDL_INIT_JOYSTICK;
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    int r = SDL_Init(f2);
    if (r) rt_log("SDL_Init failed: %s", SDL_GetError());
    t0_us = plat_ticks_us();
    ret_c(c, (uint32_t)r);
}
SHIM(sdl_Quit) { sdl_shutdown(); ret_c(c, 0); }
SHIM(sdl_GetTicks) { ret_c(c, (uint32_t)((plat_ticks_us() - t0_us) / 1000)); }
SHIM(sdl_Delay) {
    uint64_t t = plat_ticks_us();
    plat_sleep_ms(ARG(0));
    perf_delay_us += plat_ticks_us() - t;
    ret_c(c, 0);
}
SHIM(sdl_WM_SetCaption) {
    if (ARG(0)) snprintf(caption, sizeof caption, "%s", GS(ARG(0)));
    if (win) SDL_SetWindowTitle(win, caption);
    ret_c(c, 0);
}
SHIM(sdl_ShowCursor) {
    int t = (int)ARG(0);
    int prev = SDL_ShowCursor(-1);
    if (t >= 0) SDL_ShowCursor(t ? SDL_ENABLE : SDL_DISABLE);
    ret_c(c, (uint32_t)prev);
}

// SDL_SetVideoMode(w, h, bpp, flags): an 8-bit palettized screen shown in a scaled window
SHIM(sdl_SetVideoMode) {
    int w = (int)ARG(0), h = (int)ARG(1);
    uint32_t flags = ARG(3);
    if (w == 0 || h == 0) { w = scr_w; h = scr_h; }
    if (!w || !h) { w = 640; h = 480; }
    if (!win) {
        SDL_DisplayMode dm;
        int scale = 1;
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
            while ((scale + 1) * w <= dm.w * 9 / 10 && (scale + 1) * h <= dm.h * 9 / 10) scale++;
        win = SDL_CreateWindow(caption, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w * scale, h * scale,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
        if (!win) { rt_log("SDL_CreateWindow: %s", SDL_GetError()); ret_c(c, 0); return; }
#ifdef __APPLE__
        const char *vd = SDL_GetCurrentVideoDriver();
        if (vd && !strcmp(vd, "cocoa")) SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
#endif
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
        if (!ren) { rt_log("SDL_CreateRenderer: %s", SDL_GetError()); ret_c(c, 0); return; }
        renderer_low_latency();
        SDL_DisplayMode wm;
        if (SDL_GetWindowDisplayMode(win, &wm) == 0 && wm.refresh_rate > 0) fc_period = 1000.0 / wm.refresh_rate;
        SDL_RendererInfo ri;
        if (SDL_GetRendererInfo(ren, &ri) == 0) rt_log("renderer: %s, refresh %.2f ms", ri.name, fc_period);
    }
    SDL_SetWindowFullscreen(win, (flags & SDL1_FULLSCREEN) ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!screen || w != scr_w || h != scr_h) {
        if (tex) SDL_DestroyTexture(tex);
        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_RenderSetLogicalSize(ren, w, h);
        free(rgb_buf);
        rgb_buf = calloc((size_t)w * (size_t)h, 4);
        if (!(flags & SDL1_FULLSCREEN)) {
            int ww, wh;
            SDL_GetWindowSize(win, &ww, &wh);
            if (ww < w || wh < h) SDL_SetWindowSize(win, w, h);
        }
        uint32_t old = screen;
        screen = new_surface(flags, w, h, alloc_format(8, 0, 0, 0, 0));
        if (old) {
            // keep the old palette
            uint32_t op = F_PAL(S_FMT(old)), np = F_PAL(S_FMT(screen));
            memcpy(GP(G32(np + 4)), GP(G32(op + 4)), 4 * 256);
            free_surface(old);
        }
        scr_w = w; scr_h = h;
    }
    S_FLAGS(screen) = (flags & (SDL1_FULLSCREEN | SDL1_HWPALETTE)) | SDL1_SWSURFACE;
    ret_c(c, screen);
}
SHIM(sdl_GetVideoSurface) { ret_c(c, screen); }
static void audio_det_pump(uint64_t us);
SHIM(sdl_Flip) {
    chk_extern("SDL_Flip");
    if (plat_is_det()) {   // one display refresh of virtual time per frame
        plat_advance_us(16667);
        audio_det_pump(16667);
    }
    if (screen) chk_frame_end(GP(S_PIX(screen)), scr_w, scr_h, S_PITCH(screen), GP(G32(F_PAL(S_FMT(screen)) + 4)));
    present();
    ret_c(c, 0);
}
SHIM(sdl_SetPalette) {
    uint32_t s = ARG(0), flags = ARG(1), cols = ARG(2);
    int first = (int)ARG(3), n = (int)ARG(4);
    uint32_t pal = s ? F_PAL(S_FMT(s)) : 0;
    if (!pal) { ret_c(c, 0); return; }
    int pn = (int)G32(pal);
    for (int i = 0; i < n && first + i < pn; i++) {
        uint32_t d = G32(pal + 4) + 4 * (uint32_t)(first + i);
        G8(d) = G8(cols + 4 * i); G8(d + 1) = G8(cols + 4 * i + 1); G8(d + 2) = G8(cols + 4 * i + 2);
    }
    if (s == screen && (flags & 2)) present();
    ret_c(c, 1);
}
SHIM(sdl_CreateRGBSurface) {
    int w = (int)ARG(1), h = (int)ARG(2), bpp = (int)ARG(3);
    uint32_t f = alloc_format(bpp, ARG(4), ARG(5), ARG(6), ARG(7));
    uint32_t s = new_surface(ARG(0) & (SDL1_SRCALPHA | SDL1_SRCCOLORKEY), w, h, f);
    if (G32(f + 0x1c)) S_FLAGS(s) |= SDL1_SRCALPHA;
    ret_c(c, s);
}
SHIM(sdl_FreeSurface) { if (ARG(0) != screen) free_surface(ARG(0)); ret_c(c, 0); }
SHIM(sdl_LockSurface) { ret_c(c, 0); }
SHIM(sdl_UnlockSurface) { ret_c(c, 0); }
SHIM(sdl_SetAlpha) {
    uint32_t s = ARG(0), flags = ARG(1);
    if (flags & SDL1_SRCALPHA) S_FLAGS(s) |= SDL1_SRCALPHA; else S_FLAGS(s) &= ~SDL1_SRCALPHA;
    G8(S_FMT(s) + 0x24) = (uint8_t)ARG(2);
    ret_c(c, 0);
}
SHIM(sdl_GetRGBA) {
    int r, g, b, a;
    get_rgba(ARG(1), ARG(0), &r, &g, &b, &a);
    G8(ARG(2)) = (uint8_t)r; G8(ARG(3)) = (uint8_t)g; G8(ARG(4)) = (uint8_t)b; G8(ARG(5)) = (uint8_t)a;
    ret_c(c, 0);
}
SHIM(sdl_ConvertSurface) { ret_c(c, convert_surface(ARG(0), ARG(1), ARG(2))); }
SHIM(sdl_DisplayFormat) {
    uint32_t s = ARG(0);
    if (!screen) { ret_c(c, 0); return; }
    ret_c(c, convert_surface(s, S_FMT(screen), S_FLAGS(s) & (SDL1_SRCALPHA | SDL1_SRCCOLORKEY)));
}
SHIM(sdl_DisplayFormatAlpha) {
    uint32_t s = ARG(0);
    uint32_t f = alloc_format(32, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0xff000000u);
    uint32_t d = convert_surface(s, f, S_FLAGS(s) & SDL1_SRCALPHA);
    free_format(f);
    ret_c(c, d);
}

SHIM(sdl_FillRect) {
    uint32_t s = ARG(0), r = ARG(1), color = ARG(2);
    int cx = (int16_t)G16(s + 0x20), cy = (int16_t)G16(s + 0x22), cw = G16(s + 0x24), ch = G16(s + 0x26);
    int x = cx, y = cy, w = cw, h = ch;
    if (r) {
        int rx = (int16_t)G16(r), ry = (int16_t)G16(r + 2), rw = G16(r + 4), rh = G16(r + 6);
        int x0 = rx > cx ? rx : cx, y0 = ry > cy ? ry : cy;
        int x1 = (rx + rw < cx + cw) ? rx + rw : cx + cw, y1 = (ry + rh < cy + ch) ? ry + rh : cy + ch;
        if (x1 <= x0 || y1 <= y0) { G16(r + 4) = 0; G16(r + 6) = 0; ret_c(c, 0); return; }
        x = x0; y = y0; w = x1 - x0; h = y1 - y0;
        G16(r) = (uint16_t)x; G16(r + 2) = (uint16_t)y; G16(r + 4) = (uint16_t)w; G16(r + 6) = (uint16_t)h;
    }
    int bypp = F_BYPP(S_FMT(s));
    uint32_t pitch = S_PITCH(s);
    for (int yy = 0; yy < h; yy++) {
        uint32_t row = S_PIX(s) + (uint32_t)(y + yy) * pitch + (uint32_t)(x * bypp);
        if (bypp == 1) memset(GP(row), (int)(color & 0xff), (size_t)w);
        else for (int xx = 0; xx < w; xx++) put_px(row + (uint32_t)(xx * bypp), bypp, color);
    }
    ret_c(c, 0);
}

// SDL_UpperBlit with SDL 1.2 clipping semantics (dstrect is updated)
SHIM(sdl_UpperBlit) {
    uint32_t src = ARG(0), sr = ARG(1), dst = ARG(2), dr = ARG(3);
    if (!src || !dst) { ret_c(c, (uint32_t)-1); return; }
    int dx = 0, dy = 0;
    if (dr) { dx = (int16_t)G16(dr); dy = (int16_t)G16(dr + 2); }
    int srcx, srcy, w, h;
    if (sr) {
        srcx = (int16_t)G16(sr); w = G16(sr + 4);
        if (srcx < 0) { w += srcx; dx -= srcx; srcx = 0; }
        int maxw = S_W(src) - srcx; if (maxw < w) w = maxw;
        srcy = (int16_t)G16(sr + 2); h = G16(sr + 6);
        if (srcy < 0) { h += srcy; dy -= srcy; srcy = 0; }
        int maxh = S_H(src) - srcy; if (maxh < h) h = maxh;
    } else { srcx = srcy = 0; w = S_W(src); h = S_H(src); }
    int cx = (int16_t)G16(dst + 0x20), cy = (int16_t)G16(dst + 0x22), cw = G16(dst + 0x24), ch = G16(dst + 0x26);
    int d;
    d = cx - dx; if (d > 0) { w -= d; dx += d; srcx += d; }
    d = dx + w - cx - cw; if (d > 0) w -= d;
    d = cy - dy; if (d > 0) { h -= d; dy += d; srcy += d; }
    d = dy + h - cy - ch; if (d > 0) h -= d;
    if (dr) { G16(dr) = (uint16_t)dx; G16(dr + 2) = (uint16_t)dy; }
    if (w > 0 && h > 0) {
        if (dr) { G16(dr + 4) = (uint16_t)w; G16(dr + 6) = (uint16_t)h; }
        int blend = (S_FLAGS(src) & SDL1_SRCALPHA) && G32(S_FMT(src) + 0x1c);
        lower_blit(src, srcx, srcy, dst, dx, dy, w, h, blend);
    } else if (dr) { G16(dr + 4) = 0; G16(dr + 6) = 0; }
    ret_c(c, 0);
}

// ------------------------------------------------------------------ files (RWops) / BMP
// guest SDL_RWops: we only need an identity -> small block holding the host path and mode
SHIM(sdl_RWFromFile) {
    char p[1024];
    host_path(GS(ARG(0)), p, sizeof p);
    uint32_t rw = gcalloc(1, 1100);
    snprintf(GS(rw + 16), 1024, "%s", p);
    snprintf(GS(rw + 1050), 16, "%s", GS(ARG(1)));
    G32(rw) = 0x52574f50;  // "RWOP"
    ret_c(c, rw);
}

SHIM(sdl_SaveBMP_RW) {
    uint32_t s = ARG(0), rw = ARG(1);
    if (!s || !rw) { ret_c(c, (uint32_t)-1); return; }
    FILE *f = fopen(GS(rw + 16), "wb");
    if (!f) { if (ARG(2)) gfree(rw); ret_c(c, (uint32_t)-1); return; }
    int w = S_W(s), h = S_H(s), bypp = F_BYPP(S_FMT(s));
    int rowb = ((w * 3) + 3) & ~3;
    uint32_t size = 54 + (uint32_t)rowb * (uint32_t)h;
    uint8_t hd[54] = {'B', 'M'};
    memcpy(hd + 2, &size, 4);
    uint32_t off = 54, hs = 40, img = (uint32_t)rowb * (uint32_t)h;
    memcpy(hd + 10, &off, 4); memcpy(hd + 14, &hs, 4); memcpy(hd + 18, &w, 4); memcpy(hd + 22, &h, 4);
    hd[26] = 1; hd[28] = 24;
    memcpy(hd + 34, &img, 4);
    fwrite(hd, 1, 54, f);
    uint8_t *row = calloc(1, (size_t)rowb);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            int r, g, b, a;
            get_rgba(S_FMT(s), get_px(S_PIX(s) + (uint32_t)y * S_PITCH(s) + (uint32_t)(x * bypp), bypp), &r, &g, &b, &a);
            row[3 * x] = (uint8_t)b; row[3 * x + 1] = (uint8_t)g; row[3 * x + 2] = (uint8_t)r;
        }
        fwrite(row, 1, (size_t)rowb, f);
    }
    free(row);
    fclose(f);
    if (ARG(2)) gfree(rw);
    ret_c(c, 0);
}

// ------------------------------------------------------------------ audio
static uint32_t audio_cb, audio_ud, audio_buf, audio_len;
static CPU *audio_cpu;
static int audio_open;   // 1: host device, 2: virtual (deterministic mode)
static SDL_AudioSpec det_spec;
static int det_paused = 1;

static void host_audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    if (!audio_cpu) audio_cpu = rt_new_cpu(THREAD_STACK);
    if ((uint32_t)len > audio_len) { gfree(audio_buf); audio_buf = gmalloc((uint32_t)len); audio_len = (uint32_t)len; }
    memset(GP(audio_buf), 0, (size_t)len);
    uint32_t a[3] = {audio_ud, audio_buf, (uint32_t)len};
    guest_call(audio_cpu, audio_cb, 3, a);
    memcpy(stream, GP(audio_buf), (size_t)len);
    // debug: GRX_WAVOUT=file.raw appends everything the game mixes (raw, format as requested)
    static FILE *wav;
    static int wav_init;
    if (!wav_init) { wav_init = 1; const char *fn = getenv("GRX_WAVOUT"); if (fn) wav = fopen(fn, "wb"); }
    if (wav) { fwrite(stream, 1, (size_t)len, wav); fflush(wav); }
}

SHIM(sdl_OpenAudio) {
    uint32_t want = ARG(0), got = ARG(1);
    SDL_AudioSpec w, h;
    memset(&w, 0, sizeof w);
    w.freq = (int)G32(want);
    w.format = G16(want + 4);
    w.channels = G8(want + 6);
    w.samples = G16(want + 8);
    w.callback = host_audio_cb;
    audio_cb = G32(want + 0x10);
    audio_ud = G32(want + 0x14);
    // SDL 1.2 games mix in exactly the format they asked for: let SDL2 convert (obtained = NULL).
    // Deterministic mode: no device; the mixer callback is run from SDL_Flip (audio_det_pump).
    if (plat_is_det()) audio_open = 2;
    else if (SDL_OpenAudio(&w, NULL) < 0) { rt_log("SDL_OpenAudio: %s", SDL_GetError()); ret_c(c, (uint32_t)-1); return; }
    h = w;
    h.silence = (w.format == AUDIO_U8) ? 0x80 : 0;
    if (!h.samples) h.samples = 4096;
    h.size = (Uint32)h.samples * h.channels * (SDL_AUDIO_BITSIZE(w.format) / 8);
    rt_log("audio: %d Hz fmt %04x ch %d samples %d cb %08x ud %08x", w.freq, w.format, w.channels, w.samples, audio_cb, audio_ud);
    if (!audio_open) audio_open = 1;
    det_spec = h;
    uint32_t o = got ? got : want;
    G32(o) = (uint32_t)h.freq;
    G16(o + 4) = h.format;
    G8(o + 6) = h.channels;
    G8(o + 7) = h.silence;
    G16(o + 8) = h.samples;
    G32(o + 0xc) = h.size;
    G32(o + 0x10) = audio_cb;
    G32(o + 0x14) = audio_ud;
    ret_c(c, 0);
}
SHIM(sdl_PauseAudio) {
    det_paused = (int)ARG(0);
    if (audio_open == 1) SDL_PauseAudio((int)ARG(0));
    ret_c(c, 0);
}
SHIM(sdl_CloseAudio) { if (audio_open == 1) SDL_CloseAudio(); audio_open = 0; ret_c(c, 0); }

// deterministic mode: run the game's mixer for the virtual time that passed, in device-sized chunks
static void audio_det_pump(uint64_t us) {
    static uint64_t owed_bytes_x1e6;
    if (audio_open != 2 || det_paused || !det_spec.size) return;
    uint64_t bps = (uint64_t)det_spec.freq * det_spec.channels * (SDL_AUDIO_BITSIZE(det_spec.format) / 8);
    owed_bytes_x1e6 += bps * us;
    static uint8_t buf[1 << 16];
    while (owed_bytes_x1e6 >= (uint64_t)det_spec.size * 1000000u && det_spec.size <= sizeof buf) {
        owed_bytes_x1e6 -= (uint64_t)det_spec.size * 1000000u;
        host_audio_cb(NULL, buf, (int)det_spec.size);
        chk_audio_hash(buf, det_spec.size);
    }
}

// SDL_LoadWAV_RW(rw, freesrc, spec*, Uint8 **buf, Uint32 *len) -> spec*
SHIM(sdl_LoadWAV_RW) {
    uint32_t rw = ARG(0), spec = ARG(2), pbuf = ARG(3), plen = ARG(4);
    if (!rw) { ret_c(c, 0); return; }
    SDL_AudioSpec s;
    Uint8 *buf = NULL;
    Uint32 len = 0;
    SDL_AudioSpec *r = SDL_LoadWAV(GS(rw + 16), &s, &buf, &len);
    if (ARG(1)) gfree(rw);
    if (!r) { ret_c(c, 0); return; }
    uint32_t g = gmalloc(len);
    memcpy(GP(g), buf, len);
    SDL_FreeWAV(buf);
    memset(GP(spec), 0, 0x18);
    G32(spec) = (uint32_t)s.freq;
    G16(spec + 4) = s.format;
    G8(spec + 6) = s.channels;
    G8(spec + 7) = s.silence;
    G16(spec + 8) = s.samples;
    G32(spec + 0xc) = s.size;
    G32(pbuf) = g;
    G32(plen) = len;
    ret_c(c, spec);
}
SHIM(sdl_FreeWAV) { gfree(ARG(0)); ret_c(c, 0); }

// audio conversion: SDL2 does the work; the host SDL_AudioCVT is kept next to the guest one
typedef struct { uint32_t guest; SDL_AudioCVT cvt; } CvtSlot;
static CvtSlot cvts[64];
static SDL_AudioCVT *cvt_for(uint32_t g, int create) {
    for (int i = 0; i < 64; i++) if (cvts[i].guest == g) return &cvts[i].cvt;
    if (!create) return NULL;
    for (int i = 0; i < 64; i++) if (!cvts[i].guest) { cvts[i].guest = g; return &cvts[i].cvt; }
    cvts[0].guest = g;
    return &cvts[0].cvt;
}
SHIM(sdl_BuildAudioCVT) {
    uint32_t g = ARG(0);
    SDL_AudioCVT *cv = cvt_for(g, 1);
    int r = SDL_BuildAudioCVT(cv, (SDL_AudioFormat)ARG(1), (Uint8)ARG(2), (int)ARG(3), (SDL_AudioFormat)ARG(4), (Uint8)ARG(5), (int)ARG(6));
    memset(GP(g), 0, 0x54);
    G32(g) = (uint32_t)cv->needed;
    G16(g + 4) = cv->src_format;
    G16(g + 6) = cv->dst_format;
    G64(g + 8) = b8(cv->rate_incr);
    G32(g + 0x1c) = (uint32_t)cv->len_mult;
    G64(g + 0x20) = b8(cv->len_ratio);
    ret_c(c, (uint32_t)r);
}
SHIM(sdl_ConvertAudio) {
    uint32_t g = ARG(0);
    SDL_AudioCVT *cv = cvt_for(g, 0);
    if (!cv) { ret_c(c, (uint32_t)-1); return; }
    int len = (int)G32(g + 0x14);
    cv->len = len;
    cv->buf = malloc((size_t)len * (size_t)(cv->len_mult > 0 ? cv->len_mult : 1));
    memcpy(cv->buf, GP(G32(g + 0x10)), (size_t)len);
    int r = SDL_ConvertAudio(cv);
    if (r == 0) {
        memcpy(GP(G32(g + 0x10)), cv->buf, (size_t)cv->len_cvt);
        G32(g + 0x18) = (uint32_t)cv->len_cvt;
    }
    free(cv->buf);
    cv->buf = NULL;
    ret_c(c, (uint32_t)r);
}

// ------------------------------------------------------------------ input
static uint32_t keystate;  // guest Uint8[SDLK_LAST]
#define SDLK1_LAST 323

static int key12(SDL_Keysym ks) {
    SDL_Scancode s = ks.scancode;
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return 282 + (s - SDL_SCANCODE_F1);
    if (s >= SDL_SCANCODE_F13 && s <= SDL_SCANCODE_F15) return 294 + (s - SDL_SCANCODE_F13);
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return 257 + (s - SDL_SCANCODE_KP_1);
    switch (s) {
    case SDL_SCANCODE_KP_0: return 256;
    case SDL_SCANCODE_KP_PERIOD: return 266;
    case SDL_SCANCODE_KP_DIVIDE: return 267;
    case SDL_SCANCODE_KP_MULTIPLY: return 268;
    case SDL_SCANCODE_KP_MINUS: return 269;
    case SDL_SCANCODE_KP_PLUS: return 270;
    case SDL_SCANCODE_KP_ENTER: return 271;
    case SDL_SCANCODE_KP_EQUALS: return 272;
    case SDL_SCANCODE_UP: return 273;
    case SDL_SCANCODE_DOWN: return 274;
    case SDL_SCANCODE_RIGHT: return 275;
    case SDL_SCANCODE_LEFT: return 276;
    case SDL_SCANCODE_INSERT: return 277;
    case SDL_SCANCODE_HOME: return 278;
    case SDL_SCANCODE_END: return 279;
    case SDL_SCANCODE_PAGEUP: return 280;
    case SDL_SCANCODE_PAGEDOWN: return 281;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 300;
    case SDL_SCANCODE_CAPSLOCK: return 301;
    case SDL_SCANCODE_SCROLLLOCK: return 302;
    case SDL_SCANCODE_RSHIFT: return 303;
    case SDL_SCANCODE_LSHIFT: return 304;
    case SDL_SCANCODE_RCTRL: return 305;
    case SDL_SCANCODE_LCTRL: return 306;
    case SDL_SCANCODE_RALT: return 307;
    case SDL_SCANCODE_LALT: return 308;
    case SDL_SCANCODE_RGUI: return 309;
    case SDL_SCANCODE_LGUI: return 310;
    case SDL_SCANCODE_PRINTSCREEN: return 316;
    case SDL_SCANCODE_PAUSE: return 19;
    case SDL_SCANCODE_APPLICATION: return 319;
    case SDL_SCANCODE_DELETE: return 127;
    default: break;
    }
    if (ks.sym > 0 && ks.sym < 128) return ks.sym;
    if (ks.sym >= 160 && ks.sym < 256) return ks.sym;  // Latin-1 keys map to SDLK_WORLD-ish range in 1.2
    return 0;
}

#define EVQ 256
static uint8_t evq[EVQ][20];
static int evq_head, evq_n;
static void ev_push(const uint8_t *e) {
    if (evq_n >= EVQ) return;
    memcpy(evq[(evq_head + evq_n) % EVQ], e, 20);
    evq_n++;
}

// headless test input: GRX_SCRIPT="t:click:x:y,t:key:sym,t:down:sym,t:up:sym,t:snap:n,t:quit:0,..."
//   mouse: click:x:y (left press+release), mdown:x:y / move:x:y / mup:x:y (drag), rclick:x:y, wheel:4|5
// t = ms since SDL_Init (virtual time with GRX_DET=1, so runs are reproducible)
static void script_events(void) {
    static const char *p;
    static int init;
    if (!init) { init = 1; p = getenv("GRX_SCRIPT"); }
    while (p && *p) {
        unsigned ms = 0;
        char kind[16] = {0};
        int a = 0, b = 0, n = 0;
        if (sscanf(p, "%u:%15[a-z]:%d:%d%n", &ms, kind, &a, &b, &n) < 3) { p = NULL; return; }
        int two = !strcmp(kind, "click") || !strcmp(kind, "rclick") || !strcmp(kind, "mdown") || !strcmp(kind, "move") ||
                  !strcmp(kind, "mup");
        if (!two && n == 0) sscanf(p, "%u:%15[a-z]:%d%n", &ms, kind, &a, &n);
        if ((plat_ticks_us() - t0_us) / 1000 < ms) return;   // ms since SDL_Init (virtual with GRX_DET=1)
        p += n;
        if (*p == ',') p++;
        uint8_t ev[20];
        memset(ev, 0, 20);
        if (!strcmp(kind, "quit")) { rt_log("script: %u quit", ms); chk_exit(0); }
        if (!strcmp(kind, "snap")) {
            char fn[64];
            snprintf(fn, sizeof fn, "headless_%d.bmp", a);
            void sdl_debug_snapshot(const char *path);
            sdl_debug_snapshot(fn);
            continue;
        }
        static uint8_t held;   // scripted mouse buttons held (bit 0 left)
        if (!strcmp(kind, "mdown") || !strcmp(kind, "move") || !strcmp(kind, "mup") || !strcmp(kind, "rclick") ||
            !strcmp(kind, "wheel")) {
            uint16_t x = (uint16_t)a, y = (uint16_t)b;
            if (!strcmp(kind, "wheel")) { x = 0; y = 0; }
            memcpy(ev + 4, &x, 2); memcpy(ev + 6, &y, 2);
            if (strcmp(kind, "wheel")) { ev[0] = 4; ev[2] = held; ev_push(ev); }   // motion to the spot
            if (!strcmp(kind, "mdown")) { held |= 1; ev[0] = 5; ev[2] = 1; ev[3] = 1; ev_push(ev); }
            if (!strcmp(kind, "mup")) { held &= (uint8_t)~1; ev[0] = 6; ev[2] = 1; ev[3] = 0; ev_push(ev); }
            if (!strcmp(kind, "rclick")) {
                ev[0] = 5; ev[2] = 3; ev[3] = 1; ev_push(ev);
                ev[0] = 6; ev[3] = 0; ev_push(ev);
            }
            if (!strcmp(kind, "wheel")) {
                ev[0] = 5; ev[2] = (uint8_t)a; ev[3] = 1; ev_push(ev);
                ev[0] = 6; ev[3] = 0; ev_push(ev);
            }
        } else if (!strcmp(kind, "click")) {
            uint16_t x = (uint16_t)a, y = (uint16_t)b;
            memcpy(ev + 4, &x, 2); memcpy(ev + 6, &y, 2);
            ev[2] = 1;
            ev[0] = 4; ev_push(ev);
            ev[0] = 5; ev[3] = 1; ev_push(ev);
            ev[0] = 6; ev[3] = 0; ev_push(ev);
        } else {
            int down = strcmp(kind, "up") != 0;
            for (int pass = 0; pass < (strcmp(kind, "key") ? 1 : 2); pass++) {
                int d = strcmp(kind, "key") ? down : !pass;
                if (keystate && a > 0 && a < 323) G8(keystate + (uint32_t)a) = (uint8_t)d;
                memset(ev, 0, 20);
                ev[0] = d ? 2 : 3; ev[2] = (uint8_t)d;
                uint32_t sym = (uint32_t)a;
                memcpy(ev + 8, &sym, 4);
                ev_push(ev);
            }
        }
        rt_log("script: %u ms %s %d %d", ms, kind, a, b);
    }
}

static void ensure_keystate(void) { if (!keystate) keystate = gdata_alloc(SDLK1_LAST + 13); }

static void pump(void) {
    ensure_keystate();
    if (plat_is_det()) {   // scripted input only; polling loops still see time pass
        plat_advance_us(10);
        script_events();
        return;
    }
    script_events();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        uint8_t ev[20];
        memset(ev, 0, 20);
        switch (e.type) {
        case SDL_QUIT:
            ev[0] = 12;
            ev_push(ev);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            if (e.key.repeat) break;
            int k = key12(e.key.keysym);
            int down = e.type == SDL_KEYDOWN;
            if (k > 0 && k < SDLK1_LAST) G8(keystate + (uint32_t)k) = (uint8_t)down;
            if (k == 0) break;
            ev[0] = down ? 2 : 3;
            ev[2] = (uint8_t)down;
            ev[4] = (uint8_t)e.key.keysym.scancode;
            uint32_t sym = (uint32_t)k, mod = e.key.keysym.mod;
            memcpy(ev + 8, &sym, 4);
            memcpy(ev + 12, &mod, 4);
            ev_push(ev);
            break;
        }
        case SDL_MOUSEMOTION: {
            ev[0] = 4;
            uint32_t bs = e.motion.state;
            ev[2] = (uint8_t)((bs & 1) | ((bs & 2) ? 2 : 0) | ((bs & 4) ? 4 : 0));
            int x = e.motion.x < 0 ? 0 : e.motion.x, y = e.motion.y < 0 ? 0 : e.motion.y;
            if (x >= scr_w) x = scr_w - 1;
            if (y >= scr_h) y = scr_h - 1;
            uint16_t xx = (uint16_t)x, yy = (uint16_t)y;
            int16_t xr = (int16_t)e.motion.xrel, yr = (int16_t)e.motion.yrel;
            memcpy(ev + 4, &xx, 2); memcpy(ev + 6, &yy, 2); memcpy(ev + 8, &xr, 2); memcpy(ev + 10, &yr, 2);
            ev_push(ev);
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            ev[0] = e.type == SDL_MOUSEBUTTONDOWN ? 5 : 6;
            ev[2] = e.button.button;  // 1 left, 2 middle, 3 right (same numbering)
            ev[3] = e.type == SDL_MOUSEBUTTONDOWN;
            uint16_t xx = (uint16_t)(e.button.x < 0 ? 0 : e.button.x), yy = (uint16_t)(e.button.y < 0 ? 0 : e.button.y);
            memcpy(ev + 4, &xx, 2); memcpy(ev + 6, &yy, 2);
            ev_push(ev);
            break;
        }
        case SDL_MOUSEWHEEL: {
            // SDL 1.2 reports the wheel as buttons 4 (up) and 5 (down)
            if (!e.wheel.y) break;
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            float lx = (float)mx, ly = (float)my;
            if (ren) {
                int ix, iy;
                SDL_RenderWindowToLogical(ren, mx, my, &lx, &ly);
                ix = (int)lx; iy = (int)ly;
                lx = (float)(ix < 0 ? 0 : ix); ly = (float)(iy < 0 ? 0 : iy);
            }
            uint16_t xx = (uint16_t)lx, yy = (uint16_t)ly;
            ev[2] = e.wheel.y > 0 ? 4 : 5;
            memcpy(ev + 4, &xx, 2); memcpy(ev + 6, &yy, 2);
            ev[0] = 5; ev[3] = 1; ev_push(ev);
            ev[0] = 6; ev[3] = 0; ev_push(ev);
            break;
        }
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED || e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                ev[0] = 1;
                ev[1] = e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
                ev[2] = 2;  // SDL_APPINPUTFOCUS
                ev_push(ev);
            } else if (e.window.event == SDL_WINDOWEVENT_EXPOSED) {
                present();
            }
            break;
        default:
            break;
        }
    }
}

SHIM(sdl_PumpEvents) { pump(); ret_c(c, 0); }
SHIM(sdl_PollEvent) {
    uint32_t ev = ARG(0);
    pump();
    if (!evq_n) { ret_c(c, 0); return; }
    if (ev) {
        memcpy(GP(ev), evq[evq_head], 20);
        evq_head = (evq_head + 1) % EVQ;
        evq_n--;
    }
    ret_c(c, 1);
}
SHIM(sdl_PushEvent) { ev_push(GP(ARG(0))); ret_c(c, 0); }
SHIM(sdl_GetKeyState) {
    perf_ticks++;
    ensure_keystate();
    if (ARG(0)) G32(ARG(0)) = SDLK1_LAST;
    ret_c(c, keystate);
}

// joysticks: the guest gets small integer handles (index + 1)
static SDL_Joystick *joys[16];
static SDL_Joystick *joy(uint32_t h) { return (h >= 1 && h <= 16) ? joys[h - 1] : NULL; }
SHIM(sdl_NumJoysticks) { ret_c(c, (uint32_t)SDL_NumJoysticks()); }
SHIM(sdl_JoystickOpen) {
    int i = (int)ARG(0);
    if (i < 0 || i >= 16) { ret_c(c, 0); return; }
    if (!joys[i]) joys[i] = SDL_JoystickOpen(i);
    ret_c(c, joys[i] ? (uint32_t)i + 1 : 0);
}
SHIM(sdl_JoystickOpened) { int i = (int)ARG(0); ret_c(c, (i >= 0 && i < 16 && joys[i]) ? 1 : 0); }
SHIM(sdl_JoystickNumAxes) { SDL_Joystick *j = joy(ARG(0)); ret_c(c, j ? (uint32_t)SDL_JoystickNumAxes(j) : 0); }
SHIM(sdl_JoystickNumButtons) { SDL_Joystick *j = joy(ARG(0)); ret_c(c, j ? (uint32_t)SDL_JoystickNumButtons(j) : 0); }
SHIM(sdl_JoystickNumHats) { SDL_Joystick *j = joy(ARG(0)); ret_c(c, j ? (uint32_t)SDL_JoystickNumHats(j) : 0); }
SHIM(sdl_JoystickGetAxis) { SDL_Joystick *j = joy(ARG(0)); ret_c(c, j ? (uint32_t)(int32_t)SDL_JoystickGetAxis(j, (int)ARG(1)) : 0); }
SHIM(sdl_JoystickGetButton) { SDL_Joystick *j = joy(ARG(0)); ret_c(c, j ? SDL_JoystickGetButton(j, (int)ARG(1)) : 0); }
SHIM(sdl_JoystickGetHat) { SDL_Joystick *j = joy(ARG(0)); ret_c(c, j ? SDL_JoystickGetHat(j, (int)ARG(1)) : 0); }

const ShimDef shims_sdl[] = {
    {"SDL_Init", sdl_Init}, {"SDL_Quit", sdl_Quit}, {"SDL_GetTicks", sdl_GetTicks}, {"SDL_Delay", sdl_Delay},
    {"SDL_WM_SetCaption", sdl_WM_SetCaption}, {"SDL_ShowCursor", sdl_ShowCursor},
    {"SDL_SetVideoMode", sdl_SetVideoMode}, {"SDL_GetVideoSurface", sdl_GetVideoSurface}, {"SDL_Flip", sdl_Flip},
    {"SDL_SetPalette", sdl_SetPalette}, {"SDL_CreateRGBSurface", sdl_CreateRGBSurface},
    {"SDL_FreeSurface", sdl_FreeSurface}, {"SDL_LockSurface", sdl_LockSurface}, {"SDL_UnlockSurface", sdl_UnlockSurface},
    {"SDL_SetAlpha", sdl_SetAlpha}, {"SDL_GetRGBA", sdl_GetRGBA}, {"SDL_ConvertSurface", sdl_ConvertSurface},
    {"SDL_DisplayFormat", sdl_DisplayFormat}, {"SDL_DisplayFormatAlpha", sdl_DisplayFormatAlpha},
    {"SDL_FillRect", sdl_FillRect}, {"SDL_UpperBlit", sdl_UpperBlit},
    {"SDL_RWFromFile", sdl_RWFromFile}, {"SDL_SaveBMP_RW", sdl_SaveBMP_RW},
    {"SDL_OpenAudio", sdl_OpenAudio}, {"SDL_PauseAudio", sdl_PauseAudio}, {"SDL_CloseAudio", sdl_CloseAudio},
    {"SDL_LoadWAV_RW", sdl_LoadWAV_RW}, {"SDL_FreeWAV", sdl_FreeWAV},
    {"SDL_BuildAudioCVT", sdl_BuildAudioCVT}, {"SDL_ConvertAudio", sdl_ConvertAudio},
    {"SDL_PumpEvents", sdl_PumpEvents}, {"SDL_PollEvent", sdl_PollEvent}, {"SDL_PushEvent", sdl_PushEvent},
    {"SDL_GetKeyState", sdl_GetKeyState},
    {"SDL_NumJoysticks", sdl_NumJoysticks}, {"SDL_JoystickOpen", sdl_JoystickOpen}, {"SDL_JoystickOpened", sdl_JoystickOpened},
    {"SDL_JoystickNumAxes", sdl_JoystickNumAxes}, {"SDL_JoystickNumButtons", sdl_JoystickNumButtons},
    {"SDL_JoystickNumHats", sdl_JoystickNumHats}, {"SDL_JoystickGetAxis", sdl_JoystickGetAxis},
    {"SDL_JoystickGetButton", sdl_JoystickGetButton}, {"SDL_JoystickGetHat", sdl_JoystickGetHat},
    {NULL, NULL},
};
