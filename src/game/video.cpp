// 8-bit palettized video (see video.h).
#include "game/video.h"

#include "game/file.h"
#include "game/race_options.h"
#include "game/fmath.h"

#include <cstring>

// The window is 8 bits per pixel with a hardware palette, fullscreen unless windowed.
static uint32_t mode_flags(bool windowed) { return windowed ? 0 : SDL_FULLSCREEN | SDL_HWPALETTE; }

bool video_init(bool windowed) {
    int w = g_screen_w, h = g_screen_h;
    if (!sdl::SetVideoMode(w, h, 8, mode_flags(windowed))) return false;
    g_back = sdl::CreateRGBSurface(0, w, h, 8);
    g_background = sdl::CreateRGBSurface(0, w, h, 8);
    if (g_back) {
        if (g_background) return true;
        sdl::FreeSurface(g_back);
    }
    if (g_background) sdl::FreeSurface(g_background);
    g_back = 0;
    g_background = 0;
    return false;
}

bool video_set_mode(int w, int h, bool windowed) {
    if (g_back) sdl::FreeSurface(g_back);
    if (g_background) sdl::FreeSurface(g_background);
    g_back = 0;
    g_background = 0;
    if (!sdl::SetVideoMode(w, h, 8, mode_flags(windowed))) return false;
    g_back = sdl::CreateRGBSurface(0, w, h, 8);
    g_background = sdl::CreateRGBSurface(0, w, h, 8);
    return true;
}

SdlSurfaceRef surface(int which) {
    switch (which) {
    case SURF_SCREEN: return sdl::GetVideoSurface();
    case SURF_BACK: return g_back;
    case SURF_BACKGROUND: return g_background;
    default: return 0;
    }
}

void lock_surface(int which) {
    SdlSurfaceRef s = surface(which);
    if (sdl::LockSurface(s) == 0) {
        g_pitch = sdl::surface(s)->pitch;
        g_pixels = sdl::surface(s)->pixels;
        g_locked = s;
    }
}

void unlock_surface() {
    if (!g_locked) return;
    g_pitch = 0;
    g_pixels = 0;
    sdl::UnlockSurface(g_locked);
    g_locked = 0;
}

bool blit_surface(int src, int dst) {
    SdlSurfaceRef s = surface(src), d = surface(dst);
    if (!s || !d) return false;
    return sdl::UpperBlit(s, nullptr, d, nullptr) == 0;
}

// The top-left w x h of the background onto the back buffer at x, y.
bool blit_background(int16_t w, int16_t h, int16_t x, int16_t y) {
    if (!g_background || !g_back) return false;
    guest::Local<SdlRect> from, to;
    *from = {0, 0, static_cast<uint16_t>(w), static_cast<uint16_t>(h)};
    *to = {x, y, static_cast<uint16_t>(w), static_cast<uint16_t>(h)};
    return sdl::UpperBlit(g_background, from.get(), g_back, to.get()) == 0;
}

void fill_rect_corners(uint32_t color, int16_t x1, int16_t y1, int16_t x2, int16_t y2) {
    if (!g_back) return;
    guest::Local<SdlRect> r;
    *r = {x1, y1, static_cast<uint16_t>(x2 - x1), static_cast<uint16_t>(y2 - y1)};
    sdl::FillRect(g_back, r.get(), color);
}

void fill_rect(uint32_t color, int16_t x, int16_t y, uint16_t w, uint16_t h) {
    if (!g_back) return;
    guest::Local<SdlRect> r;
    *r = {x, y, w, h};
    sdl::FillRect(g_back, r.get(), color);
}

// whole back buffer if box is null
void fill_box(uint32_t color, const Box *box) {
    if (!g_back) return;
    if (!box) {
        sdl::FillRect(g_back, nullptr, color);
        return;
    }
    guest::Local<SdlRect> r;
    int16_t x = static_cast<int16_t>(box->x1), y = static_cast<int16_t>(box->y1);
    *r = {x, y, static_cast<uint16_t>(static_cast<int16_t>(box->x2) - x),
          static_cast<uint16_t>(static_cast<int16_t>(box->y2) - y)};
    sdl::FillRect(g_back, r.get(), color);
}

static void apply_palette() {
    SdlSurfaceRef screen = sdl::GetVideoSurface();
    if (screen) sdl::SetPalette(screen, SDL_LOGPAL | SDL_PHYSPAL, g_palette, 0, 256);
    if (g_back) sdl::SetPalette(g_back, SDL_LOGPAL | SDL_PHYSPAL, g_palette, 0, 256);
    if (g_background) sdl::SetPalette(g_background, SDL_LOGPAL | SDL_PHYSPAL, g_palette, 0, 256);
}

