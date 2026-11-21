#include "bridge/guest.h"
#include "game/menu.h"
#include "game/advanced_options.h"
#include "game/menu_page.h"
#include "game/file_dialog.h"
#include "game/message_box.h"
#include "game/name_entry.h"
#include "game/track_info.h"
#include "game/text.h"
#include "game/lang.h"

using namespace abi;

REWRITE(476400, "menu_add_driver", 1, 4, RET_NONE) { menu_add_driver(argp<Widget>(c, 0)); }
REWRITE(4764f0, "menu_remove_driver", 1, 4, RET_NONE) { menu_remove_driver(argp<Widget>(c, 0)); }
REWRITE(4735a0, "widget_draw", 1, 4, RET_NONE) { widget_draw(argp<Widget>(c, 0)); }
REWRITE(484e50, "draw_text", 3, 0, RET_NONE) {
    draw_text(ecx<MsString>(c), edx<Box>(c), static_cast<uint8_t>(arg(c, 0)), arg(c, 2));
}
REWRITE(47da90, "main_menu_draw_page", 0, 0, RET_NONE) { main_menu_draw_page(); }
REWRITE(4760e0, "main_menu_folder_clicked", 1, 4, RET_NONE) { main_menu_folder_clicked(argp<Widget>(c, 0)); }
REWRITE(476280, "main_menu_move_to_clicked", 1, 4, RET_NONE) { main_menu_move_to_clicked(argp<Widget>(c, 0)); }
REWRITE(474810, "race_length_changed", 1, 4, RET_NONE) { race_length_changed(argp<Widget>(c, 0)); }
REWRITE(474830, "random_tracks_clicked", 1, 4, RET_NONE) { random_tracks_clicked(argp<Widget>(c, 0)); }
REWRITE(474850, "select_random_tracks", 0, 0, RET_NONE) { select_random_tracks(static_cast<int>(ECX)); }
REWRITE(474b30, "start_clicked", 1, 4, RET_NONE) { start_clicked(argp<Widget>(c, 0)); }
REWRITE(474bb0, "exit_clicked", 1, 4, RET_NONE) { exit_clicked(argp<Widget>(c, 0)); }
REWRITE(474bf0, "track_toggle", 0, 0, RET_NONE) { track_toggle(static_cast<int>(ECX), static_cast<int>(EDX)); }
REWRITE(474e10, "tracks_mark_selected", 0, 0, RET_NONE) { tracks_mark_selected(); }
REWRITE(482d40, "name_entry_open", 0, 0, RET_NONE) { name_entry_open(ecx<Driver>(c), edx<MsString>(c)); }
REWRITE(482db0, "name_entry_close", 1, 4, RET_NONE) { name_entry_close(); }
REWRITE(482fd0, "stats_reset_clicked", 1, 4, RET_NONE) { stats_reset_clicked(argp<Widget>(c, 0)); }
REWRITE(48d5d0, "dopt_rename_clicked", 1, 4, RET_NONE) { dopt_rename_clicked(argp<Widget>(c, 0)); }
REWRITE(487020, "set_player_driver", 0, 0, RET_NONE) { set_player_driver(static_cast<int>(ECX), edx<Driver>(c)); }
REWRITE(483790, "main_menu_right_click", 0, 0, RET_NONE) { main_menu_right_click(static_cast<int>(ECX), static_cast<int>(EDX)); }
REWRITE(47d6f0, "draw_dialog", 2, 0, RET_NONE) {
    draw_dialog(ecx<int32_t>(c), edx<MsString>(c), argp<PageLabel>(c, 0), argi(c, 1));
}
REWRITE(481650, "advanced_clicked", 1, 4, RET_NONE) { advanced_clicked(argp<Widget>(c, 0)); }
REWRITE(47db80, "options_draw_page", 0, 0, RET_NONE) { options_draw_page(); }
REWRITE(481260, "options_open", 1, 4, RET_NONE) { options_open(); }
REWRITE(480ff0, "main_menu_show", 0, 0, RET_NONE) { main_menu_show(); }
REWRITE(4766e0, "main_menu_options_clicked", 1, 4, RET_NONE) { main_menu_options_clicked(argp<Widget>(c, 0)); }
REWRITE(484640, "msgbox_button_clicked", 1, 4, RET_NONE) { msgbox_button_clicked(argp<Widget>(c, 0)); }
REWRITE(484c90, "inputbox_button_clicked", 1, 4, RET_NONE) { inputbox_button_clicked(argp<Widget>(c, 0)); }
REWRITE(48f450, "file_dialog_file_clicked", 1, 4, RET_NONE) { file_dialog_file_clicked(argp<Widget>(c, 0)); }
REWRITE(4832d0, "main_menu_car_clicked", 1, 4, RET_NONE) { main_menu_car_clicked(argp<Widget>(c, 0)); }
REWRITE(4833a0, "main_menu_car_picked", 1, 4, RET_NONE) { main_menu_car_picked(argp<Widget>(c, 0)); }
REWRITE(482ca0, "track_info_open", 0, 0, RET_NONE) { track_info_open(ecx<char>(c)); }
REWRITE(47f3a0, "track_info_draw", 0, 0, RET_NONE) { track_info_draw(); }
REWRITE(482e30, "track_info_reset_clicked", 1, 4, RET_NONE) { track_info_reset_clicked(argp<Widget>(c, 0)); }
REWRITE(483af0, "key_about", 0, 0, RET_NONE) { key_about(static_cast<int>(ECX)); }
REWRITE(484cf0, "draw_text_at", 3, 0, RET_NONE) {
    draw_text_at(ecx<MsString>(c), static_cast<int>(EDX), static_cast<int>(arg(c, 0)), static_cast<uint8_t>(arg(c, 1)),
                 static_cast<uint8_t>(arg(c, 2)));
}
REWRITE(48f290, "file_dialog_fill", 0, 0, RET_NONE) { file_dialog_fill(); }
REWRITE(482860, "file_dialog_open", 1, 4, RET_NONE) { file_dialog_open(argp<Widget>(c, 0)); }
REWRITE(47f220, "file_dialog_draw", 0, 0, RET_NONE) { file_dialog_draw(); }
REWRITE(48f4c0, "file_dialog_delete_clicked", 1, 4, RET_NONE) { file_dialog_delete_clicked(argp<Widget>(c, 0)); }
REWRITE(482940, "file_dialog_closed", 1, 4, RET_NONE) { file_dialog_closed(argp<Widget>(c, 0)); }
REWRITE(4831b0, "track_info_description_clicked", 1, 4, RET_NONE) { track_info_description_clicked(argp<Widget>(c, 0)); }
REWRITE(471400, "lang_load", 0, 0, RET_NONE) { lang_load(ecx<MsString>(c)); }
REWRITE(4716e0, "lang_find_entry", 2, 0, RET_AL) {
    ret_bool(c, lang_find_entry(ecx<char>(c), EDX, arg(c, 0), argp<MsString>(c, 1)));
}
REWRITE(4725a0, "driver_list_fill", 0, 0, RET_NONE) { driver_list_fill(ecx<Widget>(c)); }
REWRITE(476960, "delete_folder_clicked", 1, 4, RET_NONE) { delete_folder_clicked(argp<Widget>(c, 0)); }
REWRITE(472e10, "folder_lists_fill", 0, 0, RET_NONE) { folder_lists_fill(ecx<Widget>(c), edx<Widget>(c)); }
REWRITE(472830, "track_list_fill", 6, 0, RET_NONE) {
    uint32_t args = ESP + 4;  // the two vectors by value
    track_list_fill(ecx<Widget>(c), (EDX & 0xff) != 0, &guest::at<MsVector<MsString>>(args),
                    &guest::at<MsVector<int32_t>>(args + 12));
}
REWRITE(474f40, "track_list_clicked", 1, 4, RET_NONE) { track_list_clicked(argp<Widget>(c, 0)); }
REWRITE(476710, "track_new_folder_clicked", 1, 4, RET_NONE) { track_new_folder_clicked(argp<Widget>(c, 0), ECX); }
REWRITE(4752f0, "track_list_refresh", 1, 4, RET_NONE) { track_list_refresh(argp<Widget>(c, 0)); }
REWRITE(476ba0, "delete_tracks_clicked", 1, 4, RET_NONE) { delete_tracks_clicked(argp<Widget>(c, 0)); }
REWRITE(475b00, "move_to_clicked", 1, 4, RET_NONE) { move_to_clicked(argp<Widget>(c, 0)); }
REWRITE(4846a0, "input_box", 1, 0, RET_EAX) { ret(c, input_box(edx<MsString>(c), argp<MsString>(c, 0))); }
REWRITE(47fc80, "driver_stats_draw", 0, 0, RET_NONE) { driver_stats_draw(); }
