// The widget toolkit (see gui.h).
#include "game/gui.h"
#include "game/strings.h"
#include "game/text.h"

#include <cstring>

Widget &gui_widget(int64_t i) { return g_gui.widgets.get()[i]; }
void gui_call(GuiCallback cb, Widget *w) { guest::call_stdcall(cb, w); }
static Widget &widget(int64_t i) { return gui_widget(i); }
static void call(GuiCallback cb, Widget *w) { gui_call(cb, w); }

// Redraw one widget completely with its type's draw callback.
static void draw_full(Widget &w) {
    w.redraw = kRedrawAll;
    if (w.type >= WIDGET_BUTTON && w.type <= WIDGET_LIST) {
        GuiCallback cb = g_gui.draw[w.type - 1];
        if (cb) call(cb, &w);
    }
    w.redraw = 0;
}

void gui_reset() {
    g_gui.screen = 0;
    g_gui.count = 0;
    g_gui.widgets = nullptr;
    g_gui.focus = -1;
    g_gui.dragging = 0;  // (drag_offset and the padding are left alone)
    g_gui.pressed = 0;
    g_gui.scale = 0;
    g_gui.editing = -1;
    g_gui.flags = 0;
    g_gui.wheel = 0;
    for (GuiCallback &cb : g_gui.draw) cb = 0;
}

// flags: GUI_KEYBOARD, GUI_FOCUS_FIRST (give the first button the keyboard focus)
int gui_open(int screen, Widget *widgets, uint32_t count, float scale, GuiCallback draw_text, GuiCallback draw_slider,
             GuiCallback draw_list, uint32_t flags) {
    if (g_gui_active) return -1;
    g_gui_active = 1;
    gui_reset();
    g_gui.flags = flags & ~4u;
    g_gui.draw[WIDGET_TEXT - 1] = draw_text;
    g_gui.draw[WIDGET_SLIDER - 1] = draw_slider;
    g_gui.screen = screen;
    g_gui.count = count;
    g_gui.widgets = widgets;
    g_gui.scale = scale;
    g_gui.draw[WIDGET_BUTTON - 1] = 0x4735a0;  // widget_draw
    g_gui.draw[WIDGET_LIST - 1] = draw_list;

    for (uint32_t i = 0; i < g_gui.count; i++) {
        Widget &w = widget(i);
        w.handle = (static_cast<uint32_t>(screen) << 16) + i;
        if (w.flags & WF_HIDDEN) continue;
        w.redraw = kRedrawAll;
        w.state = 0;
        if (w.type == WIDGET_BUTTON && (flags & GUI_FOCUS_FIRST) && g_gui.focus < 0) {
            g_gui.focus = static_cast<int32_t>(i);
            w.state |= WS_FOCUS;
        }
        draw_full(w);
    }
    return 0;
}

void gui_close() {
    if (!g_gui_active) return;
    g_gui_active = 0;
    for (uint32_t i = 0; i < g_gui.count; i++) {
        Widget &w = widget(i);
        w.state = 0;
        if (w.type == WIDGET_LIST && w.list) {
            w.list->previous = -1;
            w.list->selected = -1;
            for (uint32_t k = 0; k < static_cast<uint32_t>(widget(i).list->items.size()); k++)
                widget(i).list->items[k]->marked = 0;
        }
    }
    gui_reset();
}

// Save the active screen (to open a dialog over it); returns the saved state, or the live state if no
// screen is active.
GuiState *gui_push() {
    if (!g_gui_active) return &g_gui;
    g_gui_active = 0;
    auto *saved = static_cast<GuiState *>(guest::op_new(sizeof(GuiState)));
    *saved = g_gui;
    gui_reset();
    return saved;
}

int gui_pop(ptr32<GuiState> *saved) {
    if (g_gui_active) return -1;
    g_gui_active = 1;
    gui_reset();
    g_gui = **saved;
    guest::op_delete(saved->get(), sizeof(GuiState));
    *saved = nullptr;
    gui_redraw_all();
    return 0;
}

