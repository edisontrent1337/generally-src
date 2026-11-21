// Entry points of the original's widget toolkit functions (see game/gui.h).
#include "game/text.h"
#include "bridge/guest.h"
#include "game/gui.h"
#include "game/message_box.h"
#include "game/menu.h"

using namespace abi;

REWRITE(402770, "MsString::c_str", 0, 0, RET_EAX) { ret_ptr(c, ecx<MsString>(c)->c_str()); }

REWRITE(488d30, "gui_reset", 0, 0, RET_NONE) { gui_reset(); }
REWRITE(488ab0, "gui_open", 6, 0, RET_EAX) {
    ret(c, gui_open(static_cast<int>(ECX), edx<Widget>(c), arg(c, 0), xmm(c, 1), arg(c, 2), arg(c, 3), arg(c, 4),
                    arg(c, 5)));
}
REWRITE(488c00, "gui_close", 0, 0, RET_NONE) { gui_close(); }
REWRITE(488da0, "gui_push", 0, 0, RET_EAX) {
    GuiState *s = gui_push();
    if (s != &g_gui) ECX = guest::addr(s);  // (the original leaves the saved block in ECX too)
    ret_ptr(c, s);
}
REWRITE(488e00, "gui_pop", 0, 0, RET_EAX) { ret(c, gui_pop(ecx<ptr32<GuiState>>(c))); }
REWRITE(489860, "gui_redraw_all", 0, 0, RET_NONE) { gui_redraw_all(); }
REWRITE(4890e0, "gui_set_focus", 0, 0, RET_NONE) { gui_set_focus(static_cast<int>(ECX)); }
REWRITE(48a730, "gui_escape_up", 0, 0, RET_NONE) { gui_escape_up(); }
REWRITE(4898f0, "gui_mouse_motion", 1, 0, RET_NONE) {
    gui_mouse_motion(static_cast<int>(ECX), static_cast<int>(EDX), (arg(c, 0) & 0xff) != 0);
}
REWRITE(489be0, "gui_mouse_down", 0, 0, RET_NONE) { gui_mouse_down(static_cast<int>(ECX), static_cast<int>(EDX)); }
REWRITE(48a1e0, "gui_mouse_up", 0, 0, RET_NONE) { gui_mouse_up(static_cast<int>(ECX), static_cast<int>(EDX)); }
REWRITE(48a300, "gui_wheel", 1, 0, RET_NONE) { gui_wheel(static_cast<int16_t>(arg(c, 0))); }
REWRITE(48a950, "gui_key_char", 0, 0, RET_NONE) { gui_key_char(static_cast<int>(ECX)); }
REWRITE(48a3d0, "gui_enter_down", 0, 0, RET_NONE) { gui_enter_down(); }
REWRITE(48a510, "gui_enter_up", 0, 0, RET_NONE) { gui_enter_up(); }
REWRITE(48a600, "gui_escape_down", 0, 0, RET_NONE) { gui_escape_down(); }
REWRITE(48a790, "gui_focus_step", 0, 0, RET_NONE) { gui_focus_step(static_cast<int>(ECX)); }

REWRITE(489150, "button_init", 4, 0, RET_NONE) {
    button_init(ecx<Widget>(c), edx<MsString>(c), xmm(c, 2), xmm(c, 3), argf(c, 0), argf(c, 1), arg(c, 2), arg(c, 3));
}
REWRITE(4891e0, "text_init", 5, 0, RET_NONE) {
    text_init(ecx<Widget>(c), edx<MsString>(c), xmm(c, 2), xmm(c, 3), argf(c, 0), argf(c, 1), argi(c, 2), arg(c, 4));
}
REWRITE(4892d0, "slider_init", 7, 0, RET_NONE) {
    slider_init(ecx<Widget>(c), edx<MsString>(c), xmm(c, 2), xmm(c, 3), argf(c, 0), argf(c, 1), argi(c, 2),
                argi(c, 3), argi(c, 4), arg(c, 5), arg(c, 6));
}
REWRITE(489380, "list_init", 5, 0, RET_NONE) {
    list_init(ecx<Widget>(c), EDX, xmm(c, 1), xmm(c, 2), xmm(c, 3), argf(c, 0), argf(c, 1), argf(c, 2), argf(c, 3),
              arg(c, 4));
}

