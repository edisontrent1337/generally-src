// Modal dialogs over the current screen: the message box (OK / Yes-No) and the text input box.
#pragma once
#include "game/gui.h"

GLOBAL(ptr32<Widget>, g_msgbox_widgets, 0x4a517c);
GLOBAL(ptr32<char>, g_msgbox_text, 0x4a4f1c);     // copy of the message
GLOBAL(ptr32<char>, g_msgbox_title, 0x4a4c48);    // copy of the title
GLOBAL(uint32_t, g_msgbox_redraw, 0x4a4c94);      // function repainting the screen below
GLOBAL(ptr32<Widget>, g_inputbox_widgets, 0x4a5120);
GLOBAL(ptr32<char>, g_inputbox_text, 0x4a4c78);
GLOBAL(ptr32<char>, g_inputbox_lines, 0x4a4c4c);

void msgbox_button_clicked(Widget *button);
void inputbox_button_clicked(Widget *button);
int message_box(uint32_t redraw, const MsString *title, const MsString *text, int buttons);
uint32_t input_box(const MsString *text, const MsString *title);