void gui_redraw_all() {
    for (uint32_t i = 0; i < g_gui.count; i++)
        if (!(widget(i).flags & WF_HIDDEN)) draw_full(widget(i));
}

void gui_set_focus(int index) {
    if (index >= static_cast<int>(g_gui.count)) index = static_cast<int>(g_gui.count) - 1;
    if (index < 0) index = -1;
    g_gui.focus = index;
    Widget &w = widget(index);
    w.state |= WS_FOCUS;
    if (w.type == WIDGET_TEXT) {  // focusing a text field starts editing it
        g_gui.editing = index;
        w.state |= WS_EDITING;
        w.redraw |= 4;
        if (g_gui.draw[WIDGET_TEXT - 1]) call(g_gui.draw[WIDGET_TEXT - 1], &w);
        w.redraw = 0;
    }
}

// Escape released: the pressed cancel button fires.
void gui_escape_up() {
    if (g_gui.focus < 0) return;
    Widget &w = widget(g_gui.focus);
    if (w.flags & WF_HIDDEN) {
        g_gui.focus = -1;
        return;
    }
    if ((w.state & WS_DOWN) && (w.flags & (WF_CANCEL | WF_HIDDEN)) == WF_CANCEL && w.type == WIDGET_BUTTON) {
        w.state &= ~WS_DOWN;
        w.redraw = 2;
        if (g_gui.draw[WIDGET_BUTTON - 1]) call(g_gui.draw[WIDGET_BUTTON - 1], &w);
        if (w.on_click) call(w.on_click, &w);
    }
}

// ------------------------------------------------------------------ widget setup

static void common_init(Widget *w, WidgetType type, uint32_t flags) {
    w->variant = 0;
    w->id = 0;
    w->state = 0;
    w->handle = 0;
    w->redraw = 0;
    w->list = nullptr;
    w->flags = flags;
    w->type = type;
}

void button_init(Widget *w, const MsString *label, float x, float y, float width, float height, GuiCallback on_click,
                 uint32_t flags) {
    common_init(w, WIDGET_BUTTON, flags);
    w->min = w->pos = w->max = 0;
    if (&w->label != label) w->label.assign(*label);
    w->x = x;
    w->y = y;
    w->w = width;
    w->h = height;
    w->on_click = on_click;
}

// A text field is at least 4 heights wide.
static float field_width(float min_w, float height) {
    float w = height * 4.0f;
    return w > min_w ? w : min_w;
}

void text_init(Widget *w, const MsString *text, float x, float y, float min_w, float height, int max_len,
               uint32_t flags) {
    common_init(w, WIDGET_TEXT, flags);
    w->min = 0;
    if (&w->label != text) w->label.assign(*text);
    w->x = x;
    w->y = y;
    w->w = field_width(min_w, height);
    w->h = height;
    w->pos = static_cast<int32_t>(text->size);
    w->on_click = 0;
    w->max = max_len > 0 ? max_len : 32;
    w->label.reserve(static_cast<uint32_t>(w->max) + 1);
}

void slider_init(Widget *w, const MsString *label, float x, float y, float min_w, float height, int min, int value,
                 int max, GuiCallback on_change, uint32_t flags) {
    common_init(w, WIDGET_SLIDER, flags);
    if (&w->label != label) w->label.assign(*label);
    w->x = x;
    w->y = y;
    w->min = min;
    w->pos = value;
    w->max = max;
    w->w = field_width(min_w, height);
    w->h = height;
    w->on_click = on_change;
}

void list_init(Widget *w, GuiCallback on_click, float x, float y, float width, float height, float column_w,
               float row_h, float scrollbar_w, uint32_t flags) {
    common_init(w, WIDGET_LIST, flags);
    w->min = w->pos = w->max = 0;
    w->label.clear();
    w->x = x;
    w->y = y;
    w->w = width;
    w->h = height;
    w->on_click = on_click;

    auto *d = static_cast<ListData *>(guest::op_new(sizeof(ListData)));
    std::memset(static_cast<void *>(d), 0, sizeof *d);
    w->list = d;
    d->column_w = column_w;
    d->row_h = row_h;
    d->scrollbar_w = scrollbar_w;
    if (d->row_h > w->h) d->row_h = w->h;
    float inner_w = w->w - d->scrollbar_w;
    if (d->column_w > inner_w) d->column_w = inner_w;
    d->visible_rows = guest::to_int(w->h / d->row_h);
    d->columns = guest::to_int((w->w - d->scrollbar_w) / d->column_w);
    d->selected = -1;
    d->previous = -1;
}