// Copies the palette out as 0x00RRGGBB values (and re-applies it).
void palette_store(uint32_t *rgb) {
    for (int i = 0; i < 256; i++) rgb[i] = g_palette[i].r << 16 | g_palette[i].g << 8 | g_palette[i].b;
    apply_palette();
}

// Sets the palette from 0x00RRGGBB values.
void palette_load(const uint32_t *rgb) {
    for (int i = 0; i < 256; i++) {
        uint32_t v = rgb[i];
        g_palette[i] = {static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v), 0};
    }
    apply_palette();
}

// Sort order of the display mode list {width, height}: largest first.
bool display_mode_greater(const uint32_t *a, const uint32_t *b) {
    if (b[0] == a[0]) return b[1] < a[1];
    return b[0] < a[0];
}

// The fullscreen display modes the system offers, each size once, sorted by display_mode_greater.
MsVector<DisplayMode> *display_modes(MsVector<DisplayMode> *out) {
    out->first = nullptr;
    out->last = nullptr;
    out->end = nullptr;
    guest::Local<uint8_t[0x9c]> dm;  // DEVMODEA
    std::memset(*dm, 0, 0x9c);
    *reinterpret_cast<uint16_t *>(*dm + 0x24) = 0x9c;  // dmSize
    for (uint32_t i = 0; guest::call_stdcall(guest::at<uint32_t>(IMP_EnumDisplaySettingsA), 0u, i, dm.get()); i++) {
        DisplayMode m = {*reinterpret_cast<int32_t *>(*dm + 0x6c), *reinterpret_cast<int32_t *>(*dm + 0x70)};
        bool known = false;
        for (DisplayMode *p = out->first.get(); p != out->last.get(); p++)
            if (p->w == m.w && p->h == m.h) {
                known = true;
                break;
            }
        if (known) continue;
        // push_back: grow through the original's vector code (0x46cf30) when full
        if (out->last.a == out->end.a) guest::call_thiscall(0x46cf30, out, 0u);
        if (out->last) *out->last = m;
        out->last.a += sizeof(DisplayMode);
    }
    // std::sort(first, last, display_mode_greater) (0x46d010)
    guest::call_regs(0x46d010, out->first.a, out->last.a, static_cast<uint32_t>(out->size()), 0x46c550u);
    return out;
}

// Copies the background to the screen and shows it.
void show_background() {
    blit_surface(1, 0);
    guest::call_import(IMP_SDL_Flip, guest::call_import(IMP_SDL_GetVideoSurface));
}

// Saves the screen as a BMP file; with a size, a 32-bit copy scaled to w × h.
void screenshot_save(const MsString *path, uint32_t w, uint32_t h) {
    if (!w || !h) {
        uint32_t rw = guest::call_import(IMP_SDL_RWFromFile, path->c_str(), 0x49cdd0u, 1u);  // mode "wb"
        guest::call_import(IMP_SDL_SaveBMP_RW, guest::call_import(IMP_SDL_GetVideoSurface), rw, 1u);
        return;
    }
    uint32_t screen = guest::call_import(IMP_SDL_GetVideoSurface);
    uint32_t format = guest::at<uint32_t>(screen + 4);  // SDL_PixelFormat: BitsPerPixel at +4
    // converts with the screen's own format, temporarily claiming 32 bits per pixel
    guest::at<uint8_t>(format + 4) = 32;
    uint32_t converted = guest::call_import(IMP_SDL_ConvertSurface, screen, format, 0u);
    uint32_t scaled = surface_fit(converted, static_cast<int>(w), static_cast<int>(h));
    uint32_t rw = guest::call_import(IMP_SDL_RWFromFile, path->c_str(), 0x49cdd0u, 1u);
    guest::call_import(IMP_SDL_SaveBMP_RW, scaled, rw, 1u);
    guest::call_import(IMP_SDL_FreeSurface, scaled);
    guest::at<uint8_t>(format + 4) = 8;
}

// SDL_MUSTLOCK
static bool must_lock(const SdlSurface *s) { return s->offset || (s->flags & 0x4005); }

