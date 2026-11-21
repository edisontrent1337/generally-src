// Entry adapters: Driver options screen.
#include "bridge/guest.h"
#include "game/driver_options.h"

using namespace abi;

REWRITE(48af80, "joy_binding_name", 0, 0, RET_NONE) { joy_binding_name(ecx<JoyBinding>(c), edx<MsString>(c)); }
REWRITE(48adc0, "dopt_close_dropdown", 0, 0, RET_NONE) { dopt_close_dropdown(); }
REWRITE(48b1c0, "dopt_show_controls", 0, 0, RET_NONE) { dopt_show_controls(ECX != 0); }
REWRITE(48b950, "dopt_update_colors", 1, 4, RET_NONE) { dopt_update_colors(); }
REWRITE(48b5a0, "dopt_driver_selected", 1, 4, RET_NONE) { dopt_driver_selected(argp<Widget>(c, 0)); }
REWRITE(48b4f0, "dopt_ai_skill_changed", 1, 4, RET_NONE) { dopt_ai_skill_changed(argp<Widget>(c, 0)); }
REWRITE(48bb70, "dopt_bind_accelerate", 1, 4, RET_NONE) { dopt_bind_control(CTRL_ACCELERATE, argp<Widget>(c, 0)); }
REWRITE(48bc30, "dopt_bind_brake", 1, 4, RET_NONE) { dopt_bind_control(CTRL_BRAKE, argp<Widget>(c, 0)); }
REWRITE(48bd00, "dopt_bind_left", 1, 4, RET_NONE) { dopt_bind_control(CTRL_LEFT, argp<Widget>(c, 0)); }
REWRITE(48bdd0, "dopt_bind_right", 1, 4, RET_NONE) { dopt_bind_control(CTRL_RIGHT, argp<Widget>(c, 0)); }
REWRITE(48b530, "dopt_name_clicked", 1, 4, RET_NONE) { dopt_name_clicked(argp<Widget>(c, 0)); }
REWRITE(48d3d0, "dopt_delete_driver_clicked", 1, 4, RET_NONE) { dopt_delete_driver_clicked(argp<Widget>(c, 0)); }
REWRITE(48cb10, "dopt_create_driver_clicked", 1, 4, RET_NONE) { dopt_create_driver_clicked(argp<Widget>(c, 0)); }
REWRITE(48cd20, "dopt_create_driver_done", 1, 4, RET_NONE) { dopt_create_driver_done(argp<Widget>(c, 0)); }
REWRITE(481910, "dopt_open", 1, 4, RET_NONE) { dopt_open(); }
REWRITE(47e440, "dopt_draw_page", 0, 0, RET_NONE) { dopt_draw_page(); }
REWRITE(483bc0, "wait_for_control", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(wait_for_control(ecx<MsString>(c), &guest::at<JoyBinding>(EDX)))); }
