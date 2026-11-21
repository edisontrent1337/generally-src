// Main menu: selecting tracks for the season, Start and Exit.
#include <cstring>

#include "game/gui.h"
#include "game/file.h"
#include "game/input.h"
#include "game/lang.h"
#include "game/menu.h"
#include "game/message_box.h"
#include "game/selected_tracks.h"
#include "orig/menu.h"
#include "orig/sdl.h"

static constexpr uint32_t STR_BACKSLASH = 0x49ccfc;  // "\\"
static constexpr uint32_t STR_SPACE = 0x49cc4c;      // " "
static constexpr uint32_t STR_TRK = 0x49d7cc;        // "trk"
static constexpr uint32_t STR_TRACKS = 0x49ccf4;     // "tracks"

static Widget &widget(int i) { return g_menu_widgets.get()[i]; }
static ListItem &track_item(int i) { return *g_track_list->items[i]; }
static int track_items() { return g_track_list->items.size(); }

void race_length_changed(Widget *slider) { g_race_length = static_cast<int16_t>(slider->pos); }

// Adds (mode 2), removes (mode 0) or adds unless already selected (other modes) track item `index`.
// Selected tracks are named by their path below tracks/: "\name" or "\folder\name".
void track_toggle(int index, int mode) {
    guest::Local<MsString> path, t1, t2, t3;
    path->init();
    ListItem &item = track_item(index);
    if (static_cast<uint32_t>(item.folder) > 1) {
        MsString::concat(t1.get(), STR_BACKSLASH, &g_folder_list->items[item.folder]->text);
        MsString::concat_move(t2.get(), t1.get(), STR_BACKSLASH);
        MsString::concat_move(t3.get(), t2.get(), &item.text);
        path->destroy();
        path->move_assign(t3.get());
        t3->destroy();
        t2->destroy();
    } else {
        MsString::concat(t1.get(), STR_BACKSLASH, &item.text);
        path->destroy();
        path->move_assign(t1.get());
    }
    if (t1->cap >= 16) guest::call_stdcall(0x403830, t1->ptr, t1->cap + 1);
    if (mode == 0) {
        int i = static_cast<int>(selected_track_find(path->c_str()));
        if (i >= 0) selected_track_remove(i);
    } else if (mode == 2 || static_cast<int>(selected_track_find_nocase(path->c_str())) < 0) {
        selected_track_add(path->c_str());
    }
    if (path->cap >= 16) guest::call_stdcall(0x403830, path->ptr, path->cap + 1);
}

// Marks the items of the shown folder that are selected.
void tracks_mark_selected() {
    guest::Local<MsString> path;
    path->init();
    int n = track_items();
    if (n <= 0) return;
    for (int i = 0; i < n; i++) {
        path->assign(&guest::at<char>(STR_BACKSLASH), 1);
        uint32_t folder = static_cast<uint32_t>(track_item(i).folder);
        if (folder > 1) {
            path->append(g_folder_list->items[folder]->text);
            path->append(STR_BACKSLASH, 1);
        }
        path->append(track_item(i).text);
        if (static_cast<int>(selected_track_find(path->c_str())) >= 0) track_item(i).marked = 1;
    }
    if (path->cap >= 16) guest::call_stdcall(0x403830, path->ptr, path->cap + 1);
}

// Replaces the selection by `count` random tracks of the shown folder.
void select_random_tracks(int count) {
    int n = track_items();
    if (count < 0) count = 0;
    else if (count > n) count = n;
    for (int i = 0; i < n; i++) {
        if (track_item(i).marked) {
            track_item(i).marked = 0;
            track_toggle(i, 0);
        }
    }
    for (int left = n; count > 0; count--, left--) {
        int r = static_cast<int>(guest::call_import(IMP_rand));
        for (int i = 0, k = 0; i < n; i++) {
            if (track_item(i).marked) continue;
            if (k == r % left) {
                track_toggle(i, 2);
                track_item(i).marked = 1;
                break;
            }
            k++;
        }
    }
    // caption "<n> selected"
    guest::Local<MsStringStream> ss;
    guest::Local<MsString> text;
    ss->construct();
    *ss << g_selected_tracks.size();
    ss->put(STR_SPACE);
    *ss << &g_lang[L_SELECTED];
    ss->str(text.get());
    MsString &caption = widget(W_SELECTED_COUNT).label;
    caption.destroy();
    caption.move_assign(text.get());
    if (text->cap >= 16) guest::call_stdcall(0x403830, text->ptr, text->cap + 1);
    widget_draw(&widget(W_SELECTED_COUNT));
    list_draw(&widget(W_TRACK_LIST));
    ss->destruct();
}

