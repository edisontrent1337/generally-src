// The Driver options screen: pick a driver (dropdown list), set its two car colours (RGB sliders) and
// either its controls (human drivers: a key or joystick input per control) or its skill (AI drivers).
#pragma once
#include "game/driver.h"
#include "game/gui.h"
#include "game/lang.h"

GLOBAL(ptr32<Widget>, g_dopt_widgets, 0x4a4c8c);

// widget indices
enum {
    DOPT_CONTROL = 4,    // 4..7: button showing the input bound to each control (Accelerate..Steer right)
    DOPT_DRIVERS = 8,    // dropdown list of drivers
    DOPT_NAME = 9,       // button showing the selected driver's name (opens the dropdown)
    DOPT_COLOR = 10,     // 10..12 RGB of colour 1, 13..15 RGB of colour 2 (sliders 0..255)
    DOPT_AI_SKILL = 16,  // slider, AI drivers only
    DOPT_EMPTY = 17,     // shown instead of everything else when there are no drivers
    DOPT_COUNT = 20,
    // the "Create driver" dialog
    DOPT_NEW_NAME = 20,  // text field
    DOPT_NEW_KIND = 21,  // toggle: id 2 human, otherwise computer
    DOPT_DIALOG_END = 24
};


void joy_binding_name(const JoyBinding *b, MsString *out);
void dopt_close_dropdown();
void dopt_name_clicked(Widget *w);
void dopt_show_controls(bool human);
void dopt_update_colors();
void dopt_driver_selected(Widget *list);
void dopt_ai_skill_changed(Widget *slider);
void dopt_bind_control(int control, Widget *button);
void dopt_delete_driver_clicked(Widget *button);
void dopt_create_driver_clicked(Widget *button);
void dopt_create_driver_done(Widget *button);
void dopt_open();
void dopt_draw_page();
int wait_for_control(const MsString *name, JoyBinding *joy);
