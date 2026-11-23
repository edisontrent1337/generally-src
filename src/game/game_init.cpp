// Game start-up (0x478d40), also run again after the language, palette, sound set or resolution changed:
// loads the language, gr.ini, the video mode, the sound effects, fonts and cars, and builds the widget
// arrays of every screen. Arrays that already exist only get their captions from the (new) language.
#include <cstring>

#include "game/advanced_options.h"
#include "game/driver.h"
#include "game/driver_options.h"
#include "game/file.h"
#include "game/file_dialog.h"
#include "game/fmath.h"
#include "game/ini.h"
#include "game/input.h"
#include "game/lang.h"
#include "game/menu.h"
#include "game/message_box.h"
#include "game/name_entry.h"
#include "game/race_options.h"
#include "game/race_state.h"
#include "game/render.h"
#include "game/selected_tracks.h"
#include "game/sound.h"
#include "game/text.h"
#include "game/track_info.h"
#include "game/video.h"
#include "bridge/imports.h"

GLOBAL(MsVector<DisplayMode>, g_display_modes, 0x4aafd0);  // filled at start-up
GLOBAL(uint32_t, g_draw_view, 0x4a4d20);
GLOBAL(int32_t, g_cfg_tyre_autosort, 0x4a4dd8);
GLOBAL(uint32_t, g_joy_state, 0x4a4cfc);
GLOBAL(uint8_t[0x50], g_unknown_4ab528, 0x4ab528);