void random_tracks_clicked(Widget *) { select_random_tracks(widget(W_RANDOM_COUNT).pos); }

void start_clicked(Widget *) {
    g_track_count = g_selected_tracks.size();
    if (g_track_count < 1 || g_player_count < 1) {
        message_box(0x47da90 /* main_menu_draw_page */, &g_lang[L_OOPS], &g_lang[L_SELECT_FIRST], 1);
        return;
    }
    g_menu_gui = gui_push();
    race_season_start();
    guest::call_import(IMP_SDL_ShowCursor, 0u);
}

// Exit (after "Are you sure?"): closes the menu and posts SDL_QUIT. The event is allocated with operator new
// and never freed.
void exit_clicked(Widget *) {
    if (!message_box(0x47da90, &g_lang[L_EXIT], &g_lang[L_ARE_YOU_SURE], 2)) return;
    gui_close();
    uint32_t ev = guest::call_cdecl(0x496bdc, 0x14u);
    guest::at<uint8_t>(ev) = 12;  // SDL_QUIT
    guest::call_import(IMP_SDL_PushEvent, ev);
}

// "Delete tracks" (folder): exactly one track folder (not the two default entries) must be marked in the
// folder list. After a Yes, the selection is forgotten and <game>\tracks\<folder> is deleted with its
// contents; then the folder list is rebuilt, the default folder marked and the track list refilled.
void delete_folder_clicked(Widget *) {
    int marked = 0;
    uint32_t folder = 0;
    for (uint32_t i = 2; i < static_cast<uint32_t>(g_folder_list->items.size()); i++) {
        if (g_folder_list->items[static_cast<int>(i)]->marked) {
            marked++;
            folder = i;
        }
    }
    if (marked != 1) {
        message_box(0x47da90, &g_lang[L_DELETE_FOLDER_TITLE], &g_lang[L_DELETE_FOLDER_SELECT_ONE], 1);
        return;
    }
    if (!message_box(0x47da90, &g_lang[L_DELETE_FOLDER_TITLE], &g_lang[L_DELETE_FOLDER_CONFIRM], 2)) return;
    ListItem *item = g_folder_list->items[static_cast<int>(folder)].get();
    guest::Local<MsString> built, path;
    built->init();
    const char *dir = g_game_dir.get();
    built->assign(dir, static_cast<uint32_t>(std::strlen(dir)));
    built->append(STR_BACKSLASH, 1);
    built->append(0x49ccf4u, 6);  // "tracks"
    built->append(STR_BACKSLASH, 1);
    built->append(item->text);
    path->init();
    path->assign(*built);
    built->destroy();
    guest::call_regs(0x4050e0, g_selected_tracks.first.a, g_selected_tracks.last.a, 0u, 0u);  // destroy the strings
    g_selected_tracks.last = g_selected_tracks.first;
    if (delete_folder(path.get())) {
        folder_lists_fill(&widget(W_FOLDER_LIST), &widget(W_MOVE_TO_LIST));
        widget(W_FOLDER_LIST).list->items[0]->marked = 1;
        track_list_refresh(nullptr);
        gui_redraw_all();
    } else {
        message_box(0x47da90, &g_lang[L_DELETE_FOLDER_TITLE], &g_lang[L_DELETE_FOLDER_FAILED], 1);
    }
    path->destroy();
}

