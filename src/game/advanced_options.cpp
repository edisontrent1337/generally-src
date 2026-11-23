#include "game/advanced_options.h"

#include <cstring>

#include "game/driver.h"
#include "game/driver_options.h"
#include "game/file.h"
#include "game/file_dialog.h"
#include "game/main.h"
#include "game/menu.h"
#include "game/message_box.h"
#include "game/name_entry.h"
#include "game/race_options.h"
#include "game/race_state.h"
#include "game/replay.h"
#include "game/text.h"
#include "game/track_info.h"
#include "game/lang.h"
#include "game/menu_page.h"
#include "game/video.h"

void adv_draw_labels();

static Widget &widget(int i) { return g_adv_widgets.get()[i]; }

static void unmark_all(ListData *d) {
    d->previous = -1;
    d->selected = -1;
    for (uint32_t i = 0; i < static_cast<uint32_t>(d->items.size()); i++) d->items[static_cast<int>(i)]->marked = 0;
}

// Back to the normal page: every widget visible except the dropdown lists.
static void show_page() {
    for (int i = 0; i < ADV_COUNT; i++) widget(i).flags &= ~WF_HIDDEN;
}
static void hide_lists_and_refresh() {
    widget(ADV_SOUNDSET_LIST).flags |= WF_HIDDEN;
    widget(ADV_LANGUAGE_LIST).flags |= WF_HIDDEN;
    widget(ADV_RESOLUTION_LIST).flags |= WF_HIDDEN;
    widget(ADV_PALETTE_LIST).flags |= WF_HIDDEN;
    if (g_saved_gui) widget(ADV_BACK).flags |= WF_HIDDEN;
    adv_draw_labels();
    gui_redraw_all();
}

// A dropdown button: opens its list (alone on the page, with the button above it), or closes it again.
void adv_open_dropdown(int list, int button) {
    if (!(widget(list).flags & WF_HIDDEN)) {
        unmark_all(widget(list).list);
        show_page();
        hide_lists_and_refresh();
        return;
    }
    for (int i = 0; i < ADV_COUNT; i++) widget(i).flags |= WF_HIDDEN;
    widget(list).flags &= ~WF_HIDDEN;
    widget(button).flags &= ~WF_HIDDEN;
    gui_set_focus(list);
    list_draw(&widget(list));
}

// A list's callback: the clicked item becomes the button's text, the list closes.
void adv_pick(Widget *w, int button) {
    if (!w || !w->list) return;
    ListData *d = w->list;
    if (d->selected >= 0) {
        MsString *text = &d->items[d->selected]->text;
        if (&widget(button).label != text) widget(button).label.assign(*text);
    }
    unmark_all(w->list);
    show_page();
    w->flags |= WF_HIDDEN;
    hide_lists_and_refresh();
}


// The display modes (0x46c570, a vector of {w, h}) as "WxH" items.
void adv_fill_resolutions(Widget *list) {
    guest::Local<MsVector<DisplayMode>> modes;
    guest::Local<char[32]> text;
    guest::Local<MsString> s;
    display_modes(modes.get());
    for (uint32_t i = 0; i < static_cast<uint32_t>(modes->size()); i++) {
        // sprintf(text, "%dx%d", w, h) (0x472560; it passes the caller's ECX, here the vector's data, on
        // as the locale argument)
        guest::call_regs(0x472560, modes->first.a, 0, guest::addr(text.get()), 0x49d7d0u, (*modes)[static_cast<int>(i)].w,
                         (*modes)[static_cast<int>(i)].h);
        s->init();
        s->assign(*text, static_cast<uint32_t>(std::strlen(*text)));
        list_add(list, s.get());
        s->destroy();
    }
    if (modes->first) guest::call_stdcall(0x401ed0, modes->first, (modes->end.a - modes->first.a) >> 3);
}

