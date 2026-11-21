// Option buttons that cycle through a few states when clicked. The state is kept in the widget's id
// (2, 3, ...) and the label shows the state's text.
#include "game/gui.h"
#include "game/lang.h"
#include "game/menu.h"

static void set_label(Widget *w, int text) {
    if (&w->label != &g_lang[text]) w->label.assign(g_lang[text]);
}

// on (3) / off (2)
void on_off_clicked(Widget *w) {
    if (w->id == 3) {
        w->id = 2;
        set_label(w, L_OFF);
    } else if (w->id == 2) {
        w->id = 3;
        set_label(w, L_ON);
    }
    widget_draw(w);
}

// Advances the state through first..last (wrapping around) and shows labels[state - first].
static void cycle(Widget *w, uint32_t last, const int *labels) {
    w->id++;
    if (static_cast<uint32_t>(w->id) > last) w->id = 2;
    if (w->id >= 2 && static_cast<uint32_t>(w->id) <= last) set_label(w, labels[w->id - 2]);
    widget_draw(w);
}

// race length unit: laps / kilometers / minutes
void length_unit_clicked(Widget *w) {
    static const int labels[] = {L_LENGTH_LAPS, L_LENGTH_KM, L_LENGTH_MINUTES};
    cycle(w, 4, labels);
}

// starting order: random / by points / by points reversed
void start_order_clicked(Widget *w) {
    static const int labels[] = {L_START_RANDOM, L_START_POINTS, L_START_POINTS_REVERSED};
    cycle(w, 4, labels);
}

// type of a new driver: human / computer
void driver_type_clicked(Widget *w) {
    static const int labels[] = {L_HUMAN, L_COMPUTER};
    cycle(w, 3, labels);
}