// Fills the folder list with the subfolders of tracks (in listing order; list_insert_sorted appends) and
// the two fixed entries on top ("all tracks", "default tracks"); if given, move_to (the "Move to" list)
// gets the subfolders too. All folder marks are cleared. The list becomes g_folder_list.
void folder_lists_fill(Widget *folders, Widget *move_to) {
    enter_game_dir();
    guest::Local<MsVector<MsString>> names;
    guest::Local<MsString> ext, dir;
    names->first = nullptr;
    names->last = nullptr;
    names->end = nullptr;
    ext->init();
    ext->assign(&guest::at<char>(0x49b574), 0);  // no extension
    dir->init();
    dir->assign(&guest::at<char>(0x49ccf4), 6);  // "tracks"
    list_directory(dir.get(), ext.get(), names.get(), 2, 0x14);
    dir->destroy();
    ext->destroy();
    list_clear(folders);
    MsString *name = names->first.get();
    for (int n = names->size(); n != 0; n--, name++) list_insert_sorted(folders, name);
    if (move_to) {
        list_clear(move_to);
        int count = folders->list->items.size();
        for (int i = 0; i < count; i++) {
            folders->list->items[i]->marked = 0;
            list_add(move_to, &folders->list->items[i]->text);
        }
    }
    list_insert(folders, &g_lang[L_ALL_TRACKS], 0);
    list_insert(folders, &g_lang[L_DEFAULT_TRACKS], 1);
    g_folder_list = folders->list;
    folders->id = 0;
    if (names->first) {
        guest::call_regs(0x4050e0, names->first.a, names->last.a, 0u, 0u);  // destroy the strings
        guest::call_stdcall(0x404c00, names->first, (names->end.a - names->first.a) / static_cast<uint32_t>(sizeof(MsString)));
    }
}

// A track list entry while the list is built: the track's name and the folder-list index of its folder
// (1: the default folder, tracks/ itself). vector<TrackEntry> is handled by the original's own code.
struct TrackEntry {
    MsString name;
    int32_t folder;
};
static_assert(sizeof(TrackEntry) == 0x1c);

// Destroys and frees a vector<string> (the inlined destructor).
static void free_strings(MsVector<MsString> *v) {
    if (!v->first) return;
    guest::call_regs(0x4050e0, v->first.a, v->last.a, 0u, 0u);  // destroy the strings
    guest::call_stdcall(0x404c00, v->first, (v->end.a - v->first.a) / static_cast<uint32_t>(sizeof(MsString)));
}

