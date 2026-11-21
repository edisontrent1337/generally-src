// Entry adapters: keyboard and joysticks.
#include "bridge/guest.h"
#include "game/input.h"

using namespace abi;

REWRITE(470e10, "keyboard_snapshot", 0, 0, RET_AL) { ret(c, keyboard_snapshot()); }
REWRITE(471100, "joysticks_open", 0, 0, RET_NONE) { joysticks_open(); }
REWRITE(4713c0, "joysticks", 1, 4, RET_EAX) { ret(c, guest::addr(joysticks())); }
REWRITE(471ae0, "events_poll", 0, 0, RET_AL) { ret(c, events_poll(ECX)); }
REWRITE(417070, "joystick", 1, 4, RET_EAX) { ret(c, guest::addr(joystick(arg(c, 0)))); }
REWRITE(407c00, "control_value", 3, 0, RET_EAX) { ret(c, control_value(ECX, arg(c, 0), arg(c, 1), arg(c, 2))); }
REWRITE(4718a0, "key_to_char", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(key_to_char(ecx<SdlKeysym>(c)))); }
REWRITE(471b80, "event_handle", 0, 0, RET_AL) { ret_bool(c, event_handle(ecx<SdlEvent>(c))); }
REWRITE(470e70, "joystick_poll", 0, 0, RET_AL) { ret_bool(c, joystick_poll()); }
