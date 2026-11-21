// Widget toolkit: mouse and keyboard input (see gui.h).
//
// Mouse: a press picks the widget under the cursor as the focus; buttons fire on release over them,
// sliders and list scrollbars react to clicks on their arrows, track and thumb (and can be dragged),
// list items are selected on press. Keyboard: arrows move the focus between buttons and text fields,
// Enter presses the default (or focused) button, Escape the cancel button, printable keys edit the
// text field being edited.
//
// Quirks of the original kept on purpose: several paths index the widget array with -1 (focus unset)
// and Escape scans one widget past the end.
#include "game/gui.h"

static Widget &widget(int64_t i) { return gui_widget(i); }
static void call(GuiCallback cb, Widget *w) {
    if (cb) gui_call(cb, w);
}
static GuiCallback draw_cb(WidgetType t) { return g_gui.draw[t - 1]; }

// Every handler starts like this: a focused widget that has been hidden loses the focus.
static bool focus_hidden() {
    if (g_gui.focus >= 0 && (widget(g_gui.focus).flags & WF_HIDDEN)) {
        g_gui.focus = -1;
        return true;
    }
    return false;
}

static float layout(int pixels) { return static_cast<float>(pixels) / g_gui.scale; }

// x87/SSE conversion of a value the original treats as unsigned
static float ufloat(int32_t v) { return static_cast<float>(static_cast<uint32_t>(v)); }

// Slider geometry: arrow buttons of one height at both ends; the value bar between them, inset by
// another half height unless WF_SLIDER_WIDE.
static float slider_t(const Widget &w, float mx, bool wide) {
    float h = w.h;
    return wide ? (mx - (w.x + h)) / (w.w - h * 2.0f) : (mx - (h * 1.5f + w.x)) / (w.w - h * 3.0f);
}

// item index under a point (layout units relative to the widget), unchecked
static int list_hit(const Widget &w, const ListData *d, float dx, float dy) {
    return (guest::to_int(dy / d->row_h) + w.pos) * d->columns + guest::to_int(dx / d->column_w);
}

void gui_mouse_motion(int x, int y, bool left_held) {
    if (focus_hidden() || !left_held) return;
    GuiCallback cb;
    Widget *pw;
    if (!g_gui.dragging) {
        if (g_gui.focus < 0) return;
        Widget &w = widget(g_gui.focus);
        pw = &w;
        if (w.type == WIDGET_TEXT) return;
        float dx = layout(x) - w.x, dy = layout(y) - w.y;
        if (dx < 0.0f || dy < 0.0f || w.w < dx || w.h < dy) {  // left the widget: release look
            if (!g_gui.pressed) return;
            g_gui.pressed = 0;
            w.state &= ~WS_DOWN;
        } else {  // back over it: pressed look
            if (g_gui.pressed) return;
            g_gui.pressed = 1;
            w.state |= WS_DOWN;
        }
        w.redraw = 2;
        switch (w.type) {
        case WIDGET_BUTTON: cb = draw_cb(WIDGET_BUTTON); break;
        case WIDGET_SLIDER: cb = draw_cb(WIDGET_SLIDER); break;
        case WIDGET_LIST: cb = draw_cb(WIDGET_LIST); break;
        default: w.redraw = 0; return;
        }
    } else {
        Widget &w = widget(g_gui.focus);
        pw = &w;
        if (w.type == WIDGET_SLIDER) {  // dragging the value
            int old = w.pos, v;
            float t = slider_t(w, layout(x), (w.flags & WF_SLIDER_WIDE) != 0);
            if (0.0f <= t) {
                v = w.max;
                if (t < 1.0f) v = guest::to_int(static_cast<float>(v - w.min) * t + 0.5f) + w.min;
            } else {
                v = w.min;
            }
            w.pos = v;
            if (old == v) return;
            w.redraw = 8;
            call(w.on_click, &w);
            cb = draw_cb(WIDGET_SLIDER);
        } else if (w.type == WIDGET_LIST) {  // dragging the scrollbar thumb
            ListData *d = w.list;
            int max = w.max, old = w.pos;
            float sb = d->scrollbar_w;
            int n = d->items.size();
            float frac = ufloat(n - max) / ufloat(n);
            float track = w.h - sb * 2.0f;
            float min_frac = sb / track;
            if (frac < min_frac) frac = min_frac;
            float t = (((layout(y) - (w.y + sb)) / track - g_gui.drag_offset) - frac * 0.5f) / (1.0f - frac);
            if (0.0f <= t) {
                w.pos = t < 1.0f ? guest::to_int(static_cast<float>(max - w.min) * t) + w.min : max;
            } else {
                w.pos = w.min;
            }
            if (old == w.pos) return;
            w.redraw = 8;
            cb = draw_cb(WIDGET_LIST);
        } else {
            return;
        }
    }
    call(cb, pw);
    pw->redraw = 0;
}

