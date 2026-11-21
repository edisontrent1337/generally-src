// The Options screen (tyre wear, fuel, damage, race length unit, starting order, points) and the
// settings it shares with Advanced options.
#include <cstring>

#include "game/advanced_options.h"
#include "game/driver_options.h"
#include "game/gui.h"
#include "game/lang.h"
#include "game/menu.h"
#include "game/menu_page.h"
#include "game/race_options.h"


static void set_label(Widget &w, int text) {
    const MsString *s = &g_lang[text];
    if (&w.label != s) w.label.assign(*s);
}

static void set_text(Widget &w, const char *s) { w.label.assign(s, static_cast<uint32_t>(std::strlen(s))); }

// Loads the race options into the Advanced options widgets: tyre wear (4, reversed), the slider 6
// (w1e / 2.56, rounded), the on/off toggles 7, 8, 9 (and 17 from the Options screen; captions lang 5 / 6),
// the sliders 10..12 and the text fields 14, 16 (and 20 from the pause menu).
static void adv_widgets_load(bool from_pause) {
    RaceOptions &o = g_race_options;
    Widget *a = g_adv_widgets.get();
    a[4].id = 3 - o.b1c;
    a[6].pos = guest::to_int(static_cast<float>(o.w1e) / 2.56f + 0.5f);
    a[7].id = o.b19 + 2;
    a[8].id = o.b18 + 2;
    a[9].id = o.b1a + 2;
    if (!from_pause) a[17].id = o.b1b + 2;
    set_label(a[4], o.b1c ? 6 : 5);
    set_label(a[7], o.b19 == 0 ? 6 : 5);
    set_label(a[8], o.b18 == 0 ? 6 : 5);
    set_label(a[9], o.b1a == 0 ? 6 : 5);
    if (!from_pause) set_label(a[17], o.b1b == 0 ? 6 : 5);
    a[10].pos = o.b20;
    a[11].pos = o.b21;
    a[12].pos = o.b22;
    set_text(a[14], o.text[0]);
    set_text(a[16], o.text[1]);
    if (from_pause) set_text(a[20], o.text[2]);
}

// Draws the Options page: title lang 35 and the 14 captions lang 39..52 in column 14 (the seventh, a
// heading, in colour 0x73).
void options_draw_page() {
    static const int kRows[14] = {7, 10, 13, 16, 19, 22, 29, 33, 36, 39, 42, 45, 48, 51};
    guest::Local<PageLabel[14]> labels;
    for (int i = 0; i < 14; i++) {
        PageLabel &l = (*labels)[i];
        l.x = 14;
        l.y = kRows[i];
        l.color = i == 6 ? 0x73 : 0xf0;
        l.text.init();
        l.text.assign(g_lang[39 + i]);
    }
    draw_page(&g_lang[35], *labels, 14);
    guest::call_stdcall(0x4970c6, guest::addr(*labels), static_cast<uint32_t>(sizeof(PageLabel)), 14u, 0x47db40u);
}

// Options button (and coming back from Advanced options / Driver options, when the main menu is already
// saved): the first time, saves the main menu's GUI and loads the race options into both screens'
// widgets; then opens the Options screen.
void options_open() {
    if (g_menu_gui) {
        gui_close();
    } else {
        g_menu_gui = gui_push();
        RaceOptions &o = g_race_options;
        Widget *w = g_options_widgets.get();
        w[4].pos = o.b09;
        w[5].pos = o.b0a;
        w[6].pos = o.b0c;
        w[7].pos = o.b0b;
        for (int i = 0; i < 7; i++) w[10 + i].pos = o.b02[i];  // points for places 1..6 and the fastest lap
        w[8].id = o.length_unit + 2;
        w[9].id = o.b0e + 2;
        static const int kUnitText[3] = {L_LENGTH_LAPS, L_LENGTH_KM, 164};  // 164: minutes
        set_label(w[8], kUnitText[o.length_unit == 1 ? 1 : o.length_unit == 0 ? 0 : 2]);
        set_label(w[9], o.b0e == 0 ? L_START_RANDOM : o.b0e == 1 ? L_START_POINTS : L_START_POINTS_REVERSED);

        adv_widgets_load(false);
    }
    options_draw_page();
    gui_open(4, g_options_widgets.get(), 0x11, guest::at<float>(0x49dc7c) /* 1.0 */, 0, 0x473b00, 0, 0);
    gui_redraw_all();
}

GLOBAL(ptr32<GuiState>, g_pause_saved_gui, 0x4a5118);  // the pause menu below Advanced options

// Advanced button (Options screen, Driver options, pause menu). From the pause menu (button id 3) the
// pause menu's GUI is saved, the buttons that make no sense during a race are hidden (Advanced widget 1,
// Driver options widgets 1, 17, 18, these shown in variant 2) and the race options are loaded; otherwise
// the current screen is just closed. Then opens Advanced options.
void advanced_clicked(Widget *button) {
    if (button->id == 3) {
        g_pause_saved_gui = gui_push();
        Widget *a = g_adv_widgets.get(), *d = g_dopt_widgets.get();
        a[1].flags |= WF_HIDDEN;
        d[1].flags |= WF_HIDDEN;
        d[1].variant = 2;
        d[17].flags |= WF_HIDDEN;
        d[17].variant = 2;
        d[18].flags |= WF_HIDDEN;
        d[18].variant = 2;
        adv_widgets_load(true);
    } else {
        gui_close();
    }
    adv_draw_labels();
    gui_open(5, g_adv_widgets.get(), 0x15, guest::at<float>(0x49dc7c) /* 1.0 */, 0, 0x473b00, 0x473ee0, 0);
    gui_redraw_all();
}