namespace {
constexpr uint32_t STR_EMPTY = 0x49b574, STR_PERCENT = 0x49d820 /* "%|%" */, STR_KG = 0x49d824 /* "kg|kg" */,
                   STR_ENGLISH = 0x49d754, STR_DEFAULT = 0x49d75c, STR_ZERO_SPACE = 0x49d81c /* "0 " */,
                   STR_RES_FORMAT = 0x49d7d0 /* "%dx%d" */, STR_SPACE = 0x49cc4c;

// new Widget[n] (count in front, each built by widget_construct, 0x47cef0)
Widget *new_widgets(uint32_t n) {
    uint32_t p = guest::call_cdecl(0x496c14, n * static_cast<uint32_t>(sizeof(Widget)) + 4);
    if (!p) return nullptr;
    guest::at<uint32_t>(p) = n;
    guest::call_stdcall(0x4971ad, p + 4, static_cast<uint32_t>(sizeof(Widget)), n, 0x47cef0u, 0x47cf10u);
    return &guest::at<Widget>(p + 4);
}

void relabel(Widget &w, int lang) { w.label.assign(g_lang[lang]); }

// a caption built from a guest C string for one init call
struct TempLabel {
    guest::Local<MsString> s;
    explicit TempLabel(uint32_t cstr) { s->construct(cstr); }
    ~TempLabel() { s->destroy(); }
    const MsString *get() { return s.get(); }
};

void button(Widget &w, int lang, float x, float y, float width, float height, GuiCallback cb, uint32_t flags = 0) {
    button_init(&w, &g_lang[lang], x, y, width, height, cb, flags);
}
void button(Widget &w, uint32_t cstr, float x, float y, float width, float height, GuiCallback cb,
            uint32_t flags = 0) {
    TempLabel t(cstr);
    button_init(&w, t.get(), x, y, width, height, cb, flags);
}
void slider(Widget &w, uint32_t cstr, float x, float y, int value, int max, GuiCallback cb = 0, uint32_t flags = 0) {
    TempLabel t(cstr);
    slider_init(&w, t.get(), x, y, 26, 2, 0, value, max, cb, flags);
}

// the four buttons along the bottom of the Options, Advanced and Driver options screens
void bottom_buttons(Widget *w, int lang1, GuiCallback cb1, float x1, int lang2, GuiCallback cb2, float x2) {
    button(w[0], 24 /* Cancel */, 0, 56, 20, 4, 0x481d00, WF_CANCEL);
    button(w[1], lang1, x1, 56, 20, 4, cb1);
    button(w[2], lang2, x2, 56, 20, 4, cb2);
    button(w[3], 28 /* OK */, 60, 56, 20, 4, 0x481d00, WF_DEFAULT);
    w[3].variant = 1;
}

void main_menu_build(float dx, float dy) {
    if (Widget *w = g_menu_widgets) {
        relabel(w[0], 17);  // Exit
        relabel(w[1], 25);  // Options
        relabel(w[2], 90);  // Load
        relabel(w[3], 18);  // Start
        relabel(w[20], 134);
        relabel(w[21], 135);
        relabel(w[22], 136);
        relabel(w[17], 70);
        relabel(w[25], 131);
        relabel(w[26], 132);
        guest::Local<MsStringStream> ss;
        guest::Local<MsString> text;
        ss->construct();
        *ss << selected_track_count();
        ss->put(STR_SPACE);
        *ss << &g_lang[L_SELECTED];
        ss->str(text.get());
        guest::call_thiscall(0x404b80, &w[W_SELECTED_COUNT].label, text.get());  // = move(text)
        text->destroy();
        MsVector<ptr32<ListItem>> &folders = w[W_FOLDER_LIST].list->items;
        if (folders.size() >= 2) {
            folders[0]->text.assign(g_lang[L_ALL_TRACKS]);
            folders[1]->text.assign(g_lang[L_DEFAULT_TRACKS]);
        }
        ss->destruct();
        return;
    }
    Widget *w = new_widgets(30);
    g_menu_widgets = w;
    button(w[0], 17, 0, 56, 20, 4, 0x474bb0, WF_CANCEL);
    button(w[1], 25, 20, 56, 20, 4, 0x481260);
    button(w[2], 90, 40, 56, 20, 4, 0x482860);
    w[2].id = 2;
    button(w[3], 18, 60, 56, 20, 4, 0x474b30, WF_DEFAULT);
    for (int i = 0; i < 6; i++) {
        button(w[W_DRIVER_CAR + 1 + i], STR_EMPTY, 14, 11 + 3 * i, 18, 3, 0x4832d0, WF_NO_FOCUS);
        w[W_DRIVER_CAR + 1 + i].id = 20 + i;
    }
    for (int i = 0; i < 6; i++) {
        button(w[10 + i], STR_EMPTY, 32, 11 + 3 * i, 15, 3, 0x4764f0, WF_NO_FOCUS);
        w[10 + i].id = 10 + i;
    }
    list_init(&w[W_DRIVER_LIST], 0x476400, 50, 11, 16, 16, 14, 2, 2, 0);
    w[W_DRIVER_LIST].id = 1;
    driver_list_fill(&w[W_DRIVER_LIST]);
    button(w[17], 70, 50, 27.5f, 16, 3, 0x4766e0);
    {
        guest::Local<MsString> empty;
        empty->init();
        slider_init(&w[W_LENGTH_UNIT], empty.get(), 40, 7, 26, 2, 1, g_race_options.length, 0xff, 0x474810, 0);
    }
    list_init(&w[W_TRACK_LIST], 0x474f40, 11, 35, 58, 16, 14, 2, 2, WF_LIST_CLICK);
    w[W_TRACK_LIST].id = 0;
    g_track_list = w[W_TRACK_LIST].list;
    button(w[20], 134, 11, 51.5f, 14, 3, 0x476280);
    button(w[21], 135, 25, 51.5f, 14, 3, 0x476ba0);
    button(w[22], 136, 39, 51.5f, 14, 3, 0x474830);
    {
        guest::Local<MsString> empty;
        empty->init();
        slider_init(&w[W_RANDOM_COUNT], empty.get(), 53.5f, 52, 15.5f, 2, 1, 1, 0, 0, 0);
    }
    button(w[W_FOLDER], 128, 11, 32, 22, 3, 0x4760e0);
    w[W_FOLDER].id = 5;
    button(w[25], 131, 33, 32, 10, 3, 0x476710);
    w[25].id = 6;
    button(w[26], 132, 43, 32, 10, 3, 0x476960);
    w[26].id = 6;
    list_init(&w[W_FOLDER_LIST], 0x4752f0, 11, 35, 24, 22, 22, 2, 2, WF_HIDDEN | WF_LIST_CLICK | 0x10);
    list_init(&w[W_MOVE_TO_LIST], 0x475b00, 11, 29, 24, 22, 22, 2, 2, WF_HIDDEN);
    folder_lists_fill(&w[W_FOLDER_LIST], &w[W_MOVE_TO_LIST]);
    // the default folder starts marked: "[Default Tracks]" if present, else the first entry
    MsVector<ptr32<ListItem>> &folders = w[W_FOLDER_LIST].list->items;
    folders[0]->marked = 1;
    for (int i = 0; i < folders.size(); i++) {
        const MsString &t = folders[i]->text;
        if (t.size == 16 && std::memcmp(t.c_str(), "[Default Tracks]", 16) == 0) {
            folders[0]->marked = 0;
            folders[i]->marked = 1;
        }
    }
    button(w[W_SELECTED_COUNT], STR_EMPTY, 54, 32, 26, 3, 0);
    w[W_SELECTED_COUNT].id = 7;
    guest::Local<MsString> text;
    MsString::concat(text.get(), STR_ZERO_SPACE, &g_lang[L_SELECTED]);
    MsString &caption = w[W_SELECTED_COUNT].label;
    caption.destroy();
    caption.move_assign(text.get());
    text->destroy();
    widgets_to_screen(w, 30, dx, dy);
}

void options_build(float dx, float dy) {
    if (Widget *w = g_options_widgets) {
        relabel(w[0], 24);
        relabel(w[1], 26);
        relabel(w[2], 27);
        relabel(w[3], 28);
        for (int i = 10; i < 17; i++) relabel(w[i], 58);
        return;
    }
    Widget *w = new_widgets(17);
    g_options_widgets = w;
    bottom_buttons(w, 26 /* Advanced */, 0x481650, 40, 27 /* Drivers */, 0x481910, 20);
    slider(w[4], STR_PERCENT, 40, 7, 0, 100);
    slider(w[5], STR_PERCENT, 40, 10, 0, 100);
    slider(w[6], STR_KG, 40, 13, 0, 200);
    slider(w[7], STR_PERCENT, 40, 16, 0, 100);
    button(w[8], 2, 40, 18.5f, 26, 3, 0x48c9d0);
    w[8].id = 2;
    button(w[9], 56, 40, 21.5f, 26, 3, 0x48ca70);
    w[9].id = 2;
    static const int points[7] = {9, 6, 4, 3, 2, 1, 1};
    for (int i = 0; i < 7; i++)
        slider_init(&w[10 + i], &g_lang[58], 40, 33 + 3 * i, 26, 2, 0, points[i], 50, 0, 0);
    widgets_to_screen(w, 17, dx, dy);
}

void advanced_build(float dx, float dy) {
    if (Widget *w = g_adv_widgets) {
        relabel(w[0], 24);
        relabel(w[1], 25);
        relabel(w[2], 27);
        relabel(w[3], 28);
        return;
    }
    Widget *w = new_widgets(21);
    g_adv_widgets = w;
    bottom_buttons(w, 25 /* Options */, 0x481260, 20, 27 /* Drivers */, 0x481910, 40);
    button(w[4], L_ON, 40, 17.5f, 26, 3, 0x48c960);
    w[4].id = 3;
    slider(w[6], STR_PERCENT, 40, 24, 0, 100);
    button(w[7], L_ON, 40, 26.5f, 26, 3, 0x48c960);
    w[7].id = 3;
    button(w[8], L_OFF, 40, 29.5f, 26, 3, 0x48c960);
    w[8].id = 2;
    button(w[9], L_ON, 40, 32.5f, 26, 3, 0x48c960);
    w[9].id = 3;
    slider(w[10], STR_PERCENT, 40, 38, 0, 100);
    slider(w[11], STR_PERCENT, 40, 41, 0, 100);
    slider(w[12], STR_PERCENT, 40, 44, 0, 100);

    list_init(&w[18], 0x48c050, 40, 24, 26, 12, 24, 2, 2, WF_HIDDEN);
    w[18].id = 0;
    adv_fill_resolutions(&w[18]);
    guest::Local<char[16]> res;
    guest::call_regs(0x472560, 0, 0, guest::addr(res.get()), STR_RES_FORMAT, g_screen_w, g_screen_h);  // sprintf
    button(w[5], guest::addr(res.get()), 40, 20.5f, 26, 3, 0x48bea0);
    w[5].id = 0;

    list_init(&w[19], 0x48c300, 40, 10, 26, 12, 24, 2, 2, WF_HIDDEN);
    w[19].id = 0;
    adv_fill_palettes(&w[19]);
    button(w[20], guest::addr(g_race_options.text[2]), 40, 6.5f, 26, 3, 0x48c150);
    w[20].id = 0;

    list_init(&w[13], 0x48c5b0, 40, 13, 26, 12, 24, 2, 2, WF_HIDDEN);
    w[13].id = 0;
    adv_fill_languages(&w[13]);
    button(w[14], STR_ENGLISH, 40, 9.5f, 26, 3, 0x48c400);
    w[14].id = 0;

    list_init(&w[15], 0x48c860, 40, 16, 26, 12, 24, 2, 2, WF_HIDDEN);
    w[15].id = 0;
    adv_fill_soundsets(&w[15]);
    button(w[16], STR_DEFAULT, 40, 12.5f, 26, 3, 0x48c6b0);
    w[16].id = 0;

    button(w[17], L_OFF, 40, 48.7f, 26, 3, 0x48c960);
    w[17].id = 2;
    widgets_to_screen(w, 21, dx, dy);
}

void driver_options_build(float dx, float dy) {
    if (Widget *w = g_dopt_widgets) {
        relabel(w[0], 24);
        relabel(w[1], 25);
        relabel(w[2], 26);
        relabel(w[3], 28);
        relabel(w[17], 70);
        relabel(w[18], 71);
        relabel(w[19], 72);
        relabel(w[22], 28);
        relabel(w[23], 24);
        return;
    }
    Widget *w = new_widgets(24);
    g_dopt_widgets = w;
    bottom_buttons(w, 25 /* Options */, 0x481260, 40, 26 /* Advanced */, 0x481650, 20);
    static const GuiCallback bind[4] = {0x48bb70, 0x48bc30, 0x48bd00, 0x48bdd0};
    for (int i = 0; i < 4; i++) button(w[4 + i], STR_EMPTY, 40, 36.5f + 3 * i, 26, 3, bind[i]);
    list_init(&w[8], 0x48b5a0, 46, 10, 16, 12, 14, 2, 2, WF_HIDDEN);
    w[8].id = 1;
    w[8].flags = WF_HIDDEN;
    w[8].list->selected = 0;
    button(w[9], STR_EMPTY, 40, 6.5f, 26, 3, 0x48b530);
    w[9].id = 0;
    static const float rows[6] = {13, 16, 19, 23, 26, 29};
    for (int i = 0; i < 6; i++) slider(w[10 + i], STR_EMPTY, 40, rows[i], 0, 0xff, 0x48b950);
    slider(w[16], STR_EMPTY, 40, 37, 0, 200, 0x48b4f0, WF_HIDDEN);
    button(w[17], 70, 10, 52, 20, 3, 0x48cb10);
    button(w[18], 71, 30, 52, 20, 3, 0x48d3d0);
    button(w[19], 72, 50, 52, 20, 3, 0x48d5d0);
    {
        TempLabel t(STR_EMPTY);
        text_init(&w[20], t.get(), 38, 26.5f, 16, 3, 12, WF_HIDDEN);
    }
    button(w[21], 83, 38, 30.5f, 16, 3, 0x48d360, WF_HIDDEN);
    w[21].id = 2;
    button(w[22], 28, 40, 37, 20, 4, 0x48cd20, WF_DEFAULT | WF_HIDDEN);
    w[22].id = 2;
    button(w[23], 24, 20, 37, 20, 4, 0x48cd20, WF_CANCEL | WF_HIDDEN);
    widgets_to_screen(w, 24, dx, dy);
}

void car_dropdown_build(float dx, float dy) {
    if (g_car_dropdown_widgets) return;
    Widget *w = new_widgets(2);
    g_car_dropdown_widgets = w;
    list_init(&w[0], 0x4833a0, 10, 2, 19, 18, 17, 3, 2, 0);
    w[0].id = 2;
    button(w[1], STR_EMPTY, 14, 12, 18, 3, 0x4833a0, WF_NO_FOCUS);
    w[1].id = 0x14;
    for (int i = 0; i < g_car_count; i++) {
        TempLabel name(guest::addr(g_cars.get()[i].name));
        list_add(&w[0], name.get());
    }
    widgets_to_screen(w, 2, dx, dy);
}

void pause_build(float dx, float dy) {
    static const int labels[6] = {29, 151, 25, 91, 117, 30};
    if (Widget *w = g_pause_widgets) {
        for (int i = 0; i < 6; i++) relabel(w[i], labels[i]);
        return;
    }
    static const GuiCallback cbs[6] = {0x46f370, 0x46f380, 0x481650, 0x482860, 0x46f490, 0x46f430};
    Widget *w = new_widgets(6);
    g_pause_widgets = w;
    for (int i = 0; i < 6; i++) {
        button(w[i], labels[i], 32, 21 + 3 * i, 16, 3, cbs[i], i == 0 ? WF_DEFAULT | WF_CANCEL : 0);
        w[i].variant = 3;
        if (i == 2 || i == 3) w[i].id = 3;
    }
    widgets_to_screen(w, 6, dx, dy);
}

void file_dialog_build(float dx, float dy) {
    if (Widget *w = g_file_dialog_widgets) {
        relabel(w[0], 24);
        relabel(w[1], 92);
        relabel(w[2], 90);
        return;
    }
    Widget *w = new_widgets(5);
    g_file_dialog_widgets = w;
    button(w[0], 24, 10, 46, 20, 4, 0x482940, WF_CANCEL);
    w[0].id = 0;
    button(w[1], 92, 30, 46, 20, 4, 0x48f4c0);
    button(w[2], 90, 50, 46, 20, 4, 0x482940, WF_DEFAULT);
    w[2].id = 2;
    list_init(&w[3], 0x48f450, 29, 16.5f, 36, 22, 34, 2, 2, 0);
    {
        TempLabel t(STR_EMPTY);
        text_init(&w[4], t.get(), 29, 40.5f, 36, 3, 32, 0);
    }
    widgets_to_screen(w, 5, dx, dy);
}

// a screen of buttons only, relabelled from the same table
struct Btn {
    int lang;
    float x, y;
    GuiCallback cb;
    uint32_t flags;
    int id;
};
void buttons_build(ptr32<Widget> &array, const Btn *b, int n, float dx, float dy) {
    if (Widget *w = array) {
        for (int i = 0; i < n; i++) relabel(w[i], b[i].lang);
        return;
    }
    Widget *w = new_widgets(static_cast<uint32_t>(n));
    array = w;
    for (int i = 0; i < n; i++) {
        button(w[i], b[i].lang, b[i].x, b[i].y, 20, 4, b[i].cb, b[i].flags);
        if (b[i].id) w[i].id = b[i].id;
    }
    widgets_to_screen(w, n, dx, dy);
}

void track_info_build(float dx, float dy) {
    if (Widget *w = g_track_info_widgets) {
        relabel(w[0], 125);
        relabel(w[1], 31);
        return;
    }
    Widget *w = new_widgets(3);
    g_track_info_widgets = w;
    button(w[0], 125, 20, 50, 20, 4, 0x482e30);
    button(w[1], 31, 40, 50, 20, 4, 0x482db0, WF_DEFAULT | WF_CANCEL);
    button(w[2], STR_EMPTY, 14, 43, 20, 4, 0x4831b0);
    widgets_to_screen(w, 3, dx, dy);
}

void input_box_build(float dx, float dy) {
    if (Widget *w = g_inputbox_widgets) {
        relabel(w[0], 31);
        relabel(w[1], 24);
        return;
    }
    Widget *w = new_widgets(3);
    g_inputbox_widgets = w;
    button(w[0], 31, 20, 37, 20, 4, 0x484c90, WF_DEFAULT);
    w[0].id = 3;
    button(w[1], 24, 40, 37, 20, 4, 0x484c90, WF_CANCEL);
    w[1].id = 2;
    {
        TempLabel t(STR_EMPTY);
        text_init(&w[2], t.get(), 20, 30, 40, 2, 0, 0);
    }
    widgets_to_screen(w, 3, dx, dy);
}
}  // namespace