// True when any pixel of the surface is not fully opaque.
bool surface_has_alpha(SdlSurfaceRef ref) {
    SdlSurface *s = sdl::surface(ref);
    bool found = false;
    if (must_lock(s)) sdl::LockSurface(ref);
    for (int x = 0; x < s->w; x++) {
        for (int y = 0; y < s->h; y++) {
            int bpp = s->format->bytes_per_pixel;
            const uint8_t *p = &guest::at<uint8_t>(s->pixels + static_cast<uint32_t>(s->pitch * y + bpp * x));
            uint32_t pixel;
            switch (bpp) {
            case 1: pixel = p[0]; break;
            case 2: pixel = p[0] | p[1] << 8; break;
            case 3: pixel = static_cast<uint32_t>(p[2]) << 16 | p[1] << 8 | p[0]; break;
            case 4: std::memcpy(&pixel, p, 4); break;
            default: pixel = 0;
            }
            guest::Local<uint8_t[4]> rgba;
            uint32_t a = guest::addr(*rgba);
            guest::call_import(IMP_SDL_GetRGBA, pixel, s->format.a, a, a + 1, a + 2, a + 3);
            if ((*rgba)[3] != 0xff) {
                found = true;
                x = s->w;  // stop both loops
                break;
            }
        }
    }
    if (must_lock(s)) sdl::UnlockSurface(ref);
    return found;
}

namespace {
// Lanczos filtering for a scale factor (output / input size): 3 lobes, 2 when shrinking below 0.67, 1 at
// half size and below.
struct Lanczos {
    double scale;    // output / input
    double s;        // min(scale, 1): the kernel is stretched when shrinking
    int lobes;
    double support;  // input pixels on either side
    uint32_t taps;   // kernel entries kept per output pixel
    Lanczos(double scale_, int input) : scale(scale_), s(scale_ <= 1.0 ? scale_ : 1.0) {
        lobes = 3;
        if (s < 0.67) lobes = 2;
        if (s <= 0.5) lobes = 1;
        support = lobes / s;
        taps = static_cast<uint32_t>(guest::to_int64(support * 2.0)) + 5;
        if (taps >= static_cast<uint32_t>(input)) taps = static_cast<uint32_t>(input);
    }
    double weight(double x) const {
        double t = x * s;
        if (t == 0.0) return 1.0;
        if (static_cast<double>(-lobes) >= t || t >= static_cast<double>(lobes)) return 0.0;
        t = t * 3.14159265359;
        const double outer = guest_sin(t / lobes);
        const double inner = guest_sin(t);
        return (outer * (inner * lobes)) / (t * t);
    }
};

// The input range of output pixel i: first pixel and count.
void input_range(const Lanczos &l, int i, int input, double &centre, uint32_t &first, uint32_t &count) {
    centre = (static_cast<double>(i) + 0.5) / l.scale;
    double lo = (centre - l.support) + 0.5;
    if (0.0 > lo) lo = 0.0;
    first = static_cast<uint32_t>(guest::to_int64(lo));
    double hi = (l.support + 0.5) + centre;
    if (hi > static_cast<double>(input)) hi = static_cast<double>(input);
    count = static_cast<uint32_t>(guest::to_int64(hi)) - first;
}

// A filtered channel value: normalised by the kernel's sum, clamped to 0..255 and rounded.
uint8_t channel(double v, double sum) {
    if (sum != 0.0 && sum != 1.0) v = v / sum;
    if (v < 0.0) v = 0.0;
    if (255.0 < v) v = 255.0;
    int i = guest::to_int(v);
    double frac = v - static_cast<double>(i & 0xff);
    if (frac < 0.0) frac = -frac;
    if (0.5 <= frac) i++;
    return static_cast<uint8_t>(i);
}

uint32_t malloc_array(uint32_t n, uint32_t size) {  // operator new[] with the overflow check
    const uint64_t bytes = static_cast<uint64_t>(n) * size;
    return guest::call_cdecl(0x496c14, bytes > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(bytes));
}

}  // namespace