// The files (or folders, mode 2) of a game folder with the given extension, as list items.
static void fill_from_folder(Widget *list, uint32_t dir, uint32_t dir_len, uint32_t ext, uint32_t ext_len, int mode) {
    enter_game_dir();
    guest::Local<MsVector<MsString>> names;
    guest::Local<MsString> e, d;
    names->first = nullptr;
    names->last = nullptr;
    names->end = nullptr;
    e->init();
    e->assign(&guest::at<char>(ext), ext_len);
    d->init();
    d->assign(&guest::at<char>(dir), dir_len);
    list_directory(d.get(), e.get(), names.get(), mode, 0x16);
    d->destroy();
    e->destroy();
    MsString *first = names->first.get();
    for (int n = names->size(), i = 0; n != 0; n--, i++) list_add(list, &first[i]);
    list->id = 0;
    if (names->first) {
        guest::call_regs(0x4050e0, names->first.a, names->last.a, 0u, 0u);  // destroy the strings
        guest::call_stdcall(0x404c00, names->first, (names->end.a - names->first.a) / static_cast<uint32_t>(sizeof(MsString)));
    }
}

void adv_fill_palettes(Widget *list) { fill_from_folder(list, 0x49d7dc /* "palettes" */, 8, 0x49d7d8 /* "pal" */, 3, 1); }
void adv_fill_languages(Widget *list) { fill_from_folder(list, 0x49d728 /* "lang" */, 4, 0x49d7e8 /* "lng" */, 3, 1); }
void adv_fill_soundsets(Widget *list) { fill_from_folder(list, 0x49d7ec /* "sounds" */, 6, 0x49b574 /* "" */, 0, 2); }

// Draws the page's title and captions.
void adv_draw_labels() {
    static const struct {
        int y, text;  // row, language entry
    } rows[] = {{21, 59}, {7, 163}, {18, 162}, {24, 60}, {27, 61}, {30, 62}, {33, 63},
                {38, 64}, {41, 65}, {44, 66}, {10, 68}, {13, 69}, {49, 122}};
    constexpr int n = sizeof(rows) / sizeof(rows[0]);
    guest::Local<PageLabel[n]> labels;
    for (int i = 0; i < n; i++) {
        PageLabel &l = (*labels)[i];
        l.x = 14;
        l.y = rows[i].y;
        l.color = 0xf0;
        l.text.init();
        l.text.assign(g_lang[rows[i].text]);
    }
    draw_page(&g_lang[L_ADV_TITLE], *labels, n);
    // destroy the array (`eh vector destructor iterator' with the label destructor 0x47db40)
    guest::call_stdcall(0x4970c6, labels.get(), static_cast<uint32_t>(sizeof(PageLabel)), static_cast<uint32_t>(n), 0x47db40u);
}


namespace {
constexpr uint32_t kAutosave = 0x49cce8;  // the autosave game's name (8 characters)

void rescale_all(void (*fn)(Widget *, int, float, float)) {
    // the 640x480 layout (80 x 60 units) is centred on the screen
    float dx = static_cast<float>(g_screen_w - g_font_size * 80) * 0.5f;
    float dy = static_cast<float>(g_screen_h - g_font_size * 60) * 0.5f;
    fn(g_menu_widgets.get(), 0x1e, dx, dy);
    fn(g_options_widgets.get(), 0x11, dx, dy);
    fn(g_adv_widgets.get(), 0x15, dx, dy);
    fn(g_dopt_widgets.get(), 0x18, dx, dy);
    fn(g_car_dropdown_widgets.get(), 2, dx, dy);
    fn(g_pause_widgets.get(), 6, dx, dy);
    fn(g_file_dialog_widgets.get(), 5, dx, dy);
    fn(g_msgbox_widgets.get(), 3, dx, dy);
    fn(g_track_info_widgets.get(), 3, dx, dy);
    fn(g_name_widgets.get(), 2, dx, dy);
    fn(g_inputbox_widgets.get(), 3, dx, dy);
}

// Saves the running race as the autosave game and frees it (before everything is reloaded).
void autosave_race() {
    race_controls_init();
    pause_menu_open();
    guest::Local<MsString> name;
    name->init();
    name->assign(&guest::at<char>(kAutosave), 8);
    game_save(name.get());
    name->destroy();
    race_free();
}

void reset_player_drivers() {
    for (int i = 0; i < g_player_count; i++) set_player_driver(i, nullptr);
}
}  // namespace