// The text field being edited stops editing (and reports its new text).
static void end_editing() {
    Widget &e = widget(g_gui.editing);
    e.state &= ~(WS_FOCUS | WS_EDITING);
    e.redraw |= 5;
    call(draw_cb(WIDGET_TEXT), &e);
    e.redraw = 0;
    call(e.on_click, &e);
    g_gui.editing = -1;
}

// Unfocus the previously focused widget (after focus moved elsewhere)
static void blur(int index, uint32_t clear) {
    Widget &o = widget(index);
    o.redraw = kRedrawAll;
    o.state &= ~clear;
    if (o.type == WIDGET_BUTTON) call(draw_cb(WIDGET_BUTTON), &o);
    else if (o.type == WIDGET_TEXT) call(draw_cb(WIDGET_TEXT), &o);
    o.redraw = 0;
}

static void slider_press(Widget &w, float mx) {
    float h = w.h;
    w.state |= WS_DOWN;
    w.redraw |= 2;
    float t = (mx - (w.x + h)) / (w.w - h * 2.0f);
    if (0.0f < t) {
        if (t <= 1.0f) {  // on the bar: jump there and start dragging
            int old = w.pos;
            if (!(w.flags & WF_SLIDER_WIDE)) t = (mx - (h * 1.5f + w.x)) / (w.w - h * 3.0f);
            g_gui.dragging = 1;
            int max = w.max, min = w.min;
            int v = guest::to_int(static_cast<float>(max - min) * t + 0.5f) + min;
            w.pos = v;
            if (v < min) w.pos = min;
            else if (max < v) w.pos = max;
            if (old != w.pos) w.redraw |= 8;
        } else if (w.pos < w.max) {  // "+" arrow
            w.pos++;
            w.redraw |= 8;
            w.state |= WS_INC_DOWN;
        }
    } else if (w.min < w.pos) {  // "-" arrow
        w.pos--;
        w.redraw |= 8;
        w.state |= WS_DEC_DOWN;
    }
    call(draw_cb(WIDGET_SLIDER), &w);
}

// returns true if the draw callback ran (and reset redraw)
static bool list_press(Widget &w, float mx, float my) {
    if (w.list->selected < 0) {
        w.redraw = 8;
        w.list->selected = 0;
    }
    ListData *d = w.list;
    float dx = mx - w.x;
    float sb = d->scrollbar_w;
    if (dx <= w.w - sb) {  // items
        if (dx < static_cast<float>(d->columns) * d->column_w) {
            int i = list_hit(w, d, dx, my - w.y);
            if (i < d->items.size()) {
                if (i < 0) i = 0;
                d->previous = d->selected;
                w.list->selected = i;
                ListItem *item = w.list->items[i];
                if (item) item->marked ^= 1;
                w.redraw |= 0x10000008;
                call(draw_cb(WIDGET_LIST), &w);
                if ((w.flags & WF_LIST_CLICK) && w.on_click) call(w.on_click, &w);
                w.list->previous = -1;
            }
        }
        return false;
    }
    // scrollbar: arrows at both ends, page up/down beside the thumb, drag on the thumb
    w.state |= WS_DOWN;
    w.redraw |= 2;
    int32_t redraw = w.redraw;
    float t = (my - (w.y + sb)) / (w.h - sb * 2.0f);
    if (0.0f < t) {
        if (t <= 1.0f) {
            int max = w.max;
            if (max != 0) {
                float sbw = d->scrollbar_w;
                int n = d->items.size();
                float frac = ufloat(n - max) / ufloat(n);
                float track = w.h - sbw * 2.0f;
                float min_frac = sbw / track;
                if (frac < min_frac) frac = min_frac;
                int pos = w.pos;
                float at = (my - (sbw + w.y)) / track;
                float thumb = ((1.0f - frac) * static_cast<float>(pos)) / static_cast<float>(max);
                if (thumb <= at) {
                    if (at < thumb + frac) {
                        g_gui.dragging = 1;
                        g_gui.drag_offset = (at - thumb) - frac * 0.5f;
                    } else {  // page down
                        int v = d->visible_rows - 1 + pos;
                        w.pos = max < v ? max : v;
                        w.redraw = redraw | 8;
                    }
                } else {  // page up
                    int v = pos - d->visible_rows + 1;
                    w.pos = v < w.min ? w.min : v;
                    w.redraw = redraw | 8;
                }
            }
        } else if (w.pos < w.max) {
            w.pos++;
            w.state |= WS_INC_DOWN;
            w.redraw = redraw | 8;
        }
    } else if (w.min < w.pos) {
        w.pos--;
        w.state |= WS_DEC_DOWN;
        w.redraw = redraw | 8;
    }
    if (draw_cb(WIDGET_LIST)) {
        call(draw_cb(WIDGET_LIST), &w);
        return true;
    }
    return false;
}

