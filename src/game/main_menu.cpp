// Main menu page and its track folder dropdowns.
#include "game/driver_options.h"
#include "game/lang.h"
#include "game/track_info.h"
#include "game/name_entry.h"
#include "game/menu.h"
#include "game/menu_page.h"
#include "game/race_state.h"
#include "game/selected_tracks.h"
#include "game/text.h"

#include <cstring>
#include "orig/menu.h"

static Widget &widget(int i) { return g_menu_widgets.get()[i]; }

static void show_all(bool on) {
    for (int i = 0; i < W_MENU_COUNT; i++) {
        if (on) widget(i).flags &= ~WF_HIDDEN;
        else widget(i).flags |= WF_HIDDEN;
    }
}

void main_menu_draw_page() {
    guest::Local<PageLabel> label;
    label->x = 14;
    label->y = 7;
    label->color = 0xf0;
    label->text.init();
    label->text.assign(g_lang[L_RACE_LENGTH]);
    draw_page(&g_lang[L_MAIN_TITLE], label.get(), 1);
    guest::call_stdcall(0x4970c6, label.get(), static_cast<uint32_t>(sizeof(PageLabel)), 1u, 0x47db40u);
}

// Back to the normal page with both dropdowns closed.
static void close_dropdowns() {
    show_all(true);
    widget(W_FOLDER_LIST).flags |= WF_HIDDEN;
    widget(W_MOVE_TO_LIST).flags |= WF_HIDDEN;
    main_menu_draw_page();
}

// The folder button: opens the folder list (alone with its button), or closes it.
void main_menu_folder_clicked(Widget *) {
    if (!(widget(W_FOLDER_LIST).flags & WF_HIDDEN)) {
        close_dropdowns();
    } else {
        show_all(false);
        widget(W_FOLDER_LIST).flags &= ~WF_HIDDEN;
        widget(W_FOLDER).flags &= ~WF_HIDDEN;
        gui_set_focus(W_FOLDER_LIST);
    }
    gui_redraw_all();
}

// "Move to": opens the list of target folders (needs selected tracks), or closes it.
void main_menu_move_to_clicked(Widget *) {
    if (!(widget(W_MOVE_TO_LIST).flags & WF_HIDDEN)) {
        close_dropdowns();
    } else {
        if (g_selected_tracks.size() < 1) {
            message_box(0x47da90 /* main_menu_draw_page */, &g_lang[L_MOVE_TRACKS], &g_lang[L_SELECT_TRACK_FIRST], 1);
            return;
        }
        show_all(false);
        widget(W_MOVE_TO_LIST).flags &= ~WF_HIDDEN;
        widget(W_MOVE_TO).flags &= ~WF_HIDDEN;
        gui_set_focus(W_MOVE_TO_LIST);
    }
    gui_redraw_all();
}

// Options button: options_open, dopt_open (both screens are prepared); then the Create
// driver dialog state of the Driver options widgets is reset (dopt_create_driver_clicked). Each gets the
// button.
void main_menu_options_clicked(Widget *w) {
    uint32_t b = guest::addr(w);
    options_open();
    dopt_open();
    dopt_create_driver_clicked(w);
}

// The car button of player slot p (widget id 20 + p): opens the car dropdown below the button.
void main_menu_car_clicked(Widget *button) {
    int p = button->id - 20;
    if (p >= g_player_count || p < 0) return;
    g_menu_gui = gui_push();
    Widget *w = g_car_dropdown_widgets.get();
    w[0].x = static_cast<float>(g_font_size) + button->x;
    w[0].y = button->h + button->y + static_cast<float>(g_menu_border);
    w[0].id = button->id;
    w[1].x = button->x;
    w[1].y = button->y;
    w[1].id = button->id;
    gui_open(2, w, 2, guest::at<float>(0x49dc7c) /* 1.0 */, 0, 0, 0x473ee0 /* list_draw */, 0);
    gui_set_focus(0);
}

// A car picked in the dropdown: the player's driver gets it and is saved under the player's name; back
// to the main menu.
void main_menu_car_picked(Widget *list) {
    if (list->list) {
        guest::Local<MsString> name;
        int p = list->id - 20;
        Driver *d = g_player_driver[p].get();
        d->car = list->list.get()->selected;
        name->init();
        name->assign(g_player_name[p], static_cast<uint32_t>(std::strlen(g_player_name[p])));
        driver_save(d, name.get());
        name->destroy();
    }
    gui_close();
    main_menu_show();
}

