// Keyboard state and joysticks.
#pragma once
#include "bridge/msvc.h"

GLOBAL(ptr32<uint8_t>, g_keys, 0x4a4ee4);  // copy of SDL's key state (512 entries)

struct JoyInfo {
    int32_t axes, buttons, hats;
    uint32_t handle;  // SDL_Joystick*
};
static_assert(sizeof(JoyInfo) == 16);
GLOBAL(MsVector<JoyInfo>, g_joysticks, 0x4a5204);  // the joysticks opened

bool keyboard_snapshot();
void joysticks_open();
MsVector<JoyInfo> *joysticks();

struct SdlEvent {  // SDL 1.2 SDL_Event
    uint8_t type;
    uint8_t data[19];
};
static_assert(sizeof(SdlEvent) == 20);
bool events_poll(uint32_t max);
bool event_handle(SdlEvent *e);
JoyInfo *joystick(uint32_t i);
int control_value(uint32_t key, uint32_t joy, uint32_t input, uint32_t flags);

struct SdlKeysym {  // SDL 1.2 SDL_keysym
    uint8_t scancode;
    uint8_t pad[3];
    int32_t sym;
    uint32_t mod;
    uint16_t unicode;
};
int key_to_char(const SdlKeysym *k);
GLOBAL(int32_t, g_screenshot_w, 0x4a4dec);   // F11: screenshot size (gr.ini)
GLOBAL(int32_t, g_screenshot_h, 0x4a4df0);

// SDL 1.2 key codes (SDLKey) used as indices into g_keys
enum SdlKey : int {
    SDLK_RSHIFT = 303,
    SDLK_LSHIFT = 304,
    SDLK_RCTRL = 305,
    SDLK_LCTRL = 306,
};
bool joystick_poll();