// OK (variant 1) or Cancel of Advanced options, also closing Driver options (0x481d00). On OK the
// widgets go back into the race options (the Options screen's too, unless over the pause menu). A new
// display mode or colour depth reloads everything (the race is autosaved first, and the widgets of every
// screen are rescaled to the new size); otherwise a new language reloads everything, a new sound set
// reloads the sounds, a new palette resets the video palette. Then, if Driver options built its working
// copies, those are written back (on OK) and saved, the race's drivers are matched by name and the rest
// go back into the main menu's driver list. Finally the screen below comes back: the paused race, the
// autosaved game, or the main menu.
void adv_options_done(Widget *button) {
    bool autosaved = false;
    RaceOptions &o = g_race_options;
    Widget *a = g_adv_widgets.get(), *d = g_dopt_widgets.get();
    a[ADV_BACK].flags &= ~WF_HIDDEN;
    d[1].flags &= ~WF_HIDDEN;
    d[1].variant = 0;
    d[17].flags &= ~WF_HIDDEN;
    d[17].variant = 0;
    d[18].flags &= ~WF_HIDDEN;
    d[18].variant = 0;
    const uint8_t old_depth = o.b1c;
    if (button->variant == 1) {
        if (!g_saved_gui) {
            Widget *w = g_options_widgets.get();
            o.b09 = static_cast<uint8_t>(w[4].pos);
            o.b0a = static_cast<uint8_t>(w[5].pos);
            o.b0c = static_cast<uint8_t>(w[6].pos);
            o.b0b = static_cast<uint8_t>(w[7].pos);
            for (int i = 0; i < 7; i++) o.b02[i] = static_cast<uint8_t>(w[10 + i].pos);
            o.length_unit = static_cast<uint8_t>(w[8].id - 2);
            o.b0e = static_cast<uint8_t>(w[9].id - 2);
        }
        o.b1c = static_cast<uint8_t>(3 - a[4].id);
        o.w1e = static_cast<uint16_t>(guest::to_int(static_cast<float>(a[6].pos) * 2.56f + 0.5f));
        o.b19 = static_cast<uint8_t>(a[7].id - 2);
        o.b18 = static_cast<uint8_t>(a[8].id - 2);
        o.b1a = static_cast<uint8_t>(a[9].id - 2);
        o.b1b = static_cast<uint8_t>(a[17].id - 2);
        o.b20 = static_cast<uint8_t>(a[10].pos);
        o.b21 = static_cast<uint8_t>(a[11].pos);
        o.b22 = static_cast<uint8_t>(a[12].pos);
        auto take = [](char *dst, const Widget &w) {
            if (ms_stricmp(dst, w.label.c_str()) == 0) return false;
            std::strncpy(dst, w.label.c_str(), 0x17);
            return true;
        };
        const bool new_language = take(o.text[0], a[ADV_LANGUAGE]);
        const bool new_sounds = take(o.text[1], a[ADV_SOUNDSET]);
        const bool new_palette = take(o.text[2], a[ADV_PALETTE]);
        guest::Local<char[12]> mode;
        guest::call_regs(0x472560, 0, 0, guest::addr(mode.get()), 0x49d7d0u /* "%dx%d" */, o.i10, o.i14);
        if (ms_stricmp(*mode, a[ADV_RESOLUTION].label.c_str()) == 0 && o.b1c == old_depth) {
            if (new_language) {
                autosaved = g_saved_gui != nullptr;
                if (autosaved) autosave_race();
                game_free(false);
                game_init();
                reset_player_drivers();
                gui_redraw_all();
            } else if (new_sounds) {
                guest::Local<MsString> set;
                set->construct(guest::addr(o.text[1]));
                sound_effects_load(set.get());
                set->destroy();
                set->construct(guest::addr(o.text[1]));
                engine_sounds_load(set.get());
                set->destroy();
            } else if (new_palette) {
                palette_reset();
            }
        } else {
            if (g_saved_gui) {
                autosaved = true;
                autosave_race();
            }
            rescale_all(widgets_to_layout);
            guest::Local<uint32_t[2]> size;
            (*size)[0] = static_cast<uint32_t>(o.i10);
            (*size)[1] = static_cast<uint32_t>(o.i14);
            guest::call_cdecl(0x46f4e0, guest::addr(a[ADV_RESOLUTION].label.c_str()), 0x49d854u /* "%ux%u" */,
                              guest::addr(&(*size)[0]), guest::addr(&(*size)[1]));
            o.i10 = g_screen_w = static_cast<int32_t>((*size)[0]);
            o.i14 = g_screen_h = static_cast<int32_t>((*size)[1]);
            game_free(false);
            game_init();
            rescale_all(widgets_to_screen);
            reset_player_drivers();
            gui_redraw_all();
        }
    }

    if (g_drivers && !autosaved) {
        // the race's drivers: the saved ones (from a loaded game) or the players'
        const bool saved = g_saved_driver_count >= 0;
        ptr32<Driver> *slot = saved ? g_saved_drivers : g_player_driver;
        char (*names)[kNameLen] = saved ? g_saved_driver_names : g_player_name;
        int32_t &count = saved ? static_cast<int32_t &>(g_saved_driver_count) : static_cast<int32_t &>(g_player_count);
        Widget &menu_list = g_menu_widgets.get()[W_DRIVER_LIST];
        list_clear(&menu_list);
        for (int k = 0; k < kMaxPlayers; k++) slot[k] = nullptr;
        ListData *items = d[DOPT_DRIVERS].list.get();
        for (uint32_t j = 0; j < static_cast<uint32_t>(items->items.size()); j++) {
            ListItem *item = items->items[static_cast<int>(j)].get();
            Driver *drv = &guest::at<Driver>(item->data);
            if (button->variant == 1) std::memcpy(drv, &g_drivers.get()[j], sizeof(Driver));
            guest::Local<MsString> name;
            name->init();
            name->assign(item->text.c_str(), static_cast<uint32_t>(std::strlen(item->text.c_str())));
            driver_save(drv, name.get());
            name->destroy();
            int k = 0;
            while (k < count && ms_stricmp(item->text.c_str(), names[k]) != 0) k++;
            if (k < count) {
                slot[k] = drv;
            } else {
                list_add(&menu_list, &item->text);
                ListData *menu = g_driver_list.get();
                menu->items[menu->items.size() - 1]->data = guest::addr(drv);
            }
        }
        // close the gaps left by race drivers that no longer exist
        if (count > 0) {
            int gaps = slot[0] ? 0 : 1;
            for (int i = 1; i < count; i++) {
                if (!slot[i]) {
                    gaps++;
                } else if (gaps > 0) {
                    slot[i - gaps] = slot[i];
                    std::strncpy(names[i - gaps], names[i], kNameLen);
                }
            }
            count -= gaps;
            for (int i = count; i < kMaxPlayers; i++) {
                slot[i] = nullptr;
                names[i][0] = 0;
            }
        }
        for (int i = 0; i < count; i++) set_player_driver(i, nullptr);
        guest::call_cdecl(0x496bd7, g_drivers);  // free
        g_drivers = nullptr;
        list_clear(&d[DOPT_DRIVERS]);
        d[DOPT_DRIVERS].pos = 0;
    }

    gui_close();
    if (g_saved_gui) {
        race_controls_init();
        if (g_game_state == 0) {
            race_info_box();  // the info box before the start
        } else {
            race_render();
            if (g_game_state >= 5) {
                if (g_game_state == 5) guest::call_regs(0x446010, 0, 0);  // race results
                else season_standings();
            }
        }
        pause_menu_open();
    } else if (autosaved) {
        guest::Local<MsString> name;
        name->init();
        name->assign(&guest::at<char>(kAutosave), 8);
        game_load(name.get());
        name->destroy();
    } else {
        main_menu_show();
    }
}
