#include "game/file_dialog.h"

#include <cstring>

#include "game/advanced_options.h"
#include "game/file.h"
#include "game/gui.h"
#include "game/lang.h"
#include "game/menu.h"
#include "game/message_box.h"
#include "game/race_state.h"
#include "game/replay.h"
#include "orig/menu.h"

// A file picked in the list becomes the name in the text field (its length set).
void file_dialog_file_clicked(Widget *) {
    Widget *w = g_file_dialog_widgets.get();
    ListData *list = w[FD_FILES].list.get();
    int sel = list->selected;
    if (sel < 0 || sel >= list->items.size()) return;
    MsString *name = &list->items[sel].get()->text;
    if (&w[FD_NAME].label != name) w[FD_NAME].label.assign(*name);
    w = g_file_dialog_widgets.get();
    w[FD_NAME].pos = static_cast<int32_t>(w[FD_FILES].list.get()->items[sel].get()->text.size);
    text_field_draw(&g_file_dialog_widgets.get()[FD_NAME]);
}

// The saved replays (saves/*.<ext 0x49db60>, sorted) as the file list; the name field emptied.
void file_dialog_fill() {
    Widget *w = g_file_dialog_widgets.get();
    list_clear(&w[FD_FILES]);
    enter_game_dir();
    guest::Local<MsVector<MsString>> names;
    guest::Local<MsString> ext, dir;
    names->first = nullptr;
    names->last = nullptr;
    names->end = nullptr;
    ext->init();
    ext->assign(&guest::at<char>(0x49db60), 3);
    dir->init();
    dir->assign(&guest::at<char>(0x49cd08), 5);  // "saves"
    list_directory(dir.get(), ext.get(), names.get(), 1, 0x20);
    dir->destroy();
    ext->destroy();
    MsString *first = names->first.get();
    for (int n = names->size(), i = 0; n != 0; n--, i++) list_insert_sorted(&w[FD_FILES], &first[i]);
    w[FD_FILES].id = 2;
    Widget *now = g_file_dialog_widgets.get();
    now[FD_NAME].label.clear();
    g_file_dialog_widgets.get()[FD_NAME].pos = 0;
    if (names->first) {
        guest::call_regs(0x4050e0, names->first.a, names->last.a, 0u, 0u);  // destroy the strings
        guest::call_stdcall(0x404c00, names->first, (names->end.a - names->first.a) / static_cast<uint32_t>(sizeof(MsString)));
    }
}

// Load (button id 2, from the main menu) or Save (id 3, from the pause menu): titles the dialog, saves the
// screen below and opens the dialog with the file list (load) or the name field (save) focused.
void file_dialog_open(Widget *button) {
    int focus = FD_NAME;
    Widget *w = g_file_dialog_widgets.get();
    if (button->id == 2) {
        w[FD_TITLE].id = 2;
        if (&g_file_dialog_widgets.get()[FD_TITLE].label != &g_lang[L_LOAD_TITLE])
            g_file_dialog_widgets.get()[FD_TITLE].label.assign(g_lang[L_LOAD_TITLE]);
        g_menu_gui = gui_push();
        focus = FD_FILES;
    } else if (button->id == 3) {
        w[FD_TITLE].id = 3;
        if (&g_file_dialog_widgets.get()[FD_TITLE].label != &g_lang[L_SAVE_TITLE])
            g_file_dialog_widgets.get()[FD_TITLE].label.assign(g_lang[L_SAVE_TITLE]);
        g_saved_gui = gui_push();
    }
    file_dialog_fill();
    file_dialog_draw();
    gui_open(7, g_file_dialog_widgets.get(), 5, 1.0f, 0x473930, 0, 0x473ee0, 0);
    gui_set_focus(focus);
}

// The dialog's box, its title ("<Load|Save> <game>") and the captions of the list and the name field.
void file_dialog_draw() {
    guest::Local<int32_t[4]> box;
    static const int32_t kBox[4] = {10, 10, 70, 50};
    for (int i = 0; i < 4; i++) (*box)[i] = kBox[i];
    guest::Local<PageLabel[2]> labels;
    static const struct { int y, text; } rows[] = {{0x29, L_FD_FILENAME}, {0x11, L_FD_FILES}};
    for (int i = 0; i < 2; i++) {
        PageLabel &l = (*labels)[i];
        l.x = 0xf;
        l.y = rows[i].y;
        l.color = 0xf0;
        l.text.init();
        l.text.assign(g_lang[rows[i].text]);
    }
    guest::Local<MsString> tmp, title;
    MsString::concat(tmp.get(), &g_file_dialog_widgets.get()[FD_TITLE].label, 0x49cc4cu);  // + " "
    MsString::concat_move(title.get(), tmp.get(), &g_lang[L_GAME]);
    tmp->destroy();
    draw_dialog(*box, title.get(), *labels, 2);
    title->destroy();
    guest::call_stdcall(0x4970c6, labels.get(), static_cast<uint32_t>(sizeof(PageLabel)), 2u, 0x47db40u);
}

// Redraws the paused race below a closed dialog (the same screen the pause menu was opened over).
static void redraw_paused_race() {
    if (g_game_state == 0) {
        race_info_box();
    } else {
        race_render();
        if (g_game_state > 4) {
            if (g_game_state == 5) guest::call_regs(0x446010, 0, 0);
            else guest::call_regs(0x4473e0, 0, 0);
        }
    }
}

