// Draw callbacks for text fields and sliders, shared by all menu screens.
#include <cstring>

#include "game/driver.h"
#include "game/gui.h"
#include "game/text.h"
#include "orig/render.h"


constexpr uint32_t STR_CURSOR = 0x49d7f4;  // "|"
constexpr uint32_t STR_SPACE = 0x49cc4c;   // " "

// A framed box with the text (and a cursor while editing).
void text_field_draw(Widget *w) {
    if (w->type != WIDGET_TEXT || (w->flags & WF_HIDDEN)) return;
    int border = g_menu_border;
    int x = guest::to_int(w->x), y = guest::to_int(w->y);
    int x2 = guest::to_int(w->w) + x, y2 = guest::to_int(w->h) + y;
    int16_t fx1 = static_cast<int16_t>(x - border), fy1 = static_cast<int16_t>(static_cast<int16_t>(y) - static_cast<int16_t>(border));
    if (g_back) {
        guest::Local<SdlRect> r;
        *r = rect(fx1, fy1, static_cast<int16_t>(border + x2) - fx1,
                  (static_cast<int16_t>(y2) + static_cast<int16_t>(border)) - fy1);
        sdl::FillRect(g_back, r.get(), 0xf1);
        if (g_back) {
            *r = rect(x, y, static_cast<int16_t>(x2) - static_cast<int16_t>(x), static_cast<int16_t>(y2) - static_cast<int16_t>(y));
            sdl::FillRect(g_back, r.get(), 100);
        }
    }
    lock_surface(SURF_BACK);
    Box box = {g_font_size + x, y, x2 - g_font_size, y2};
    guest::Local<MsString> shown, tmp;
    if (!(w->state & WS_EDITING)) {
        tmp->init();
        tmp->assign(w->label);
    } else {
        MsString::concat(tmp.get(), &w->label, STR_CURSOR);
    }
    shown->move_from(tmp.get());
    tmp->destroy();
    draw_text(shown.get(), &box, 0xf0, TEXT_CENTER_Y);
    unlock_surface();
    shown->destroy();
}

static void draw_symbol(uint32_t str, const Box *box, uint8_t color) {
    guest::Local<MsString> s;
    s->construct(str);
    draw_text(s.get(), box, color, TEXT_CENTER_X | TEXT_CENTER_Y);
    s->destroy();
}

// [-]  ====value bar====  [+]   with "value label" centred on it. The label may give singular and
// plural forms as "point|points".
void slider_draw(Widget *w) {
    if (w->type != WIDGET_SLIDER || (w->flags & WF_HIDDEN)) return;
    int border = g_menu_border;
    int x = guest::to_int(w->x), y = guest::to_int(w->y);
    float width = w->w;
    Box frame = {x - border, y - border, guest::to_int(width + w->x) + border, guest::to_int(w->h + w->y) + border};
    int h = guest::to_int(w->h);
    int plus_x = guest::to_int(width - static_cast<float>(h)) + x;
    int bar_x = (h * 3) / 2;
    int fill_x2 = guest::to_int((static_cast<float>(w->pos - w->min) / static_cast<float>(w->max - w->min)) *
                                (width - static_cast<float>(h * 3))) +
                  bar_x + x;
    fill_box(0xf1, &frame);
    fill_rect((w->state & (WS_DOWN | WS_DEC_DOWN)) == (WS_DOWN | WS_DEC_DOWN) ? 0x5a : 100, static_cast<int16_t>(x),
              static_cast<int16_t>(y), static_cast<uint16_t>(h), static_cast<uint16_t>(h));
    if (g_back) {  // track
        guest::Local<SdlRect> r;
        int16_t tx = static_cast<int16_t>(border + x + h), ty = static_cast<int16_t>(y - border);
        *r = rect(tx, ty, static_cast<int16_t>(plus_x - border) - tx,
                  (static_cast<int16_t>(border) + static_cast<int16_t>(y) + static_cast<int16_t>(h)) - ty);
        sdl::FillRect(g_back, r.get(), 100);
    }
    if (g_back) {  // value
        guest::Local<SdlRect> r;
        int16_t bx = static_cast<int16_t>(static_cast<int16_t>(bar_x) + static_cast<int16_t>(x));
        *r = rect(bx, y, static_cast<int16_t>(fill_x2) - bx, h);
        sdl::FillRect(g_back, r.get(), 0x5a);
    }
    Box text_box = {guest::to_int(w->x), guest::to_int(w->y), guest::to_int(w->x + w->w), guest::to_int(w->y + w->h)};
    lock_surface(SURF_BACK);
    if (w->id != 1) {
        guest::Local<MsStringStream> ss;
        guest::Local<MsString> one, many, text;
        ss->construct();
        (*ss << w->pos).put(STR_SPACE);
        int bar = w->label.find('|');
        if (bar < 0) {
            *ss << &w->label;
        } else {
            w->label.substr(one.get(), 0, static_cast<uint32_t>(bar));
            w->label.substr(many.get(), static_cast<uint32_t>(bar) + 1, 0xffffffffu);
            *ss << (w->pos != 1 ? many.get() : one.get());
            many->destroy();
            one->destroy();
        }
        ss->str(text.get());
        draw_text(text.get(), &text_box, 0xf1, TEXT_CENTER_X | TEXT_CENTER_Y);
        text->destroy();
        ss->destruct();
    }
    Box minus = {border + x, border + y, (x - border) + h, (y - border) + h};
    draw_symbol(STR_MINUS, &minus, 0xf1);
    Box plus = {border + plus_x, border + y, (plus_x - border) + h, (y - border) + h};
    draw_symbol(STR_PLUS, &plus, (w->state & (WS_DOWN | WS_INC_DOWN)) == (WS_DOWN | WS_INC_DOWN) ? 0x5a : 100);
    unlock_surface();
}