void gui_mouse_down(int x, int y) {
    int old = g_gui.focus;
    if (focus_hidden()) return;
    g_gui.focus = -1;
    float mx = layout(x), my = layout(y);
    for (int i = 0; i < static_cast<int>(g_gui.count); i++) {
        Widget &w = widget(i);
        if (w.flags & WF_HIDDEN) continue;
        if (0.0f <= mx - w.x && 0.0f <= my - w.y && mx - w.x <= w.w && my - w.y <= w.h) {
            g_gui.focus = i;
            break;
        }
    }
    if (g_gui.editing >= 0 && g_gui.editing != g_gui.focus) end_editing();

    if (old >= 0 && old != g_gui.focus) {  // the previous focus loses it
        Widget &o = widget(old);
        o.state &= ~(WS_FOCUS | WS_DOWN);
        o.redraw = 3;
        GuiCallback cb = 0;
        switch (o.type) {
        case WIDGET_BUTTON: cb = draw_cb(WIDGET_BUTTON); break;
        case WIDGET_TEXT:
            if (g_gui.editing < 0) cb = draw_cb(WIDGET_TEXT);
            else o.state |= WS_FOCUS;
            break;
        case WIDGET_SLIDER: cb = draw_cb(WIDGET_SLIDER); break;
        case WIDGET_LIST: cb = draw_cb(WIDGET_LIST); break;
        default: break;
        }
        call(cb, &o);
        o.redraw = 0;
    }

    if (g_gui.focus < 0) return;
    Widget &w = widget(g_gui.focus);
    g_gui.pressed = 1;
    if (w.state == 0) {
        w.redraw = 1;
        w.state = WS_FOCUS;
    } else {
        w.redraw = 0;
    }
    switch (w.type) {
    case WIDGET_BUTTON:
        w.state |= WS_DOWN;
        w.redraw |= 2;
        if (draw_cb(WIDGET_BUTTON)) {
            call(draw_cb(WIDGET_BUTTON), &w);
            w.redraw = 0;
            return;
        }
        break;
    case WIDGET_TEXT:
        g_gui.editing = g_gui.focus;
        w.state |= WS_EDITING;
        w.redraw |= 4;
        if (draw_cb(WIDGET_TEXT)) {
            call(draw_cb(WIDGET_TEXT), &w);
            w.redraw = 0;
            return;
        }
        break;
    case WIDGET_SLIDER:
        slider_press(w, mx);
        if (w.on_click) {
            call(w.on_click, &w);
            w.redraw = 0;
            return;
        }
        break;
    case WIDGET_LIST:
        if (list_press(w, mx, my)) {
            w.redraw = 0;
            return;
        }
        break;
    default: break;
    }
    w.redraw = 0;
}