// ------------------------------------------------------------------ list boxes

ListItem *list_item_init(ListItem *item, const MsString *text) {
    item->marked = 0;
    item->text.init();
    item->text.assign(*text);
    item->folder = 0;
    item->data = 0;
    return item;
}

void list_item_delete(ListItem *item) {
    item->text.destroy();
    guest::op_delete(item, sizeof(ListItem));
}

// rows needed for n items
static int rows(const ListData *d, int n) {
    int64_t q = n / d->columns, r = n % d->columns;
    return static_cast<int>(q) + (r != 0);
}

int list_add(Widget *w, const MsString *text) {
    if (w->type != WIDGET_LIST || !w->list) return 0;
    ListData *d = w->list;
    guest::Local<ptr32<ListItem>> item;
    *item = list_item_init(static_cast<ListItem *>(guest::op_new(sizeof(ListItem))), text);
    guest::call_thiscall(0x48aa40, &d->items, item.get());  // vector::push_back
    int n = d->items.size();
    int r = rows(d, n);
    if (d->visible_rows < r) w->max = r - d->visible_rows;
    return n;
}

// Inserts a new item before position index (clamped to 0..size); returns the new item count.
int list_insert(Widget *w, const MsString *text, int index) {
    if (!w || w->type != WIDGET_LIST || !w->list) return 0;
    ListData *d = w->list;
    if (index < 0) index = 0;
    if (index > d->items.size()) index = d->items.size();
    guest::Local<ptr32<ListItem>> item;
    guest::Local<uint32_t> result;
    *item = list_item_init(static_cast<ListItem *>(guest::op_new(sizeof(ListItem))), text);
    // vector::insert(where, value) (0x48aaf0, thiscall)
    guest::call_thiscall(0x48aaf0, &d->items, result.get(), d->items.begin() + index, item.get(), item.get());
    int n = d->items.size();
    int r = rows(d, n);
    if (d->visible_rows < r) w->max = r - d->visible_rows;
    return n;
}

// Removes the item from the list (the item itself is not freed).
int list_remove(Widget *w, int index) {
    if (!w || w->type != WIDGET_LIST) return 0;
    ListData *d = w->list;
    if (!d || index < 0 || index >= d->items.size()) return 0;
    ptr32<ListItem> *at = d->items.begin() + index;
    std::memmove(at, at + 1, (d->items.last.a - guest::addr(at + 1)) & ~3u);
    d->items.last.a -= 4;
    int n = d->items.size();
    int r = rows(d, n);
    if (d->visible_rows < r) {
        w->max = r - d->visible_rows;
        if (w->max < w->pos) w->pos = w->max;
    } else {
        w->max = 0;
    }
    return n;
}

void list_clear(Widget *w) {
    if (!w || w->type != WIDGET_LIST || !w->list) return;
    ListData *d = w->list;
    for (uint32_t i = 0; i < static_cast<uint32_t>(d->items.size()); i++)
        if (d->items[i]) list_item_delete(d->items[i]);
    d->items.last = d->items.first;
    d->selected = -1;
    d->previous = -1;
    w->min = 0;
    w->pos = 0;
    w->max = 0;
    w->variant = 0;
}

// Index of the item with this text (case-insensitive), or -1.
int list_find(ListData *d, const MsString *text) {
    if (!d || text->size == 0) return -1;
    for (uint32_t i = 0; i < static_cast<uint32_t>(d->items.size()); i++)
        if (ms_stricmp(d->items[i]->text.c_str(), text->c_str()) == 0) return static_cast<int>(i);
    return -1;
}