// Fills the track list widget `list` with the tracks of the default folder (if with_default) and of the
// given folders (names below tracks/, with their folder-list indices), sorted by name (case-insensitive).
// Each item remembers its folder index. names and indices are passed by value: freed here.
void track_list_fill(Widget *list, bool with_default, MsVector<MsString> *names, MsVector<int32_t> *indices) {
    enter_game_dir();
    guest::Local<MsVector<TrackEntry>> entries;
    entries->first = entries->last = entries->end = nullptr;
    guest::Local<MsVector<MsString>> found;
    guest::Local<MsString> ext, dir, with_sep, path;
    guest::Local<TrackEntry> entry;
    if (with_default) {
        found->first = found->last = found->end = nullptr;
        ext->init();
        ext->assign(&guest::at<char>(STR_TRK), 3);
        dir->init();
        dir->assign(&guest::at<char>(STR_TRACKS), 6);
        list_directory(dir.get(), ext.get(), found.get(), 1, 0xc);
        dir->destroy();
        ext->destroy();
        for (uint32_t i = 0; i < static_cast<uint32_t>(found->size()); i++) {
            entry->name.init();
            entry->folder = 0;
            entry->name.assign((*found)[static_cast<int>(i)]);
            entry->folder = 1;
            guest::call_thiscall(0x476f30, entries.get(), entry.get());  // push_back(entry)
            entry->name.destroy();
        }
        free_strings(found.get());
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(names->size()); i++) {
        found->first = found->last = found->end = nullptr;
        ext->init();
        ext->assign(&guest::at<char>(STR_TRK), 3);
        dir->init();
        dir->assign(&guest::at<char>(STR_TRACKS), 6);
        MsString::concat_move(with_sep.get(), dir.get(), STR_BACKSLASH);         // "tracks\"
        MsString::concat_move(path.get(), with_sep.get(), &(*names)[static_cast<int>(i)]);  // + folder
        list_directory(path.get(), ext.get(), found.get(), 1, 0xc);
        path->destroy();
        with_sep->destroy();
        dir->destroy();
        ext->destroy();
        for (uint32_t k = 0; k < static_cast<uint32_t>(found->size()); k++) {
            entry->name.init();
            entry->folder = 0;
            entry->name.assign((*found)[static_cast<int>(k)]);
            entry->folder = (*indices)[static_cast<int>(i)];
            guest::call_thiscall(0x476f30, entries.get(), entry.get());  // push_back(entry)
            entry->name.destroy();
        }
        free_strings(found.get());
    }
    int count = entries->size();
    guest::call_regs(0x477c50, entries->first.a, entries->last.a, static_cast<uint32_t>(count), 0x403eb0u);  // sort
    TrackEntry *e = entries->first.get();
    for (; count != 0; count--, e++) {
        int at = list_insert_sorted(list, &e->name);
        list->list->items[at]->folder = e->folder;
    }
    list->id = 0;
    g_track_list = list->list;
    if (entries->first) {
        guest::call_regs(0x477da0, entries->first.a, entries->last.a);  // destroy the entries
        guest::call_stdcall(0x4776c0, entries->first, (entries->end.a - entries->first.a) / static_cast<uint32_t>(sizeof(TrackEntry)));
    }
    free_strings(names);
    if (indices->first) guest::call_stdcall(0x4775b0, indices->first, (indices->end.a - indices->first.a) / 4);
}

GLOBAL(int32_t, g_folder_anchor, 0x4a4068);  // folder list: start of a Shift range selection, -1 none

// SDL 1.2 key numbers of the modifier keys (indices into g_keys)
enum { KEY_RSHIFT = 303, KEY_LSHIFT = 304, KEY_RCTRL = 305, KEY_LCTRL = 306 };

// vector<int>::push_back, inlined in the original: grow (0x477340) when full
static void push_index(MsVector<int32_t> *v, int32_t i) {
    if (v->last.a == v->end.a) guest::call_thiscall(0x477340, v, 0u);
    if (v->last) *v->last = i;
    v->last.a += 4;
}