// Returns 1, or -1 if the font could not be built.
int game_init() {
    enter_game_dir();
    {
        guest::Local<MsString> name;
        name->init();
        name->assign(g_race_options.text[0], static_cast<uint32_t>(std::strlen(g_race_options.text[0])));
        lang_load(name.get());
        name->destroy();
    }
    settings_load();
    g_draw_view = g_cfg_tyre_autosort ? 0x40f7f0 : 0x414120;
    if (!g_back) video_init(g_race_options.b1c != 0);
    bool supported = false;
    for (int i = 0; i < g_display_modes.size(); i++) {
        const DisplayMode &m = g_display_modes[i];
        if (m.w == g_race_options.i10 && m.h == g_race_options.i14) {
            supported = true;
            break;
        }
    }
    if (!supported) {
        g_screen_w = 640;
        g_screen_h = 480;
        video_set_mode(640, 480, g_race_options.b1c != 0);
    }
    if (!g_audio && !audio_open()) {
        audio_close();
        sounds_free();
    }
    if (g_audio) {
        guest::Local<MsString> set;
        set->init();
        set->assign(g_race_options.text[1], static_cast<uint32_t>(std::strlen(g_race_options.text[1])));
        sound_effects_load(set.get());
        set->destroy();
    }

    if (g_disc_widths) {
        guest::call_cdecl(0x496bd7, g_disc_widths);  // free
        g_disc_widths = 0;
    }
    g_disc_widths = guest::call_cdecl(0x496c14, 400u);
    auto *widths = &guest::at<float>(g_disc_widths);
    for (int i = 0; i < 100; i++) {
        float t = static_cast<float>(i) * 0.02f - 0.99f;
        widths[i] = guest_sqrtf(1.0f - t * t);
    }
    palette_reset();
    int r = font_build();
    if (r == -1 || r == -2) return -1;
    car_models_load();
    std::memset(g_unknown_4ab528, 0, sizeof g_unknown_4ab528);
    if (!g_keys) g_keys.a = guest::call_cdecl(0x496c14, 0x143u);
    if (g_joy_state) guest::call_cdecl(0x496bd7, g_joy_state);  // freed but not cleared
    if (guest::call_import(IMP_SDL_NumJoysticks)) {
        uint64_t size = static_cast<uint64_t>(guest::call_import(IMP_SDL_NumJoysticks)) * 0x50;
        g_joy_state = guest::call_cdecl(0x496c14, size > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(size));
    }

    // the screens are laid out in font-size units on an 80 x 60 grid, centred
    float dx = static_cast<float>(g_screen_w - g_font_size * 80) * 0.5f;
    float dy = static_cast<float>(g_screen_h - g_font_size * 60) * 0.5f;
    main_menu_build(dx, dy);
    options_build(dx, dy);
    advanced_build(dx, dy);
    driver_options_build(dx, dy);
    car_dropdown_build(dx, dy);
    pause_build(dx, dy);
    file_dialog_build(dx, dy);
    track_info_build(dx, dy);
    static const Btn name[2] = {{125, 20, 46, 0x482fd0, 0, 0}, {31, 40, 46, 0x482db0, WF_DEFAULT | WF_CANCEL, 0}};
    buttons_build(g_name_widgets, name, 2, dx, dy);
    static const Btn msgbox[3] = {{31, 30, 37, 0x484640, WF_DEFAULT | WF_CANCEL, 0},
                                  {32, 20, 37, 0x484640, WF_DEFAULT | WF_HIDDEN, 3},
                                  {33, 40, 37, 0x484640, WF_CANCEL | WF_HIDDEN, 2}};
    buttons_build(g_msgbox_widgets, msgbox, 3, dx, dy);
    input_box_build(dx, dy);
    main_menu_show();
    return 1;
}