// Scales a 32-bit surface to another's size (0x401250): Lanczos filtering, first along the rows into a
// buffer of src.h rows of dst.w pixels, then along the columns into dst. Each pass computes its kernel
// once per output pixel (on the first line) into a std::vector<double>.
void scale_surface(SdlSurfaceRef src_ref, SdlSurfaceRef dst_ref) {
    const SdlSurface &src = *sdl::surface(src_ref), &dst = *sdl::surface(dst_ref);
    Lanczos lx(static_cast<double>(dst.w) / static_cast<double>(src.w), src.w);
    guest::Local<MsVector<double>> kernel_x;
    guest::call_thiscall(0x401e50, kernel_x.get(), lx.taps);  // vector<double>(taps)
    const uint32_t rows = malloc_array(static_cast<uint32_t>(src.h), 4);
    auto row = [rows](int y) { return &guest::at<uint8_t>(guest::at<uint32_t>(rows + 4u * static_cast<uint32_t>(y))); };
    for (int y = 0; y < src.h; y++)
        guest::at<uint32_t>(rows + 4u * static_cast<uint32_t>(y)) = malloc_array(static_cast<uint32_t>(dst.w), 4);
    if (lx.support <= 0.5) {
        lx.support = 0.500000000001;
        lx.s = 1.0;
    }
    for (int x = 0; x < dst.w; x++) {
        double centre, sum = 0.0, acc[4] = {};
        uint32_t first, count;
        input_range(lx, x, src.w, centre, first, count);
        double pos = (static_cast<double>(first) - centre) + 0.5;
        for (int y = 0; y < src.h; y++) {
            for (uint32_t k = 0; k < count; k++) {
                if (y == 0) {
                    const double w = lx.weight(pos);
                    pos = pos + 1.0;
                    (*kernel_x)[static_cast<int>(k)] = w;
                    sum = w + sum;
                }
                const double w = (*kernel_x)[static_cast<int>(k)];
                const uint8_t *p = &guest::at<uint8_t>(src.pixels + src.pitch * static_cast<uint32_t>(y) + (first + k) * 4);
                for (int c = 0; c < 4; c++) acc[c] = static_cast<double>(p[c]) * w + acc[c];
            }
            uint8_t *out = row(y) + x * 4;
            for (int c = 0; c < 4; c++) {
                out[c] = channel(acc[c], sum);
                acc[c] = 0.0;
            }
        }
    }

    Lanczos ly(static_cast<double>(dst.h) / static_cast<double>(src.h), src.h);
    guest::Local<MsVector<double>> kernel_y;
    guest::call_thiscall(0x401e50, kernel_y.get(), ly.taps);
    if (ly.support <= 0.5) {
        ly.support = 0.500000000001;
        ly.s = 1.0;
    }
    for (int y = 0; y < dst.h; y++) {
        double centre, sum = 0.0, acc[4] = {};
        uint32_t first, count;
        input_range(ly, y, src.h, centre, first, count);
        double pos = (static_cast<double>(first) - centre) + 0.5;
        for (int x = 0; x < dst.w; x++) {
            for (uint32_t k = 0; k < count; k++) {
                if (x == 0) {
                    const double w = ly.weight(pos);
                    pos = pos + 1.0;
                    (*kernel_y)[static_cast<int>(k)] = w;
                    sum = w + sum;
                }
                const double w = (*kernel_y)[static_cast<int>(k)];
                const uint8_t *p = row(static_cast<int>(first + k)) + x * 4;
                for (int c = 0; c < 4; c++) acc[c] = static_cast<double>(p[c]) * w + acc[c];
            }
            uint8_t *out = &guest::at<uint8_t>(dst.pixels + dst.pitch * static_cast<uint32_t>(y) + static_cast<uint32_t>(x) * 4);
            for (int c = 0; c < 4; c++) {
                out[c] = channel(acc[c], sum);
                acc[c] = 0.0;
            }
        }
    }

    for (int y = 0; y < src.h; y++) guest::call_cdecl(0x496bd7, guest::at<uint32_t>(rows + 4u * static_cast<uint32_t>(y)));  // free
    guest::call_cdecl(0x496bd7, rows);
    if (kernel_y->first) guest::call_stdcall(0x401ed0, kernel_y->first, (kernel_y->end.a - kernel_y->first.a) >> 3);
    if (kernel_x->first) guest::call_stdcall(0x401ed0, kernel_x->first, (kernel_x->end.a - kernel_x->first.a) >> 3);
}

