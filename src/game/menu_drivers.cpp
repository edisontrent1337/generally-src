// Main menu: choosing the drivers of the next race.
//
// The available drivers are in a list box; clicking one moves it into the next free player slot
// (name button + car). Clicking a player's name button puts the driver back into the list.
#include <cstring>

#include "game/menu.h"
#include "game/driver.h"
#include "game/file.h"
#include "game/video.h"
#include "orig/menu.h"

static Widget &widget(int i) { return g_menu_widgets.get()[i]; }

// Palette entries for a car colour c: a darker shade (3/4), the colour, the shade lightened, a pale tint.
void car_shades(uint32_t *pal, uint32_t c) {
    int32_t r = static_cast<int32_t>(c >> 16), g = static_cast<int32_t>(c >> 8 & 0xff), b = static_cast<int32_t>(c & 0xff);
    uint32_t dark = static_cast<uint32_t>(((r * 3 / 4) * 0x100 + g * 3 / 4) * 0x100 + b * 3 / 4);
    pal[0] = dark;
    pal[1] = (static_cast<uint32_t>(r) * 0x100 + static_cast<uint32_t>(g)) * 0x100 + static_cast<uint32_t>(b);
    pal[2] = dark + 0x404040;
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&pal[1]);  // b, g, r
    uint32_t tr = static_cast<uint32_t>(guest::to_int64(static_cast<float>(bytes[2]) * 0.5f));
    uint32_t tg = static_cast<uint32_t>(guest::to_int64(static_cast<float>(bytes[1]) * 0.5f + 64.0f));
    uint32_t tb = static_cast<uint32_t>(guest::to_int64(static_cast<float>(pal[1] & 0xff) * 0.5f + 128.0f));
    pal[3] = (tr * 0x100 + tg) * 0x100 + tb;
}

// Puts driver d (if given) into player slot `player` and sets that player's car colours in the palette
// (entries 192 + 8 * player: four for each of the two colours).
void set_player_driver(int player, Driver *d) {
    if (static_cast<uint32_t>(player) >= kMaxPlayers) return;
    if (d) g_player_driver[player] = d;
    Driver *cur = g_player_driver[player].get();
    if (!cur) return;
    guest::Local<uint32_t[256]> pal;
    palette_store(*pal);
    car_shades(*pal + 192 + 8 * player, cur->color[0]);
    car_shades(*pal + 196 + 8 * player, cur->color[1]);
    palette_load(*pal);
}

void menu_add_driver(Widget *list) {
    if (g_player_count >= kMaxPlayers) return;
    ListData *d = list->list;
    int sel = d->selected;
    if (d->items.size() == 0 || sel < 0 || sel >= d->items.size()) return;

    set_player_driver(g_player_count, ptr32<Driver>::from_addr(d->items[sel]->data));
    ListItem *item = list->list->items[sel];
    std::strncpy(g_player_name[g_player_count], item->text.c_str(), kNameLen);
    list_remove(list, list->list->selected);
    g_player_count++;
    list->list->previous = -1;
    list->list->selected = -1;
    if (g_player_count <= kMaxPlayers) {
        widget_draw(&widget(W_DRIVER_NAME + g_player_count));
        widget_draw(&widget(W_DRIVER_CAR + g_player_count));
    }
    list_draw(list);
}

void menu_remove_driver(Widget *button) {
    int slot = button->id - W_DRIVER_NAME;  // 1-based
    if (slot > g_player_count) return;
    int player = slot - 1;

    {   // back into the list, with its colour
        guest::Local<MsString> name;
        name->init();
        name->assign(g_player_name[player], std::strlen(g_player_name[player]));
        list_add(&widget(W_DRIVER_LIST), name.get());
        name->destroy();
    }
    g_driver_list->items.back()->data = g_player_driver[player].a;

    // close the gap
    for (int i = slot; i < g_player_count; i++) {
        char *dst = g_player_name[i - 1], *src = g_player_name[i];
        do *dst++ = *src; while (*src++);
        MsString &to = widget(W_DRIVER_NAME + i).label, &from = widget(W_DRIVER_NAME + i + 1).label;
        if (&to != &from) to.assign(from);
        set_player_driver(i - 1, g_player_driver[i]);
        if (i <= kMaxPlayers) {
            widget_draw(&widget(W_DRIVER_NAME + i));
            widget_draw(&widget(W_DRIVER_CAR + i));
        }
    }
    int old_count = g_player_count;
    g_player_count = old_count - 1;
    g_player_name[g_player_count][0] = 0;
    if (g_player_count < kMaxPlayers) {
        widget_draw(&widget(W_DRIVER_NAME + old_count));
        widget_draw(&widget(W_DRIVER_CAR + g_player_count + 1));
    }
    list_draw(&widget(W_DRIVER_LIST));
}

// Fills the main menu's driver list with every drivers/*.drv: one item per file (in listing order), each
// with its driver loaded into a new 0x8c-byte record kept in the item. The list becomes g_driver_list.
void driver_list_fill(Widget *list) {
    enter_game_dir();
    guest::Local<MsVector<MsString>> names;
    guest::Local<MsString> ext, dir;
    names->first = nullptr;
    names->last = nullptr;
    names->end = nullptr;
    ext->init();
    ext->assign(&guest::at<char>(0x49d7c0), 3);  // "drv"
    dir->init();
    dir->assign(&guest::at<char>(0x49d7c4), 7);  // "drivers"
    list_directory(dir.get(), ext.get(), names.get(), 1, 0xc);
    dir->destroy();
    ext->destroy();
    uint32_t n = static_cast<uint32_t>(names->size()), i = 0;
    for (; i < n; i++) {
        MsString *name = &names->first.get()[i];
        list_add(list, name);
        Driver *d = static_cast<Driver *>(guest::op_new(sizeof(Driver)));
        driver_load(d, name);
        list->list->items[static_cast<int>(i)]->data = guest::addr(d);
    }
    g_driver_list = list->list;
    list->id = 1;
    if (names->first) {
        guest::call_regs(0x4050e0, names->first.a, names->last.a, i, n);  // destroy the strings
        guest::call_stdcall(0x404c00, names->first, (names->end.a - names->first.a) / static_cast<uint32_t>(sizeof(MsString)));
    }
}