// Shows the tracks of the marked folders. sender: the widget that triggered it, or none.
// - The folder button (no WF_LIST_CLICK in redraw bit 8, i.e. not a list click) only opens the folder
//   dropdown: every main-menu widget becomes visible except the two folder lists. (Kept as in the
//   original: the flag tested is bit 3 of the sender's redraw field.)
// - A click in the folder list marks folders: plain click → only the clicked one, Shift → the range from
//   the anchor (the previous selection) to it, Ctrl → the list's own toggling stays.
// Then the track list is refilled from the marked folders (or all folders when "all tracks" is marked;
// the default folder is included when its entry is marked, always for "all tracks"), the folder button
// shows the single marked folder's name (else text 130), the track slider and scroll range are adjusted
// and the "n selected" caption is rebuilt.
void track_list_refresh(Widget *sender) {
    ListData *folders = g_folder_list.get();
    Widget *w = g_menu_widgets.get();
    int saved_scroll = w[W_TRACK_LIST].pos;
    if (sender && !(sender->redraw & 8)) {
        for (int i = 0; i < W_MENU_COUNT; i++) w[i].flags &= ~WF_HIDDEN;
        g_menu_widgets.get()[W_FOLDER_LIST].flags |= WF_HIDDEN;
        g_menu_widgets.get()[W_MOVE_TO_LIST].flags |= WF_HIDDEN;
        main_menu_draw_page();
        gui_redraw_all();
        return;
    }
    if (sender) {
        keyboard_snapshot();
        const uint8_t *keys = g_keys.get();
        int n = folders->items.size();
        if ((keys[KEY_RCTRL] & 1) || (keys[KEY_LCTRL] & 1)) {
            g_folder_anchor = -1;
        } else if (!(keys[KEY_RSHIFT] & 1) && !(keys[KEY_LSHIFT] & 1)) {
            g_folder_anchor = -1;
            for (uint32_t i = 0; i < static_cast<uint32_t>(folders->items.size()); i++)
                folders->items[static_cast<int>(i)]->marked = 0;
            folders->items[folders->selected]->marked = 1;
        } else {
            int sel = folders->selected;
            if (g_folder_anchor < 0 && (g_folder_anchor = folders->previous) < 0) g_folder_anchor = 0;
            int lo = sel < g_folder_anchor ? sel : g_folder_anchor;
            int hi = sel < g_folder_anchor ? g_folder_anchor : sel;
            for (int i = 0; i < n; i++) folders->items[i]->marked = i >= lo && i <= hi;
        }
    }
    list_clear(&g_menu_widgets.get()[W_TRACK_LIST]);

    // the marked folders: names and folder-list indices
    guest::Local<MsVector<MsString>> names;
    guest::Local<MsVector<int32_t>> indices;
    names->first = names->last = names->end = nullptr;
    indices->first = indices->last = indices->end = nullptr;
    bool all = folders->items[0]->marked != 0;
    for (uint32_t i = 2; i < static_cast<uint32_t>(folders->items.size()); i++) {
        ListItem *item = folders->items[static_cast<int>(i)].get();
        if (!all && !item->marked) continue;
        guest::Local<MsString> name;
        name->init();
        const char *text = item->text.c_str();
        name->assign(text, static_cast<uint32_t>(std::strlen(text)));
        if (all) {  // push_back(move(name)), inlined
            if (names->last.a == names->end.a) guest::call_thiscall(0x4046f0, names.get(), 0u);
            if (names->last) names->last->move_from(name.get());
            names->last.a += sizeof(MsString);
        } else {
            guest::call_thiscall(0x477130, names.get(), name.get());  // push_back(name)
        }
        name->destroy();
        push_index(indices.get(), static_cast<int32_t>(i));
    }
    bool with_default = all || folders->items[1]->marked != 0;
    {
        // track_list_fill(list, with_default, names, indices): both vectors by value (copies, freed by
        // the callee); the indices are copied first
        guest::Local<MsVector<int32_t>> idx_copy;
        guest::Local<MsVector<MsString>> names_copy;
        guest::call_thiscall(0x477090, idx_copy.get(), indices.get());
        guest::call_thiscall(0x4771b0, names_copy.get(), names.get());
        track_list_fill(&g_menu_widgets.get()[W_TRACK_LIST], with_default, names_copy.get(), idx_copy.get());
    }
    if (indices->first)
        guest::call_stdcall(0x4775b0, indices->first, (indices->end.a - indices->first.a) / 4);
    if (names->first) {
        guest::call_regs(0x4050e0, names->first.a, names->last.a, 0u, 0u);  // destroy the strings
        guest::call_stdcall(0x404c00, names->first, (names->end.a - names->first.a) / static_cast<uint32_t>(sizeof(MsString)));
    }

    // the folder button: the single marked folder's name, else "several folders"
    int marked = 0, which = -1;
    for (int i = 0; i < folders->items.size(); i++) {
        if (folders->items[i]->marked) {
            marked++;
            which = i;
        }
    }
    const MsString *caption = marked == 1 ? &folders->items[which]->text : &g_lang[L_SEVERAL_FOLDERS];
    MsString *label = &g_menu_widgets.get()[W_FOLDER].label;
    if (label != caption) label->assign(*caption);
    tracks_mark_selected();

    // the "Select n" slider: 0 .. number of tracks shown
    Widget *count = &g_menu_widgets.get()[W_RANDOM_COUNT];
    count->min = 0;
    count->max = g_track_list->items.size();
    if (count->max < 0) count->min = count->max;
    if (count->max < count->pos) count->pos = count->max;
    if (count->pos < count->min) count->pos = count->min;
    Widget *tracks = &g_menu_widgets.get()[W_TRACK_LIST];
    tracks->pos = saved_scroll < tracks->max ? saved_scroll : tracks->max;

    // "<n> selected"
    guest::Local<MsStringStream> ss;
    ss->construct();
    *ss << static_cast<int>(g_selected_tracks.size());
    ss->put(STR_SPACE) << &g_lang[L_SELECTED];
    guest::Local<MsString> text;
    ss->str(text.get());
    MsString *sel_label = &g_menu_widgets.get()[W_SELECTED_COUNT].label;
    if (sel_label != text.get()) {
        sel_label->destroy();
        sel_label->move_assign(text.get());
    }
    text->destroy();
    guest::call_stdcall(0x4735a0, guest::addr(&g_menu_widgets.get()[W_SELECTED_COUNT]));  // widget_draw
    guest::call_stdcall(0x473ee0, guest::addr(&g_menu_widgets.get()[W_FOLDER_LIST]));     // list_draw
    ss->destruct();
}

