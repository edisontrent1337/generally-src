// Drawing of the main menu's buttons (the toolkit's button draw callback).
//
// Widget ids: 0/1 headings, 5/6/7 framed buttons, 10+p the name of player p, 20+p player p's car.
#include <cstring>

#include "game/menu.h"
#include "game/text.h"
#include "orig/render.h"
#include "orig/menu.h"


enum : uint8_t {
    COL_FRAME = 0xf1,
    COL_TEXT = 0xf1,
    COL_TEXT_FOCUS = 0x73,
    COL_BUTTON = 100,
    COL_BUTTON_DOWN = 0x5f,
    COL_BUTTON_FOCUS = 0x61,
    COL_LINE = 0x5a,
    COL_NAME = 0x5a,
    COL_NAME_HUMAN = 0xf0,
};

// The view of the car previews: camera axes and zoom.
static void draw_name(const char *name, const Box *box, const Driver *d) {
    guest::Local<MsString> s;
    s->init();
    s->assign(name, static_cast<uint32_t>(std::strlen(name)));
    draw_text(s.get(), box, d->human() ? COL_NAME_HUMAN : COL_NAME, TEXT_CENTER_Y);
    s->destroy();
}

void widget_draw(Widget *w) {
    if (w->type != WIDGET_BUTTON || (w->flags & WF_HIDDEN)) return;
    int border = g_menu_border;
    Box b = {guest::to_int(w->x), guest::to_int(w->y), guest::to_int(w->w + w->x), guest::to_int(w->h + w->y)};
    int x2 = b.x2, y2 = b.y2;
    if (w->id == 5) {  // framed
        fill_rect_corners(COL_FRAME, static_cast<int16_t>(static_cast<int16_t>(b.x1) - static_cast<int16_t>(border)),
                          static_cast<int16_t>(static_cast<int16_t>(b.y1) - static_cast<int16_t>(border)),
                          static_cast<int16_t>(x2), static_cast<int16_t>(y2));
        b.x2 = x2 - border;
    }
    int right = b.x2;
    uint8_t color;
    if (w->id == 5 || w->id == 6) {
        y2 -= border;
        color = (w->state & WS_DOWN) ? COL_BUTTON_DOWN : COL_BUTTON;
        b.y2 = y2;
    } else if (w->id == 7) {
        y2 -= border;
        color = COL_BUTTON;
        b.y2 = y2;
    } else if (w->state & WS_DOWN) {
        color = COL_BUTTON_DOWN;
    } else if ((w->state & WS_FOCUS) && w->variant == 3) {
        color = COL_BUTTON_FOCUS;
    } else {
        color = COL_BUTTON;
    }
    fill_box(color, &b);
    uint32_t group = static_cast<uint32_t>(w->id) / 10;
    if (group == 1 || group == 2)  // underline the player rows
        fill_rect_corners(COL_LINE, static_cast<int16_t>(b.x1),
                          static_cast<int16_t>(static_cast<int16_t>(y2) - static_cast<int16_t>(border)),
                          static_cast<int16_t>(right), static_cast<int16_t>(y2));

    uint32_t id = static_cast<uint32_t>(w->id);
    if (id / 10 == 2) {  // player's car: model name and a small rendering
        if (g_player_count <= static_cast<int>(id - 20)) return;
        lock_surface(SURF_BACK);
        Driver *d = g_player_driver[id - 20];
        CarModel *car = &g_cars.get()[d->car];
        b.x1 += (13 - static_cast<int>(std::strlen(car->name))) * g_font_size;  // right-aligned in 13 columns
        draw_name(car->name, &b, d);
        int p = w->id - 20;
        draw_car_preview(p, &g_cars.get()[g_player_driver[p]->car], right - g_font_size * 2, (b.y1 + y2) / 2, 120.0f);
    } else {
        if (id != 0 && id != 1 && g_player_count <= static_cast<int>(id - 10)) return;
        lock_surface(SURF_BACK);
        id = static_cast<uint32_t>(w->id);
        if (id / 10 == 1) {  // player's name
            int p = static_cast<int>(id) - 10;
            if (p < g_player_count) {
                b.x2 = right + g_font_size;
                b.x1 += g_font_size;
                draw_name(g_player_name[p], &b, g_player_driver[p]);
            }
        } else {
            uint32_t flags;
            uint8_t col;
            if (id == 1) {
                flags = TEXT_CENTER_Y;
                col = COL_TEXT;
            } else if (w->variant == 3) {
                flags = TEXT_CENTER_X | TEXT_CENTER_Y;
                col = (w->state & WS_FOCUS) ? COL_TEXT_FOCUS : COL_TEXT;
            } else if (id != 7) {
                flags = TEXT_CENTER_X | TEXT_CENTER_Y;
                col = COL_TEXT;
            } else {
                flags = TEXT_CENTER_Y;
                col = COL_LINE;
            }
            draw_text(&w->label, &b, col, flags);
        }
    }
    unlock_surface();
}