// Converts a loaded image to the display format, scaled to w × h if it is not that size already (with
// alpha if it has any). Frees the source surface.
SdlSurfaceRef surface_fit(SdlSurfaceRef src, int w, int h) {
    bool alpha = surface_has_alpha(src);
    SdlSurface *s = sdl::surface(src);
    if (s->w == w && s->h == h) {
        if (alpha) {
            SdlSurfaceRef out = guest::call_import(IMP_SDL_DisplayFormatAlpha, src);
            guest::call_import(IMP_SDL_SetAlpha, src, 0x10000u, 0u);  // SDL_RLEACCEL
            sdl::FreeSurface(src);
            return out;
        }
        SdlSurfaceRef out = guest::call_import(IMP_SDL_DisplayFormat, src);
        sdl::FreeSurface(src);
        return out;
    }
    SdlSurfaceRef scaled = guest::call_import(IMP_SDL_CreateRGBSurface, 0u, w, h, 32u, 0xffu, 0xff00u, 0xff0000u, 0xff000000u);
    SdlSurfaceRef rgba = guest::call_import(IMP_SDL_ConvertSurface, src, sdl::surface(scaled)->format.a, 0u);
    sdl::FreeSurface(src);
    scale_surface(rgba, scaled);
    sdl::FreeSurface(rgba);
    SdlSurfaceRef out = guest::call_import(alpha ? IMP_SDL_DisplayFormatAlpha : IMP_SDL_DisplayFormat, scaled);
    if (alpha) guest::call_import(IMP_SDL_SetAlpha, out, 0x10000u, 0u);  // here on the result, not the source
    sdl::FreeSurface(scaled);
    return out;
}

GLOBAL(int32_t, g_screenshot_number, 0x4ab730);  // next number to try

// F12 / F11: saves the screen as screens\\shotNNNN.bmp, the first number not taken (w x h: 0 x 0 the
// screen size, else scaled to that size; see screenshot_save).
void screenshot_take(uint32_t w, uint32_t h) {
    guest::Local<MsString> path;
    path->init();
    enter_game_dir();
    guest::call_cdecl(guest::at<uint32_t>(IMP__mkdir), 0x49d730u);  // "screens"
    for (;;) {
        int n = g_screenshot_number++;
        guest::Local<MsString> dir, t1, t2;
        dir->init();
        dir->assign(&guest::at<char>(0x49d730), 7);
        MsString::concat_move(t1.get(), dir.get(), 0x49ccfcu);  // + "\\"
        uint32_t name = guest::call_cdecl(0x46f320, 0x49d738u, static_cast<uint32_t>(n));  // sprintf "shot%04d.bmp"
        MsString::concat_move(t2.get(), t1.get(), name);
        if (path.get() != t2.get()) {
            path->destroy();
            path->move_assign(t2.get());
        }
        t2->destroy();
        t1->destroy();
        dir->destroy();
        guest::Local<GameFile> f;
        file_construct(f.get());
        file_open(f.get(), path.get(), FM_IN);
        bool taken = f->is_open();
        if (taken) file_close(f.get());
        file_destruct(f.get());
        if (!taken) break;
    }
    screenshot_save(path.get(), w, h);
    path->destroy();
}

namespace {
// One channel of a built-in palette ramp: to_int(t * k + c), t running from 0 in equal steps.
struct Chan {
    float k, c;
    int at(float t) const { return static_cast<int>(guest::to_int64(t * k + c)); }
};
struct Ramp {
    int first, count;  // palette entries
    float step;
    Chan r, g, b;
};
constexpr float k29 = 0.03448275849223137f, k14 = 0.0714285746216774f, k7 = 0.1428571492433548f;
// clang-format off
const Ramp kRamps[] = {
    {30, 15, k14, {100, 60}, {120, 100}, {60, 40}},
    {45, 15, k14, {90, 70}, {75, 60}, {60, 50}},
    {60, 15, k14, {120, 100}, {110, 70}, {120, 40}},
    {75, 15, k14, {90, 160}, {110, 110}, {120, 90}},
    {90, 15, k14, {85, 160}, {70, 180}, {55, 200}},
    {105, 5, 0.25f, {55, 200}, {50, 200}, {45, 200}},
    {110, 5, 0.25f, {55, 200}, {50, 200}, {60, 0}},
    {115, 5, 0.25f, {55, 200}, {60, 0}, {60, 0}},
    {144, 3, 0.5f, {10, 220}, {10, 220}, {10, 200}},
    {148, 3, 0.5f, {30, 210}, {60, 0}, {60, 0}},
    {152, 3, 0.5f, {20, 170}, {30, 180}, {30, 100}},
    {156, 3, 0.5f, {40, 50}, {80, 100}, {20, 40}},
    {160, 3, 0.5f, {64, 96}, {80, 120}, {20, 30}},
    {164, 3, 0.5f, {20, 100}, {20, 90}, {30, 50}},
    {168, 3, 0.5f, {60, 20}, {60, 20}, {60, 20}},
    {172, 3, 0.5f, {30, 210}, {30, 210}, {60, 0}},
    {176, 3, 0.5f, {40, 215}, {40, 215}, {40, 215}},
    {180, 3, 0.5f, {20, 40}, {20, 115}, {20, 235}},
    {184, 3, 0.5f, {60, 140}, {50, 170}, {40, 200}},
    {188, 3, 0.5f, {20, 180}, {20, 100}, {20, 60}},
    {248, 8, k7, {191, 64}, {95, 160}, {0, 255}},
};
// clang-format on
const uint32_t kFixed[8] = {0x006496, 0x00324b, 0xffffff, 0xff6464, 0xff0000, 0xffff00, 0xc0c000, 0x00ff00};  // 240..247

int half(uint32_t v, int byte, float add) {  // to_int(channel / 2 + add)
    return static_cast<int>(guest::to_int64(static_cast<float>((v >> (8 * byte)) & 0xff) * 0.5f + add));
}
}  // namespace