// New folder button: asks for a name (lang 137 title, 138 text) and creates tracks\<name>. On success
// the folder is added to both folder lists and becomes the only marked one in the folder list, and the
// track list is refreshed; otherwise a message (lang 139) says it could not be created.
void track_new_folder_clicked(Widget *, uint32_t ecx) {
    uint32_t name = input_box(&g_lang[138], &g_lang[137]);
    if (!name) return;
    guest::Local<MsString> dir, t1, path;
    dir->init();
    dir->assign(&guest::at<char>(STR_TRACKS), 6);  // "tracks"
    MsString::concat_move(t1.get(), dir.get(), 0x49ccfcu);  // + "\\"
    MsString::concat_move(path.get(), t1.get(), name);
    const char *p = path->c_str();
    bool made = p && guest::call_cdecl(guest::at<uint32_t>(IMP__mkdir), guest::addr(p)) == 0;
    path->destroy();
    t1->destroy();
    dir->destroy();
    if (!made) {
        message_box(0x47da90, &g_lang[137], &g_lang[139], 1);
        return;
    }
    guest::Local<MsString> s;
    s->construct(name);
    list_insert_sorted(&widget(W_MOVE_TO_LIST), s.get());
    s->destroy();
    s->construct(name);
    int at = list_insert_sorted(&widget(W_FOLDER_LIST), s.get());
    s->destroy();
    ListData *folders = widget(W_FOLDER_LIST).list.get();
    for (uint32_t i = 0; i < static_cast<uint32_t>(folders->items.size()); i++) folders->items[static_cast<int>(i)]->marked = 0;
    folders->items[at]->marked = 1;
    track_list_refresh(nullptr);
    gui_redraw_all();
}

GLOBAL(int32_t, g_track_anchor, 0x4a406c);  // Shift range selection: the item it started from, -1 none