// Back to the main menu (after a race, the Options screens, Track info, ...): cursor on, the race length
// caption for the current unit (texts 1+2 laps, 3+4 km, 165+166 minutes, joined by a space), the page and
// track list redrawn; the menu GUI saved while racing comes back, else it is opened anew.
void main_menu_show() {
    guest::call_import(IMP_SDL_ShowCursor, 1u);
    static const int kUnitText[3][2] = {{1, 2}, {3, 4}, {165, 166}};
    int unit = g_length_unit == 1 ? 1 : g_length_unit == 2 ? 2 : 0;
    guest::Local<MsString> t1, caption;
    MsString::concat(t1.get(), &g_lang[kUnitText[unit][0]], 0x49d7f4u);  // + " "
    MsString::concat_move(caption.get(), t1.get(), &g_lang[kUnitText[unit][1]]);
    MsString &label = widget(W_LENGTH_UNIT).label;
    if (&label != caption.get()) {
        label.destroy();
        label.move_assign(caption.get());
    }
    caption->destroy();
    t1->destroy();
    main_menu_draw_page();
    track_list_refresh(nullptr);
    if (g_menu_gui) {
        gui_pop(&g_menu_gui);
        g_menu_gui = nullptr;
    } else {
        gui_open(1, g_menu_widgets.get(), 0x1e, guest::at<float>(0x49dc7c) /* 1.0 */, 0, 0x473b00, 0x473ee0, 0);
    }
}

static constexpr uint32_t STR_BACKSLASH = 0x49ccfc;  // "\\"

// Right mouse button released on the main menu (no race on, folder dropdowns closed): on the driver list
// or a player's name, that driver's stats (name_entry_open); on the track list, Track info for the track.
void main_menu_right_click(int px, int py) {
    if (g_menu_gui || !(widget(W_FOLDER_LIST).flags & WF_HIDDEN) || !(widget(W_MOVE_TO_LIST).flags & WF_HIDDEN)) return;
    float x = static_cast<float>(px), y = static_cast<float>(py);
    // the list cell under (dx, dy), or -1
    auto cell = [](const Widget &w, float dx, float dy) {
        const ListData *l = w.list.get();
        int col = guest::to_int(dx / l->column_w);
        int i = (guest::to_int(dy / l->row_h) + w.pos) * l->columns + col;
        return col < l->columns && i >= 0 && i < l->items.size() ? i : -1;
    };
    const Widget &drivers = widget(W_DRIVER_LIST);
    float dx = x - drivers.x, dy = y - drivers.y;
    if (!(dx < 0.0f) && !(drivers.w <= dx) && !(dy < 0.0f) && !(drivers.h <= dy)) {
        int i = cell(drivers, dx, dy);
        if (i >= 0) {
            ListItem *item = drivers.list->items[i].get();
            name_entry_open(ptr32<Driver>::from_addr(item->data).get(), &item->text);
        }
        return;
    }
    const Widget &names = widget(W_DRIVER_NAME + 1);  // the name column: six rows of its height
    dx = x - names.x;
    dy = y - names.y;
    if (0.0f <= dx && dx < names.w && 0.0f <= dy && dy < names.h * 6.0f) {
        int p = guest::to_int(dy / names.h);
        if (p < 0 || p >= g_player_count) return;
        guest::Local<MsString> name;
        name->construct(guest::addr(g_player_name[p]));
        name_entry_open(g_player_driver[p].get(), name.get());
        guest::call_thiscall(0x4027a0, name.get());  // ~string
    }
    const Widget &tracks = widget(W_TRACK_LIST);
    dx = x - tracks.x;
    dy = y - tracks.y;
    if (!(0.0f <= dx && dx < tracks.w && 0.0f <= dy && dy < tracks.h)) return;
    int i = cell(tracks, dx, dy);
    if (i < 0) return;
    guest::Local<MsString> path, t1, t2, t3;
    path->init();
    ListItem *item = tracks.list->items[i].get();
    if (item->folder < 2) {
        MsString::concat(t1.get(), STR_BACKSLASH, &item->text);
        guest::call_thiscall(0x404b80, path.get(), t1.get());  // path = move(t1)
        guest::call_thiscall(0x4027a0, t1.get());
    } else {
        MsString::concat(t1.get(), STR_BACKSLASH, &g_folder_list->items[item->folder]->text);
        MsString::concat_move(t2.get(), t1.get(), STR_BACKSLASH);
        MsString::concat_move(t3.get(), t2.get(), &widget(W_TRACK_LIST).list->items[i]->text);
        guest::call_thiscall(0x404b80, path.get(), t3.get());
        guest::call_thiscall(0x4027a0, t3.get());
        guest::call_thiscall(0x4027a0, t2.get());
        guest::call_thiscall(0x4027a0, t1.get());
    }
    track_info_open(path->c_str());
    guest::call_thiscall(0x4027a0, path.get());
}

// F1 in the menus: the about box (title: the version string at 0x49d85c, text at 0x49d888).
void key_about(int key) {
    if (key != 0x11a || g_menu_gui) return;  // SDLK_F1, and not while a race is on
    guest::Local<MsString> title, text;
    title->construct(0x49d85c);
    text->construct(0x49d888);
    message_box(0x47da90, title.get(), text.get(), 1);
    text->destroy();
    title->destroy();
}
