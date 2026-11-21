// Menu screens: widgets (buttons, labels, list boxes) and the driver selection on the main menu.
#pragma once
#include "game/driver.h"
#include "game/gui.h"

// Main menu widget indices
enum {
    W_DRIVER_CAR = 3,     // 3 + player: car of each selected driver
    W_DRIVER_NAME = 9,    // 9 + player: name button of each selected driver (click removes the driver)
    W_DRIVER_LIST = 16,   // available drivers
    W_LENGTH_UNIT = 18,   // caption of the race length unit
    W_TRACK_LIST = 19,
    W_MOVE_TO = 20,       // "Move to": moves the selected tracks to another folder (list W_MOVE_TO_LIST)
    W_FOLDER = 24,        // the track folder shown (list W_FOLDER_LIST)
    W_FOLDER_LIST = 27,
    W_MOVE_TO_LIST = 28,
    W_RANDOM_COUNT = 23, // how many tracks "Select" picks
    W_MENU_COUNT = 29,    // the page's widgets; 29 is the "n selected" caption
    W_SELECTED_COUNT = 29,
};
constexpr int kMaxPlayers = 6;
constexpr int kNameLen = 13;

GLOBAL(int32_t, g_player_count, 0x4a4dc0);
GLOBAL(ptr32<Driver>[kMaxPlayers], g_player_driver, 0x4a4d04);
GLOBAL(char[kMaxPlayers][kNameLen], g_player_name, 0x4a4e08);
GLOBAL(ptr32<Widget>, g_menu_widgets, 0x4a4f0c);
GLOBAL(int32_t, g_saved_driver_count, 0x4a4048);           // >= 0: the drivers below replace the players'
GLOBAL(ptr32<Driver>[kMaxPlayers], g_saved_drivers, 0x4a4c30);
GLOBAL(char[kMaxPlayers][kNameLen], g_saved_driver_names, 0x4a5128);
GLOBAL(ptr32<ListData>, g_driver_list, 0x4a4f04);
GLOBAL(ptr32<ListData>, g_folder_list, 0x4a4f10);  // track folders: "[Default Tracks]", subfolders
GLOBAL(ptr32<ListData>, g_track_list, 0x4a4f14);   // tracks of the folder shown
GLOBAL(int16_t, g_race_length, 0x4a4d48);          // in the unit chosen by the length button
GLOBAL(int32_t, g_track_count, 0x4a4ec8);          // tracks of the season about to start
GLOBAL(ptr32<Widget>, g_car_dropdown_widgets, 0x4a5178);  // 0 the car list, 1 a copy of the car button
GLOBAL(ptr32<GuiState>, g_menu_gui, 0x4a4f18);     // the main menu, saved while racing

void widget_draw(Widget *w);
void main_menu_draw_page();
void main_menu_folder_clicked(Widget *w);
void race_length_changed(Widget *slider);
void random_tracks_clicked(Widget *w);
void select_random_tracks(int count);
void start_clicked(Widget *w);
void exit_clicked(Widget *w);
void track_toggle(int index, int mode);
void tracks_mark_selected();
void main_menu_move_to_clicked(Widget *w);
void main_menu_options_clicked(Widget *w);
void main_menu_show();
void options_open();
void options_draw_page();
void advanced_clicked(Widget *button);
void main_menu_right_click(int x, int y);
void set_player_driver(int player, Driver *d);
void main_menu_car_clicked(Widget *button);
void main_menu_car_picked(Widget *list);
void key_about(int key);

// option buttons cycling through states (src/game/option_buttons.cpp)
void on_off_clicked(Widget *w);
void length_unit_clicked(Widget *w);
void start_order_clicked(Widget *w);
void driver_type_clicked(Widget *w);
void menu_add_driver(Widget *list);
void car_shades(uint32_t *pal, uint32_t color);
void menu_remove_driver(Widget *button);
void delete_folder_clicked(Widget *);
void folder_lists_fill(Widget *folders, Widget *move_to);
void track_list_refresh(Widget *sender);
void track_new_folder_clicked(Widget *button, uint32_t ecx);
void delete_tracks_clicked(Widget *button);
void move_to_clicked(Widget *button);
void track_list_clicked(Widget *list);
void track_list_fill(Widget *list, bool with_default, MsVector<MsString> *names, MsVector<int32_t> *indices);
void driver_list_fill(Widget *list);
