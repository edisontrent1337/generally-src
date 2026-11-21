#include "game/menu_page.h"

#include <cstring>

#include "game/gui.h"
#include "game/text.h"

static int origin(int screen, int units) {
    return static_cast<int>(guest::to_int64(static_cast<float>(screen - g_font_size * units) * 0.5f + 0.5f));
}
int layout_x0() { return origin(g_screen_w, 80); }
int layout_y0() { return origin(g_screen_h, 60); }

static void bar(int x, int y, int w, int h) {
    guest::Local<SdlRect> r;
    *r = rect(x, y, w, h);
    sdl::FillRect(g_back, r.get(), 0x5a);
}

static void draw_cstr_at(const char *s, int x, int y, uint8_t color) {
    guest::Local<MsString> t;
    t->init();
    t->assign(s, static_cast<uint32_t>(std::strlen(s)));
    draw_text_at(t.get(), x, y, color, false);
    t->destroy();
}

// Clears the screen and draws the page frame with its title and captions (empty ones are skipped).
void draw_page(const MsString *title, const PageLabel *labels, int count) {
    if (g_back) sdl::FillRect(g_back, nullptr, 100);
    int f = g_font_size, x0 = layout_x0(), y0 = layout_y0();
    if (g_back) {
        bar(x0 + f * 2, y0 + f * 4, f * 76, g_menu_border);
        if (g_back) bar(x0 + g_font_size * 2, g_font_size * 56 - g_menu_border + y0, g_font_size * 76, g_menu_border);
    }
    lock_surface(SURF_BACK);
    f = g_font_size;
    draw_text_at(title, x0 + f * 4, f + y0, 0x73, false);
    draw_cstr_at(&guest::at<char>(0x49cd88) /* "GeneRally" */, g_font_size * 67 + x0, g_font_size + y0, 0xf0);
    if (labels) {
        for (int i = 0; i < count; i++) {
            const PageLabel &l = labels[i];
            if (l.text.size == 0) continue;
            draw_cstr_at(l.text.c_str(), l.x * g_font_size + x0, l.y * g_font_size + y0, static_cast<uint8_t>(l.color));
        }
    }
    unlock_surface();
}

// the default constructor (only the low byte of color is set)
PageLabel *page_label_construct(PageLabel *l) {
    l->x = 0;
    l->y = 0;
    reinterpret_cast<uint8_t &>(l->color) = 0;
    l->text.init();
    return l;
}

PageLabel *page_label_construct(PageLabel *l, int32_t x, int32_t y, uint8_t color, const MsString *text) {
    l->x = x;
    l->y = y;
    reinterpret_cast<uint8_t &>(l->color) = color;
    l->text.init();
    l->text.assign(*text);
    return l;
}

static void fill(int x, int y, int w, int h, uint32_t color) {
    guest::Local<SdlRect> r;
    r->x = static_cast<int16_t>(x);
    r->y = static_cast<int16_t>(y);
    r->w = static_cast<uint16_t>(w);
    r->h = static_cast<uint16_t>(h);
    sdl::FillRect(g_back, r.get(), color);
}

// A dialog box over the current page: box = {left, top, right, bottom} in layout units; framed, with a
// line below the title and one above the buttons, the title (if any) and the captions (empty ones are
// skipped).
void draw_dialog(const int32_t box[4], const MsString *title, const PageLabel *labels, int count) {
    int f = g_font_size, b = g_menu_border;
    int x0 = layout_x0(), y0 = layout_y0();
    int left = box[0] * f + x0, right = box[2] * f + x0;
    int top = box[1] * f + y0, bottom = box[3] * f + y0;
    if (g_back) {
        fill(left - b, top - b, right - (left - b) + b, bottom - (top - b) + b, 0xf0);
        if (g_back) fill(left, top, right - left, bottom - top, 100);
    }
    f = g_font_size;
    if (g_back) fill(left + f * 2, bottom - f * 4 - b, (right - f * 2) - (left + f * 2), b, 0x5a);
    f = g_font_size;
    if (g_back) fill(left + f * 2, top + f * 4, (right - f * 2) - (left + f * 2), b, 0x5a);
    lock_surface(SURF_BACK);
    if (title->size != 0) draw_text_at(title, (box[0] + 4) * g_font_size + x0, (box[1] + 1) * g_font_size + y0, 0x73, 0);
    if (labels && count > 0) {
        for (; count != 0; count--, labels++) {
            if (labels->text.size == 0) continue;
            draw_cstr_at(labels->text.c_str(), labels->x * g_font_size + x0, labels->y * g_font_size + y0,
                         static_cast<uint8_t>(labels->color));
        }
    }
    if (g_locked) {
        g_pitch = 0;
        g_pixels = 0;
        sdl::UnlockSurface(g_locked);
        g_locked = SdlSurfaceRef{};
    }
}