// "<prefix><name><suffix>" into *joined, and a fresh string built from its C string into *out (as the
// original does); the caller frees out, then joined.
static void quote_name(MsString *out, MsString *joined, int prefix, const MsString *name, int suffix) {
    guest::Local<MsString> copy, t1;
    copy->init();
    copy->assign(*name);
    MsString::concat(t1.get(), &g_lang[prefix], copy.get());
    MsString::concat_move(joined, t1.get(), &g_lang[suffix]);
    t1->destroy();
    copy->destroy();
    out->construct(guest::addr(joined->c_str()));
}

// OK / Cancel (button id 0) of the replay dialog. From the main menu it loads the named replay (lang 98,
// 99 around the name, title 100, if it is not in the list; lang 101 if loading fails); from the pause menu
// it saves the replay range, asking first (lang 95, 96, title 97) when the file exists, then returns to
// the pause menu with the range marks cleared.
void file_dialog_closed(Widget *button) {
    Widget *w = g_file_dialog_widgets.get();
    MsString *name = &w[4].label;
    bool listed = guest::addr(w) != static_cast<uint32_t>(-0xfc) && w[3].type == 4 &&
                  button->id != 0 && list_find(w[3].list.get(), name) >= 0;
    if (!g_saved_gui) {
        if (button->id == 0) {
            gui_close();
            main_menu_show();
            return;
        }
        if (listed) {
            gui_close();
            main_menu_show();
            for (int i = 0; i < 6; i++) menu_remove_driver(&g_menu_widgets.get()[10]);
            g_menu_gui = gui_push();
            if (game_load(name) != 0) {
                main_menu_show();
                message_box(0x47da90, &g_lang[100], &g_lang[101], 1);
            }
            return;
        }
        guest::Local<MsString> text, joined;
        quote_name(text.get(), joined.get(), 98, name, 99);
        message_box(0x47f220, &g_lang[100], text.get(), 1);
        text->destroy();
        joined->destroy();
        return;
    }
    if (button->id == 0) {
        gui_close();
        redraw_paused_race();
        pause_menu_open();
        g_replay_mark_out = -1.0f;
        g_replay_mark_in = -1.0f;
        return;
    }
    if (listed) {
        guest::Local<MsString> text, joined;
        quote_name(text.get(), joined.get(), 95, name, 96);
        int yes = message_box(0x47f220, &g_lang[97], text.get(), 2);
        text->destroy();
        joined->destroy();
        if (yes == 0) return;
    }
    gui_close();
    redraw_paused_race();
    pause_menu_open();
    game_save(name);
    g_replay_mark_out = -1.0f;
    g_replay_mark_in = -1.0f;
}

// "Delete" in the replay dialog: after a Yes to text 102 + " '" + name + "'?" (title 103), removes the
// name from the list (if listed) and deletes saves\<name>.gam; the list's new selection (if any) becomes
// the name in the text field.
void file_dialog_delete_clicked(Widget *) {
    Widget *w = g_file_dialog_widgets.get();
    if (w[3].list->items.size() == 0) return;
    guest::Local<MsString> quote, part1, part2, question;
    quote->init();
    quote->assign(&guest::at<char>(0x49d804), 2);  // " '"
    MsString::concat(part1.get(), &g_lang[L_REALLY_DELETE], quote.get());
    MsString::concat_move(part2.get(), part1.get(), &w[4].label);
    MsString::concat_move(question.get(), part2.get(), 0x49d800u);  // + "'?"
    part2->destroy();
    part1->destroy();
    quote->destroy();
    guest::Local<MsString> text;
    text->init();
    text->assign(question->c_str(), static_cast<uint32_t>(std::strlen(question->c_str())));
    int yes = message_box(0x47f220, &g_lang[103], text.get(), 2);
    text->destroy();
    if (yes) {
        Widget *list = &w[3];
        if (guest::addr(list) != 0 && list->type == 4) {
            int i = list_find(list->list.get(), &w[4].label);
            if (i >= 0) list_remove(list, i);
        }
        guest::Local<MsString> dir, t1, t2, path;
        dir->construct(0x49cd08u);  // "saves"
        MsString::concat_move(t1.get(), dir.get(), 0x49ccfcu);  // + "\\"
        MsString::concat_move(t2.get(), t1.get(), &g_file_dialog_widgets.get()[4].label);
        MsString::concat_move(path.get(), t2.get(), 0x49cd10u);  // + ".gam"
        t2->destroy();
        t1->destroy();
        dir->destroy();
        guest::call_cdecl(guest::at<uint32_t>(IMP__unlink), guest::addr(path->c_str()));
        ListData *d = w[3].list.get();
        int sel = d->selected;
        if (sel < 0 || sel >= d->items.size()) {
            d->selected = -1;
        } else {
            Widget *field = &g_file_dialog_widgets.get()[4];
            MsString *src = &d->items[sel]->text;
            if (&field->label != src) field->label.assign(*src);
            field->pos = static_cast<int32_t>(w[3].list->items[sel]->text.size);
            text_field_draw(field);
        }
        list_draw(list);
        path->destroy();
    }
    question->destroy();
}