void gui_mouse_up(int x, int y) {
    if (focus_hidden()) return;
    float mx = layout(x), my = layout(y);
    if (g_gui.focus < 0 || !g_gui.pressed) return;
    Widget &w = widget(g_gui.focus);
    uint8_t type = w.type;
    w.state &= ~WS_DOWN;
    w.redraw = 2;
    GuiCallback cb;
    if (type == WIDGET_BUTTON) {  // click
        call(draw_cb(WIDGET_BUTTON), &w);
        cb = w.on_click;
    } else if (type == WIDGET_SLIDER) {
        g_gui.dragging = 0;
        w.state &= ~(WS_INC_DOWN | WS_DEC_DOWN);
        call(draw_cb(WIDGET_SLIDER), &w);
        cb = w.on_click;
    } else if (type == WIDGET_LIST) {
        g_gui.dragging = 0;
        ListData *d = w.list;
        w.state &= ~(WS_INC_DOWN | WS_DEC_DOWN);
        float dx = mx - w.x;
        if (dx < static_cast<float>(d->columns) * d->column_w) {  // released on an item
            if (d->items.size() <= list_hit(w, d, dx, my - w.y)) {
                w.redraw = 0;
                return;
            }
            call(draw_cb(WIDGET_LIST), &w);
            cb = w.on_click;
        } else {
            cb = draw_cb(WIDGET_LIST);
        }
    } else {
        w.redraw = 0;
        return;
    }
    call(cb, &w);
    w.redraw = 0;
}

void gui_wheel(int16_t delta) {
    if (g_gui.focus < 0) return;
    Widget &w = widget(g_gui.focus);
    if (w.flags & WF_HIDDEN) {
        g_gui.focus = -1;
        return;
    }
    if (w.type != WIDGET_SLIDER && w.type != WIDGET_LIST) return;
    int16_t sum = static_cast<int16_t>(g_gui.wheel + delta);
    int steps = -sum / 120;  // wheel up (positive) scrolls back
    g_gui.wheel = static_cast<int16_t>(sum + static_cast<int16_t>(steps) * 120);
    int old = w.pos, v = steps + old;
    w.pos = v;
    if (v < w.min) w.pos = w.min;
    else if (w.max < v) w.pos = w.max;
    if (old == w.pos) return;
    w.redraw = 8;
    GuiCallback cb;
    if (w.type == WIDGET_SLIDER) {
        call(draw_cb(WIDGET_SLIDER), &w);
        cb = w.on_click;
    } else {
        cb = draw_cb(WIDGET_LIST);
    }
    call(cb, &w);
    w.redraw = 0;
}

// A key typed while a text field has the focus: printable characters are appended (up to the
// field's maximum length), backspace deletes; the first key starts editing.
void gui_key_char(int key) {
    if (g_gui.focus < 0) return;
    Widget *w = &widget(g_gui.focus);
    if (w->flags & WF_HIDDEN) {
        g_gui.focus = -1;
        return;
    }
    if (w->type != WIDGET_TEXT) return;
    if (g_gui.editing >= 0) w = &widget(g_gui.editing);
    int len = static_cast<int>(w->label.size);
    if (static_cast<uint32_t>(key - 0x20) < 0x5f && len < w->max) {
        guest::call_thiscall(0x48aab0, &w->label, static_cast<uint32_t>(static_cast<int8_t>(key)));  // push_back
        w->pos = static_cast<int32_t>(w->label.size);
        w->redraw = 8;
        if (g_gui.editing < 0) {
            w->state |= WS_EDITING;
            w->redraw = 0xc;
            g_gui.editing = g_gui.focus;
        }
    } else if (key == 8 && len >= 1) {
        if (g_gui.editing < 0) {
            w->state |= WS_EDITING;
            w->redraw = 4;
            g_gui.editing = g_gui.focus;
        } else {
            guest::call_thiscall(0x403970, &w->label, static_cast<uint32_t>(len - 1));  // erase from len - 1
            w->pos = static_cast<int32_t>(w->label.size);
            w->redraw = 8;
        }
    } else {
        w->redraw = 0;
        return;
    }
    call(draw_cb(WIDGET_TEXT), w);
    w->redraw = 0;
}

// Give the focus to `index` and show it pressed (Enter / Escape).
static void press_focus(int old, int index) {
    g_gui.focus = index;
    if (old >= 0 && old != g_gui.focus) blur(old, WS_FOCUS);
    Widget &w = widget(g_gui.focus);
    w.redraw = kRedrawAll;
    w.state |= WS_FOCUS | WS_DOWN;
    call(draw_cb(WIDGET_BUTTON), &widget(g_gui.focus));
    widget(g_gui.focus).redraw = 0;
}