// Draws a C string (in guest memory) the way the original does: through a temporary std::string.
static void draw_cstr(const char *str, const Box *box, uint8_t color, uint32_t flags) {
    guest::Local<MsString> s;
    s->init();
    s->assign(str, static_cast<uint32_t>(std::strlen(str)));
    draw_text(s.get(), box, color, flags);
    s->destroy();
}

// List box: items in a grid (columns x visible rows) and a scrollbar on the right with arrows at both
// ends and a thumb sized to the visible part. Item backgrounds show marked/selected items. Widget ids
// select the item style: 1 driver list (humans highlighted), 20+p car list for player p (with car
// previews), others plain text.
void list_draw(Widget *w) {
    if (w->type != WIDGET_LIST || (w->flags & WF_HIDDEN)) return;
    int border = g_menu_border;
    int x = guest::to_int(w->x), y = guest::to_int(w->y);
    ListData *d = w->list;
    int sb = guest::to_int(d->scrollbar_w);
    int sb_x = guest::to_int(w->w + w->x) - sb;
    int down_y = (guest::to_int(w->h) - sb) + y;  // top of the lower arrow
    int n = d->items.size();
    if (g_back) {
        guest::Local<SdlRect> r;
        int fx1 = x - border, fy1 = y - border, fx2 = sb_x + border + sb, fy2 = border + down_y + sb;
        *r = rect(fx1, fy1, fx2 - fx1, fy2 - fy1);
        sdl::FillRect(g_back, r.get(), 0xf1);
        if (g_back) {
            *r = rect(x, y, (sb - x) + sb_x, (down_y - y) + sb);
            sdl::FillRect(g_back, r.get(), 100);
        }
    }
    int track = down_y - sb - y, thumb_len, thumb_y;
    if (w->max == 0) {
        thumb_len = track;
        thumb_y = 0;
    } else {
        thumb_len = (n - w->max) * track / n;
        int min_len = guest::to_int(w->list->scrollbar_w);
        if (thumb_len < min_len) thumb_len = min_len;
        thumb_y = (down_y - thumb_len - sb - y) * w->pos / w->max;
    }
    fill_rect(0xf1, static_cast<int16_t>(sb_x), static_cast<int16_t>(thumb_y + sb + y), static_cast<uint16_t>(sb),
              static_cast<uint16_t>(thumb_len));

    d = w->list;
    int col_w = guest::to_int(d->column_w), row_h = guest::to_int(d->row_h);
    int first = w->pos * d->columns;
    int last = d->visible_rows * d->columns + first;
    if (last > n) last = n;
    auto cell = [&](int k, int indent) {
        int cols = w->list->columns, r = k / cols, c = k % cols;
        return Box{c * col_w + indent + x, r * row_h + y, (c + 1) * col_w + x, (r + 1) * row_h + y};
    };
    for (int i = first, k = 0; i < last; i++, k++) {  // item backgrounds
        Box b = cell(k, 0);
        ListData *ld = w->list;
        uint8_t color;
        if (ld->items[i]->marked == 0) {
            color = (i == ld->selected && w->id == 2) ? 0x5a : 100;
        } else if (w->id == 0) {
            color = 0x5a;
        } else {
            color = 0x5f;
            ld->items[ld->selected]->marked = 0;
        }
        fill_box(color, &b);
    }

    SdlSurfaceRef back = g_back;  // (lock_surface, inlined)
    if (sdl::LockSurface(back) == 0) {
        g_pixels = sdl::surface(back)->pixels;
        g_pitch = sdl::surface(back)->pitch;
        g_locked = back;
    }
    Box up = {sb_x, y, sb_x + sb, y + sb};
    draw_cstr(&guest::at<char>(STR_MINUS), &up, 0xf1, TEXT_CENTER_X | TEXT_CENTER_Y);
    Box down = {sb_x, down_y, sb_x + sb, down_y + sb};
    draw_cstr(&guest::at<char>(STR_PLUS), &down, 0xf1, TEXT_CENTER_X | TEXT_CENTER_Y);

    uint32_t id = static_cast<uint32_t>(w->id);
    for (int i = first, k = 0; i < last; i++, k++) {
        if (id / 10 == 2) {  // cars
            int s = g_font_size;
            Box b = cell(k, 0);
            int half = (s * 3) / 2;
            draw_car_preview(static_cast<int8_t>(w->id) - 20, &g_cars.get()[i], half + b.x1, half + b.y1, 120.0f);
            b.x1 += s * 4;
            draw_cstr(w->list->items[i]->text.c_str(), &b, 0xf0, TEXT_CENTER_Y);
        } else {
            Box b = cell(k, g_font_size);
            ListItem *item = w->list->items[i];
            uint8_t color = 0xf0;
            if (id == 1) color = guest::at<Driver>(item->data).human() ? 0xf0 : 0x5a;  // drivers
            draw_cstr(item->text.c_str(), &b, color, TEXT_CENTER_Y);
        }
    }
    unlock_surface();
}