// A click in the track list (the list widget has already moved its selection): Ctrl makes the clicked
// track the only marked one; Shift marks the range from the anchor (the previous selection when the
// range starts) to the clicked track and unmarks the rest; without Shift the anchor is dropped. The
// clicked track is then (re)applied, and the "n selected" caption and the track list are redrawn.
// (A plain click's toggle happened in the list already.)
void track_list_clicked(Widget *list_widget) {
    keyboard_snapshot();
    const uint8_t *keys = g_keys.get();
    ListData *list = list_widget->list.get();
    auto marked = [&](int i) -> int32_t & { return list->items[i]->marked; };
    if ((keys[SDLK_RCTRL] & 1) || (keys[SDLK_LCTRL] & 1)) {
        g_track_anchor = -1;
        for (uint32_t i = 0; i < static_cast<uint32_t>(list->items.size()); i++) {
            if (marked(static_cast<int>(i))) {
                marked(static_cast<int>(i)) = 0;
                track_toggle(static_cast<int>(i), 0);
            }
        }
        marked(list->selected) = 1;
        track_toggle(list->selected, 1);
    }
    if (!(keys[SDLK_RSHIFT] & 1) && !(keys[SDLK_LSHIFT] & 1)) {
        g_track_anchor = -1;
    } else {
        int sel = list->selected;
        if (g_track_anchor < 0) {
            g_track_anchor = list->previous;
            if (g_track_anchor < 0) g_track_anchor = 0;
        }
        for (int i = 0; i < list->items.size(); i++) {
            int a = g_track_anchor;
            bool in_range = i < a ? i >= sel : (i <= sel || i == a);
            if (in_range) {
                if (marked(i) != 1) {
                    marked(i) = 1;
                    track_toggle(i, 1);
                }
            } else if (marked(i) != 0) {
                marked(i) = 0;
                track_toggle(i, 0);
            }
        }
    }
    track_toggle(list->selected, marked(list->selected));

    // "<n> selected"
    guest::Local<MsStringStream> ss;
    ss->construct();
    *ss << static_cast<int>(g_selected_tracks.size());
    ss->put(STR_SPACE) << &g_lang[L_SELECTED];
    guest::Local<MsString> text;
    ss->str(text.get());
    MsString *caption = &widget(W_SELECTED_COUNT).label;
    if (caption != text.get()) {
        caption->destroy();
        caption->move_assign(text.get());
    }
    text->destroy();
    guest::call_stdcall(0x4735a0, guest::addr(&widget(W_SELECTED_COUNT)));  // widget_draw
    guest::call_stdcall(0x473ee0, guest::addr(&widget(W_TRACK_LIST)));      // list_draw
    ss->destruct();
}

// "Delete tracks": deletes the selected tracks' files after a confirmation (title 146; one track: text 102
// + "'" + name + "'?", several: lang 148 + " <n> " + lang 149; none selected: lang 147). Lang 150 if a file
// could not be deleted. The selection is cleared and the track list refilled.
void delete_tracks_clicked(Widget *) {
    int n = g_selected_tracks.size();
    if (n < 1) {
        message_box(0x47da90, &g_lang[146], &g_lang[147], 1);
        return;
    }
    if (n == 1) {
        guest::Local<MsString> part1, part2, question;
        MsString::concat(part1.get(), &g_lang[L_REALLY_DELETE], 0x49d804u);                    // + "'"
        MsString::concat_move(part2.get(), part1.get(), guest::addr(selected_track_name(0) + 1));  // no '\'
        MsString::concat_move(question.get(), part2.get(), 0x49d800u);                         // + "'?"
        part2->destroy();
        part1->destroy();
        int yes = message_box(0x47da90, &g_lang[146], question.get(), 2);
        question->destroy();
        if (yes == 0) return;
    } else {
        guest::Local<MsStringStream> ss;
        ss->construct();
        int count = g_selected_tracks.size();
        *ss << &g_lang[148];
        ss->put(STR_SPACE) << count;
        ss->put(STR_SPACE) << &g_lang[149];
        guest::Local<MsString> question;
        ss->str(question.get());
        bool no = message_box(0x47da90, &g_lang[146], question.get(), 2) == 0;
        question->destroy();
        ss->destruct();
        if (no) return;
    }
    int failed = 0;
    guest::Local<MsString> path;
    path->init();
    for (int i = 0; i < n; i++) {
        const char *name = g_selected_tracks[i].c_str();
        const char *dir = g_game_dir.get();
        path->assign(dir, static_cast<uint32_t>(std::strlen(dir)));
        path->append(0x49ccfcu, 1);  // "\\"
        path->append(0x49ccf4u, 6);  // "tracks"
        path->append(guest::addr(name), static_cast<uint32_t>(std::strlen(name)));
        path->append(0x49cd00u, 4);  // ".trk"
        if (guest::call_cdecl(guest::at<uint32_t>(IMP__unlink), guest::addr(path->c_str())) != 0) failed++;
    }
    if (failed) message_box(0x47da90, &g_lang[146], &g_lang[150], 1);
    guest::call_regs(0x4050e0, g_selected_tracks.first.a, g_selected_tracks.last.a, 0u, 0u);  // destroy the strings
    g_selected_tracks.last = g_selected_tracks.first;
    track_list_refresh(nullptr);
    gui_redraw_all();
    path->destroy();
}