// Enter: press the default button, or with keyboard navigation the focused button.
void gui_enter_down() {
    int old = g_gui.focus;
    if (focus_hidden()) return;
    int found = -1;
    for (int i = 0; i < static_cast<int>(g_gui.count); i++)
        if ((widget(i).flags & (WF_DEFAULT | WF_HIDDEN)) == WF_DEFAULT && widget(i).type == WIDGET_BUTTON) found = i;
    int focus = g_gui.focus;
    if (found >= 0) {
        focus = found;
        if (g_gui.flags & GUI_KEYBOARD) {
            if (g_gui.focus < 0) {  // (the original then also skips unfocusing the old widget)
                press_focus(-1, found);
                return;
            }
            if (!(widget(g_gui.focus).flags & WF_HIDDEN)) {
                if (widget(g_gui.focus).type == WIDGET_BUTTON) found = g_gui.focus;
                focus = found;
            }
        }
    }
    press_focus(old, focus);
}

// Enter released: a button pressed by Enter fires; a text field toggles editing.
void gui_enter_up() {
    if (focus_hidden()) return;
    if (g_gui.editing >= 0) {
        Widget &e = widget(g_gui.editing);
        if (g_gui.focus == g_gui.editing) {
            e.state &= ~WS_EDITING;
            e.redraw |= 4;
        } else {
            e.state &= ~(WS_FOCUS | WS_EDITING);
            e.redraw |= 5;
        }
        call(draw_cb(WIDGET_TEXT), &widget(g_gui.editing));
        e.redraw = 0;
        call(e.on_click, &e);
        g_gui.editing = -1;
    }
    if (g_gui.focus < 0) return;
    Widget &w = widget(g_gui.focus);
    if (g_gui.editing < 0 && w.type == WIDGET_TEXT) {  // start editing the focused field
        g_gui.editing = g_gui.focus;
        w.state |= WS_EDITING;
        w.redraw |= 4;
        call(draw_cb(WIDGET_TEXT), &w);
        w.redraw = 0;
    }
    if (w.type == WIDGET_BUTTON && (w.state & WS_DOWN)) {
        w.redraw = 2;
        w.state &= ~WS_DOWN;
        call(draw_cb(WIDGET_BUTTON), &w);
        call(w.on_click, &w);
    }
}

// Escape: press the cancel button.
void gui_escape_down() {
    int old = g_gui.focus;
    if (focus_hidden()) return;
    int found = -1;
    if (static_cast<int>(g_gui.count) < 0) return;
    for (int i = 0; i <= static_cast<int>(g_gui.count); i++)  // (one past the end, as the original)
        if ((widget(i).flags & (WF_CANCEL | WF_HIDDEN)) == WF_CANCEL && widget(i).type == WIDGET_BUTTON) found = i;
    if (found < 0) return;
    press_focus(old, found);
}

// Arrow keys (with keyboard navigation): focus the next/previous button or text field, wrapping.
void gui_focus_step(int dir) {
    if (focus_hidden()) return;
    if (!(g_gui.flags & GUI_KEYBOARD)) return;
    int step = dir >= 0 ? 1 : -1;
    if (g_gui.focus >= 0) {
        uint8_t t = widget(g_gui.focus).type;
        if (t != WIDGET_BUTTON && t != WIDGET_TEXT) return;
    }
    int count = static_cast<int>(g_gui.count);
    if (count < 1) return;
    int i = g_gui.focus, wrapped = g_gui.focus - count, next;
    for (int tries = 1;; tries++) {
        i += step;
        wrapped += step;
        next = i < count ? (i < 0 ? count - 1 : i) : wrapped;
        Widget &w = widget(next);
        if (!(w.flags & (WF_HIDDEN | WF_NO_FOCUS)) && (w.type == WIDGET_BUTTON || w.type == WIDGET_TEXT)) break;
        if (count < tries + 1) return;
    }
    if (g_gui.focus == next) return;
    if (g_gui.focus >= 0) {
        int old = g_gui.focus;
        g_gui.focus = next;
        blur(old, WS_FOCUS);
    }
    g_gui.focus = next;
    Widget &w = widget(next);
    w.redraw = kRedrawAll;
    w.state |= WS_FOCUS;
    if (w.type == WIDGET_BUTTON) call(draw_cb(WIDGET_BUTTON), &widget(g_gui.focus));
    else if (w.type == WIDGET_TEXT) call(draw_cb(WIDGET_TEXT), &widget(g_gui.focus));
    widget(g_gui.focus).redraw = 0;
}