// Sets the palette chosen in the options (0x46b580): palettes\<name>.pal, 256 RGBA entries after a
// 0x18-byte header, of which entries 0..191 and 240..255 are used (192..239 are the cars' and the
// game's own colours and stay). If the file cannot be read, a built-in palette is made instead: a grey
// ramp 0..29, colour ramps (kRamps), darker copies of every fifth entry (120..143) and of the 3-entry
// ramps' second entries (147, 151, ... 191, in the gaps), and fixed colours 240..247.
void palette_reset() {
    guest::Local<uint32_t[256]> pal;
    palette_store(*pal);
    enter_game_dir();
    guest::Local<GameFile> f;
    file_construct(f.get());
    guest::Local<MsString> name;
    name->init();
    name->assign(&guest::at<char>(0x49cdbc), 9);  // "palettes\\"
    const char *chosen = g_race_options.text[2];
    name->append(guest::addr(chosen), static_cast<uint32_t>(std::strlen(chosen)));
    name->append(0x49cdc8u, 4);  // ".pal"
    if (file_open(f.get(), name.get(), FM_IN | FM_BINARY)) {
        guest::Local<uint8_t[0x400]> raw;
        file_seek(f.get(), 0x18, 0);
        int got = file_read(f.get(), *raw, 0x400);
        file_close(f.get());
        if (got == 0x400) {
            uint32_t file_pal[256];
            for (int i = 0; i < 256; i++) file_pal[i] = (*raw)[4 * i] << 16 | (*raw)[4 * i + 1] << 8 | (*raw)[4 * i + 2];
            for (int i = 0; i < 192; i++) (*pal)[i] = file_pal[i];
            for (int i = 240; i < 256; i++) (*pal)[i] = file_pal[i];
            palette_load(*pal);
            name->destroy();
            file_destruct(f.get());
            return;
        }
    }
    name->destroy();
    file_destruct(f.get());

    uint32_t *p = *pal;
    float t = 0.0f;
    for (int i = 0; i < 30; i++, t += k29) p[i] = static_cast<uint32_t>(Chan{200, 0}.at(t) * 0x10101);
    auto ramp = [&](const Ramp &r) {
        float s = 0.0f;
        for (int i = 0; i < r.count; i++, s += r.step)
            p[r.first + i] = static_cast<uint32_t>((r.r.at(s) * 0x100 + r.g.at(s)) * 0x100 + r.b.at(s));
    };
    for (int k = 0; k < 8; k++) ramp(kRamps[k]);
    for (int k = 0; k < 24; k++) {  // darker copies of entries 2, 7, ... 117
        uint32_t v = p[2 + 5 * k];
        p[120 + k] = static_cast<uint32_t>((half(v, 2, 0.0f) * 0x100 + half(v, 1, 64.0f)) * 0x100 + 0x80 + half(v, 0, 0.0f));
    }
    for (int k = 8; k < 20; k++) ramp(kRamps[k]);
    for (int e = 144; e < 192; e += 4) {  // entry e + 3: a darker copy of e + 1
        uint32_t v = p[e + 1];
        p[e + 3] = static_cast<uint32_t>((half(v, 2, 0.0f) * 0x100 + half(v, 1, 64.0f)) * 0x100 + half(v, 0, 128.0f));
    }
    for (int i = 0; i < 8; i++) p[240 + i] = kFixed[i];
    ramp(kRamps[20]);
    for (int i = 0; i < 256; i++) p[i] &= 0x00ffffff;
    palette_load(p);
}