// Index of the last c in s, or -1 (basic_string::rfind(char), 0x477420).
static int rfind_char(const MsString *s, char c) {
    const char *d = s->c_str();
    for (int i = static_cast<int>(s->size) - 1; i >= 0; i--)
        if (d[i] == c) return i;
    return -1;
}

// "Move to": closes the folder drop-down (all widgets shown again but 27 and 28) and moves the selected
// tracks' files into the folder picked in it (tracks\<folder>\); lang 145 (title 144) if a file could
// not be moved. The selection is cleared, the list refilled and the page redrawn.
void move_to_clicked(Widget *) {
    Widget *w = g_menu_widgets.get();
    for (int i = 0; i < 29; i++) w[i].flags &= ~1u;
    w[27].flags |= 1;
    w[28].flags |= 1;
    ListData *folders = w[28].list.get();
    int n = folders->items.size();
    for (int i = 0; i < n; i++) folders->items[i]->marked = 0;
    int sel = folders->selected;
    if (sel >= 0 && sel < n) {
        enter_game_dir();
        guest::Local<MsString> tmp, tracks_dir, dest_dir;
        const char *dir = g_game_dir.get();
        tmp->init();
        tmp->assign(dir, static_cast<uint32_t>(std::strlen(dir)));
        tmp->append(0x49ccfcu, 1);  // "\\"
        tmp->append(0x49ccf4u, 6);  // "tracks"
        tracks_dir->init();
        tracks_dir->assign(*tmp);
        tmp->destroy();
        tmp->init();
        tmp->assign(*tracks_dir);
        tmp->append(0x49ccfcu, 1);
        tmp->append(g_menu_widgets.get()[28].list->items[sel]->text);
        tmp->append(0x49ccfcu, 1);
        dest_dir->init();
        dest_dir->assign(*tmp);
        tmp->destroy();
        int failed = 0;
        int count = g_selected_tracks.size();
        for (int i = 0; i < count; i++) {
            guest::Local<MsString> src, file, dst;
            const char *name = g_selected_tracks[i].c_str();
            tmp->init();
            tmp->assign(*tracks_dir);
            tmp->append(guest::addr(name), static_cast<uint32_t>(std::strlen(name)));
            tmp->append(0x49cd00u, 4);  // ".trk"
            src->init();
            src->assign(*tmp);
            tmp->destroy();
            int slash = rfind_char(src.get(), '\\');
            if (slash == -1) {
                failed++;
            } else {
                src->substr(file.get(), static_cast<uint32_t>(slash + 1), 0xffffffffu);
                tmp->init();
                tmp->assign(*dest_dir);
                tmp->append(*file);
                dst->init();
                dst->assign(*tmp);
                tmp->destroy();
                file->destroy();
                if (!guest::call_import(IMP_MoveFileA, guest::addr(src->c_str()), guest::addr(dst->c_str()))) failed++;
                dst->destroy();
            }
            src->destroy();
        }
        if (failed) message_box(0x47da90, &g_lang[144], &g_lang[145], 1);
        guest::call_regs(0x4050e0, g_selected_tracks.first.a, g_selected_tracks.last.a, 0u, 0u);  // destroy the strings
        g_selected_tracks.last = g_selected_tracks.first;
        track_list_refresh(nullptr);
        dest_dir->destroy();
        tracks_dir->destroy();
    }
    main_menu_draw_page();
    gui_redraw_all();
}
