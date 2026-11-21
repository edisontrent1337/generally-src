// The widget toolkit behind every menu screen.
//
// A screen is an array of Widgets handed to gui_open(); the toolkit then owns input focus, hover and
// click state and calls the screen's draw callbacks (one per widget type) whenever a widget needs
// redrawing. Only one screen is active; gui_push()/gui_pop() save and restore it around dialogs.
#pragma once
#include "bridge/msvc.h"

// Code address of a callback in the original, taking the widget (stdcall). Kept as an address while
// callbacks are still partly original code.
using GuiCallback = uint32_t;

struct ListItem {
    int32_t marked;  // cleared when the screen closes
    MsString text;
    int32_t folder;  // track lists: index of the item's folder in the folder list (0, 1: none)
    uint32_t data;   // the driver list: Driver*
};
static_assert(sizeof(ListItem) == 0x24);

struct ListData {
    int32_t selected;  // -1: none
    int32_t previous;  // selection before the last click, -1 otherwise
    int32_t visible_rows;
    int32_t columns;
    float column_w;
    float row_h;
    float scrollbar_w;
    MsVector<ptr32<ListItem>> items;
};
static_assert(sizeof(ListData) == 0x28);

enum WidgetType : uint8_t { WIDGET_BUTTON = 1, WIDGET_TEXT = 2, WIDGET_SLIDER = 3, WIDGET_LIST = 4 };

// Widget::state
enum : uint32_t { WS_FOCUS = 1, WS_DOWN = 2, WS_EDITING = 4, WS_INC_DOWN = 0x10, WS_DEC_DOWN = 0x20 };
// Widget::flags
enum : uint32_t {
    WF_HIDDEN = 1,
    WF_NO_FOCUS = 2,       // skipped by arrow-key navigation
    WF_SLIDER_WIDE = 4,    // slider: the bar reaches the arrow buttons
    WF_LIST_CLICK = 8,     // list: on_click fires when an item is clicked
    WF_DEFAULT = 0x20,     // pressed by Enter
    WF_CANCEL = 0x40,      // pressed by Escape
};
// GuiState::flags
enum : uint32_t { GUI_KEYBOARD = 1, GUI_FOCUS_FIRST = 4 };
constexpr int32_t kRedrawAll = 0xfffffff;

struct Widget {
    uint32_t handle;  // screen << 16 | index, set by gui_open
    int32_t redraw;   // what the draw callback should redraw (kRedrawAll or a part)
    uint32_t state;   // WS_*
    float x, y, w, h;
    // slider: value range; list: first visible row (scroll position); text field: length / max length
    int32_t min, pos, max;
    MsString label;
    uint8_t type;  // WidgetType
    uint8_t pad41;
    int16_t variant;  // drawing variant (3: highlighted menu entry); dialogs: result in the first widget
    int32_t id;
    uint32_t flags;  // WF_*
    ptr32<ListData> list;
    GuiCallback on_click;
};
static_assert(sizeof(Widget) == 0x54 && offsetof(Widget, label) == 0x28 && offsetof(Widget, list) == 0x4c);

struct GuiState {
    int32_t screen;
    GuiCallback draw[4];  // by widget type - 1; draw[0] (buttons) is always widget_draw
    float scale;          // screen pixels per layout unit
    uint32_t count;
    int32_t focus;        // widget under the last click / keyboard focus, or -1
    uint8_t dragging;     // a slider or scrollbar thumb is being dragged
    uint8_t pressed;      // mouse button held over the focused widget
    int16_t unknown22;
    float drag_offset;    // scrollbar drag: grab point within the thumb
    int32_t editing;      // text field being edited, or -1
    int16_t wheel;        // mouse wheel remainder (1/120 steps)
    int16_t unknown2e;
    uint32_t flags;       // GUI_*
    ptr32<Widget> widgets;
};
static_assert(sizeof(GuiState) == 0x38);

GLOBAL(GuiState, g_gui, 0x4a5180);
GLOBAL(uint8_t, g_gui_active, 0x4a47f2);
GLOBAL(char[0x200][10], g_key_names, 0x49b848);
GLOBAL(int32_t, g_menu_border, 0x4a4df8);  // frame width around menu widgets, pixels

// string literals of the original used by widget drawing (guest addresses)
constexpr uint32_t STR_MINUS = 0x49d7f8;  // "-"
constexpr uint32_t STR_PLUS = 0x49d7fc;   // "+"

Widget *widget_construct(Widget *w);
void gui_reset();
int gui_open(int screen, Widget *widgets, uint32_t count, float scale, GuiCallback draw_text, GuiCallback draw_slider,
             GuiCallback draw_list, uint32_t flags);
void gui_close();
GuiState *gui_push();
int gui_pop(ptr32<GuiState> *saved);
void gui_redraw_all();
void gui_set_focus(int index);
void widgets_to_screen(Widget *w, int count, float dx, float dy);
void widgets_to_layout(Widget *w, int count, float dx, float dy);

// input (gui_input.cpp)
void gui_mouse_motion(int x, int y, bool left_held);
void gui_mouse_down(int x, int y);
void gui_mouse_up(int x, int y);
void gui_wheel(int16_t delta);
void gui_key_char(int key);
void gui_enter_down();
void gui_enter_up();
void gui_escape_down();
void gui_escape_up();
void gui_focus_step(int dir);

// shared with gui_input.cpp
Widget &gui_widget(int64_t index);
void gui_call(GuiCallback cb, Widget *w);

void button_init(Widget *w, const MsString *label, float x, float y, float width, float height, GuiCallback on_click,
                 uint32_t flags);
void text_init(Widget *w, const MsString *text, float x, float y, float min_w, float height, int max_len,
               uint32_t flags);
void slider_init(Widget *w, const MsString *label, float x, float y, float min_w, float height, int min, int value,
                 int max, GuiCallback on_change, uint32_t flags);
void list_init(Widget *w, GuiCallback on_click, float x, float y, float width, float height, float column_w,
               float row_h, float scrollbar_w, uint32_t flags);

ListItem *list_item_init(ListItem *item, const MsString *text);
void list_item_delete(ListItem *item);
int list_add(Widget *w, const MsString *text);
int list_remove(Widget *w, int index);
void list_clear(Widget *w);
int list_insert(Widget *w, const MsString *text, int index);
int list_insert_sorted(Widget *w, const MsString *text);
int widgets_free(ptr32<Widget> *widgets, int count);
int list_find(ListData *d, const MsString *text);

void key_name(uint32_t key, MsString *out);

// draw callbacks (gui_draw.cpp)
void text_field_draw(Widget *w);
void slider_draw(Widget *w);
void list_draw(Widget *w);