REWRITE(488a60, "list_item_init", 1, 4, RET_EAX) {
    ret_ptr(c, list_item_init(ecx<ListItem>(c), argp<MsString>(c, 0)));
}
REWRITE(489090, "list_item_delete", 1, 4, RET_EAX) {
    uint32_t self = ECX;
    list_item_delete(ecx<ListItem>(c));
    ret(c, self);
}
REWRITE(4894d0, "list_add", 0, 0, RET_EAX) { ret(c, list_add(ecx<Widget>(c), edx<MsString>(c))); }
REWRITE(4896d0, "list_remove", 0, 0, RET_EAX) { ret(c, list_remove(ecx<Widget>(c), static_cast<int>(EDX))); }
REWRITE(489770, "list_clear", 0, 0, RET_NONE) { list_clear(ecx<Widget>(c)); }
REWRITE(4897f0, "list_find", 0, 0, RET_EAX) { ret(c, list_find(ecx<ListData>(c), edx<MsString>(c))); }

REWRITE(48af30, "key_name", 0, 0, RET_NONE) { key_name(ECX, edx<MsString>(c)); }

REWRITE(473930, "text_field_draw", 1, 4, RET_NONE) { text_field_draw(argp<Widget>(c, 0)); }
REWRITE(473b00, "slider_draw", 1, 4, RET_NONE) { slider_draw(argp<Widget>(c, 0)); }
REWRITE(473ee0, "list_draw", 1, 4, RET_NONE) { list_draw(argp<Widget>(c, 0)); }
REWRITE(4895d0, "list_insert", 1, 0, RET_EAX) { ret(c, list_insert(ecx<Widget>(c), edx<MsString>(c), arg(c, 0))); }
REWRITE(488ef0, "widgets_free", 0, 0, RET_EAX) { ret(c, widgets_free(ecx<ptr32<Widget>>(c), static_cast<int>(EDX))); }
REWRITE(48c960, "on_off_clicked", 1, 4, RET_NONE) { on_off_clicked(argp<Widget>(c, 0)); }
REWRITE(48c9d0, "length_unit_clicked", 1, 4, RET_NONE) { length_unit_clicked(argp<Widget>(c, 0)); }
REWRITE(48ca70, "start_order_clicked", 1, 4, RET_NONE) { start_order_clicked(argp<Widget>(c, 0)); }
REWRITE(48d360, "driver_type_clicked", 1, 4, RET_NONE) { driver_type_clicked(argp<Widget>(c, 0)); }
REWRITE(47cef0, "widget_construct", 0, 0, RET_EAX) { ret(c, guest::addr(widget_construct(ecx<Widget>(c)))); }
REWRITE(47d280, "widgets_to_screen", 0, 0, RET_NONE) { widgets_to_screen(ecx<Widget>(c), static_cast<int>(EDX), xmm(c, 2), xmm(c, 3)); }
REWRITE(47d330, "widgets_to_layout", 0, 0, RET_NONE) { widgets_to_layout(ecx<Widget>(c), static_cast<int>(EDX), xmm(c, 2), xmm(c, 3)); }
REWRITE(472760, "list_insert_sorted", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(list_insert_sorted(ecx<Widget>(c), edx<MsString>(c)))); }
REWRITE(484120, "message_box", 2, 0, RET_EAX) {
    ret(c, static_cast<uint32_t>(message_box(ECX, argp<MsString>(c, 0), edx<MsString>(c), argi(c, 1))));
}
REWRITE(4853a0, "font_build", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(font_build())); }