void key_name(uint32_t key, MsString *out) {
    if (key < 0x200) out->assign(g_key_names[key], static_cast<uint32_t>(std::strlen(g_key_names[key])));
}

// Frees a screen's widget array (allocated with new Widget[count]) with all list boxes' items and data.
int widgets_free(ptr32<Widget> *widgets, int count) {
    if (!*widgets) return -1;
    for (int i = 0; i < count; i++) {
        Widget &w = widgets->get()[i];
        if (!w.list) continue;
        for (uint32_t k = 0; k < static_cast<uint32_t>(w.list->items.size()); k++) {
            if (!w.list->items[k]) continue;
            list_item_delete(w.list->items[k]);
            w.list->items[k] = nullptr;
        }
        ListData *d = w.list;
        if (d) {
            if (d->items.first) {  // vector storage: allocator::deallocate (0x4775b0, stdcall)
                guest::call_stdcall(0x4775b0, d->items.first, (d->items.end.a - d->items.first.a) >> 2);
                d->items.first = nullptr;
                d->items.last = nullptr;
                d->items.end = nullptr;
            }
            guest::op_delete(d, sizeof(ListData));
        }
        w.list = nullptr;
    }
    if (uint32_t a = widgets->a) {  // delete[]: destruct each widget (0x47cf10), free the block with its count
        uint32_t n = guest::at<uint32_t>(a - 4);
        guest::call_stdcall(0x4970c6, a, static_cast<uint32_t>(sizeof(Widget)), n, 0x47cf10u);
        guest::call_cdecl(0x4970b8, a - 4, n * static_cast<uint32_t>(sizeof(Widget)) + 4);
    }
    *widgets = nullptr;
    return 0;
}

// the Widget constructor: only the label is initialised
Widget *widget_construct(Widget *w) {
    w->label.init();
    return w;
}

// Layout units to screen pixels: positions and sizes are scaled by the font size (the layout unit) and
// moved by (dx, dy); lists also scale their column width, row height and scrollbar width.
void widgets_to_screen(Widget *w, int count, float dx, float dy) {
    float unit = static_cast<float>(g_font_size);
    for (; count > 0; count--, w++) {
        w->x = w->x * unit + dx;
        w->w = unit * w->w;
        w->y = unit * w->y + dy;
        w->h = w->h * unit;
        if (w->type == WIDGET_LIST && w->list) {
            ListData *d = w->list.get();
            d->scrollbar_w = d->scrollbar_w * unit;
            d->column_w = d->column_w * unit;
            d->row_h = d->row_h * unit;
        }
    }
}

// The inverse: screen pixels back to layout units.
void widgets_to_layout(Widget *w, int count, float dx, float dy) {
    float unit = static_cast<float>(g_font_size);
    for (; count > 0; count--, w++) {
        w->x = (w->x - dx) / unit;
        w->w = w->w / unit;
        w->y = (w->y - dy) / unit;
        w->h = w->h / unit;
        if (w->type == WIDGET_LIST && w->list) {
            ListData *d = w->list.get();
            d->scrollbar_w = d->scrollbar_w / unit;
            d->column_w = d->column_w / unit;
            d->row_h = d->row_h / unit;
        }
    }
}

// Inserts text into a list "in sorted position" and returns the index it got. The search halves the
// range but only asks whether the texts differ (case-insensitively), not which is smaller, so it walks
// to the end of the range unless it meets an equal text: in effect new texts are appended (the callers
// add in sorted order anyway). Kept as in the original.
int list_insert_sorted(Widget *w, const MsString *text) {
    int hi = w->list->items.size() - 1;
    if (hi < 0) return list_add(w, text) - 1;
    int lo = 0, mid = hi / 2;
    if (hi > 0) {
        do {
            if (!differs_nocase(text, &w->list->items[mid]->text)) break;
            lo = mid + 1;
            if (mid >= hi) lo = mid;
            mid = (hi + lo) / 2;
        } while (lo < hi);
    }
    if (differs_nocase(text, &w->list->items[mid]->text)) mid++;
    int count = list_insert(w, text, mid);
    if (mid >= count) mid = count - 1;
    return mid;
}
