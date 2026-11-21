#include "game/input.h"

#include <cstring>

#include "game/gui.h"
#include "game/race_state.h"
#include "game/video.h"
#include "game/menu.h"
#include "orig/input.h"

// Copies SDL's key state; Num Lock, Caps Lock and Scroll Lock (keys 300..302) are cleared. Returns false.
bool keyboard_snapshot() {
    uint8_t *dst = g_keys.get();
    guest::Local<int32_t> n;
    *n = 0;
    guest::call_import(IMP_SDL_PumpEvents);
    uint32_t src = guest::call_import(IMP_SDL_GetKeyState, guest::addr(n.get()));
    if (src && *n > 0) {
        if (*n > 0x200) *n = 0x200;
        guest::call_cdecl(0x498168, guest::addr(dst), src, static_cast<uint32_t>(*n));  // memcpy
        dst[300] = dst[301] = 0;
        dst[302] = 0;
    }
    return false;
}

// Opens every joystick not open yet and lists it (the list is rebuilt).
void joysticks_open() {
    MsVector<JoyInfo> &v = g_joysticks;
    v.last = v.first;
    guest::Local<JoyInfo> info;
    for (int i = 0; i < static_cast<int>(guest::call_import(IMP_SDL_NumJoysticks)); i++) {
        if (guest::call_import(IMP_SDL_JoystickOpened, static_cast<uint32_t>(i))) continue;
        uint32_t j = guest::call_import(IMP_SDL_JoystickOpen, static_cast<uint32_t>(i));
        info->handle = j;
        if (!j) continue;
        info->axes = static_cast<int32_t>(guest::call_import(IMP_SDL_JoystickNumAxes, j));
        info->buttons = static_cast<int32_t>(guest::call_import(IMP_SDL_JoystickNumButtons, j));
        info->hats = static_cast<int32_t>(guest::call_import(IMP_SDL_JoystickNumHats, j));
        // push_back (inlined)
        if (v.last.a == v.end.a) guest::call_regs(0x471210, guest::addr(info.get()), 0, guest::addr(info.get()));
        if (v.last) *v.last.get() = *info;
        v.last.a += sizeof(JoyInfo);
    }
}

MsVector<JoyInfo> *joysticks() { return &g_joysticks; }

// Handles up to max pending SDL events (0: all), then draws a frame: the race when it runs, the menu
// background otherwise. False when an event asked to quit.
bool events_poll(uint32_t max) {
    guest::Local<SdlEvent> ev;
    uint32_t n = 0;
    while (static_cast<int>(guest::call_import(IMP_SDL_PollEvent, ev.get())) > 0) {
        if (!event_handle(ev.get())) return false;
        if (max && ++n >= max) break;
    }
    if (g_racing)
        race_frame();
    else
        show_background();
    return true;
}

// g_joysticks[i] (vector::operator[] with the range check)
JoyInfo *joystick(uint32_t i) {
    if (i >= static_cast<uint32_t>(g_joysticks.size())) guest::call_import(IMP_Xout_of_range, 0x49ccccu);
    return &g_joysticks[static_cast<int>(i)];
}

// How far a control is pressed, 0..100: the key if it is down, else the joystick input. kind = byte 1 of
// flags (0 axis, 1 button, 2 hat), byte 2 = 1 inverts an axis (only its positive half counts).
int control_value(uint32_t key, uint32_t joy, uint32_t input, uint32_t flags) {
    if (key < 0x143 && (g_keys.get()[key] & 1)) return 100;
    if (joy == 0xffffffffu || joy >= static_cast<uint32_t>(g_joysticks.size())) return 0;
    switch ((flags >> 8) & 0xff) {
    case 0: {
        int v = static_cast<int16_t>(guest::call_import(IMP_SDL_JoystickGetAxis, joystick(joy)->handle, input));
        if (((flags >> 16) & 0xff) == 1) v = -v;
        if (v > 0) return static_cast<int>(static_cast<float>(v) / 32767.0f * 100.0f);
        return 0;
    }
    case 1:
        return (guest::call_import(IMP_SDL_JoystickGetButton, joystick(joy)->handle, input) & 0xff) * 100;
    case 2:
        guest::call_import(IMP_SDL_JoystickGetHat, joystick(joy)->handle, input);  // the hat counts as pressed
        return 100;
    }
    return 0;
}

// The character a key press types into a text field, 0 for none. Backspace is handled right here (it
// goes to the focused text field). Keypad digits and operators type their character; with Shift or a
// lock key (mod & 0x2003) letters a..y become upper case ('z' stays lower case, as in the original) and
// the US-layout symbol keys give their shifted symbol.
int key_to_char(const SdlKeysym *k) {
    int sym = k->sym;
    if (sym == 8) {
        gui_key_char(8);
        return 0;
    }
    bool printable = (sym >= 0x20 && sym < 0x7f) || static_cast<uint32_t>(sym - 0x100) <= 0xf;
    if (!printable) return 0;
    if (static_cast<uint32_t>(sym - 0x100) <= 0xf) {  // keypad 0..9 . / * - + (enter falls through)
        static const char kKeypad[15] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '/', '*', '-', '+'};
        if (sym - 0x100 < 15) return kKeypad[sym - 0x100];
    }
    if (!(k->mod & 0x2003)) return sym;
    if (static_cast<uint32_t>(sym - 'a') <= 0x18) return sym - 0x20;
    switch (sym) {
    case '\'': return '"';
    case ',': return '<';
    case '-': return '_';
    case '.': return '>';
    case '/': return '?';
    case '0': return ')';
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case ';': return ':';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '`': return '~';
    case '|': return '\\';
    }
    return sym;
}

GLOBAL(uint8_t, g_mode_toggled, 0x4a4796);   // this event switched fullscreen (Alt+Enter)
GLOBAL(uint8_t, g_fullscreen, 0x4a4d64);

enum : int32_t {
    KEY_ENTER = 0xd, KEY_ESCAPE = 0x1b, KEY_PAD_ENTER = 0x10f, KEY_UP = 0x111, KEY_DOWN = 0x112, KEY_RIGHT = 0x113,
    KEY_LEFT = 0x114, KEY_F1 = 0x11a, KEY_F4 = 0x11d, KEY_F11 = 0x124, KEY_F12 = 0x125, KEY_RALT = 0x133, KEY_LALT = 0x134,
};
enum : uint8_t { EV_KEYDOWN = 2, EV_KEYUP = 3, EV_MOUSEMOTION = 4, EV_MOUSEDOWN = 5, EV_MOUSEUP = 6, EV_QUIT = 0xc };

static bool alt_held() {
    const uint8_t *keys = &guest::at<uint8_t>(guest::call_import(IMP_SDL_GetKeyState, 0u));
    return keys[KEY_LALT] || keys[KEY_RALT];
}

// One SDL event. Global keys first: F12 / F11 screenshot (at screen size / the configured size), Alt+F4 quits, Alt+Enter switches
// between window and fullscreen (if neither mode can be set, quits). Outside races the event then goes to
// the GUI: keys (Escape, Enter, arrows step the focus, characters), F1 on release, the mouse (left button,
// wheel, right button release). Returns false to quit.
bool event_handle(SdlEvent *e) {
    g_mode_toggled = 0;
    const uint8_t *d = e->data - 1;  // event bytes, offsets as in SDL_Event
    auto u16 = [d](int off) { return static_cast<int>(*reinterpret_cast<const uint16_t *>(d + off)); };
    int32_t sym = *reinterpret_cast<const int32_t *>(d + 8);
    if (e->type == EV_KEYDOWN) {
        if (sym == KEY_F12) {
            screenshot_take(0, 0);
        } else if (sym == KEY_F11) {
            screenshot_take(static_cast<uint32_t>(g_screenshot_w), static_cast<uint32_t>(g_screenshot_h));
        } else if (sym == KEY_F4) {
            if (alt_held()) return false;
        } else if ((sym == KEY_ENTER || sym == KEY_PAD_ENTER) && alt_held()) {
            uint32_t flags = guest::at<uint32_t>(guest::call_import(IMP_SDL_GetVideoSurface));
            if (!guest::call_import(IMP_SDL_SetVideoMode, 0u, 0u, 0u, flags ^ 0x80000000u) &&
                !guest::call_import(IMP_SDL_SetVideoMode, 0u, 0u, 0u, flags))
                return false;
            g_fullscreen = static_cast<uint8_t>(1 - g_fullscreen);
            palette_reset();
            g_mode_toggled = 1;
        }
    } else if (e->type == EV_QUIT) {
        return false;
    }
    if (g_racing) return true;
    switch (e->type) {
    case EV_KEYDOWN:
        if (sym == KEY_ESCAPE) gui_escape_down();
        else if ((sym == KEY_ENTER || sym == KEY_PAD_ENTER) && !g_mode_toggled) gui_enter_down();
        else if (sym == KEY_UP || sym == KEY_RIGHT) gui_focus_step(-1);
        else if (sym == KEY_DOWN || sym == KEY_LEFT) gui_focus_step(1);
        else if (int ch = key_to_char(reinterpret_cast<const SdlKeysym *>(d + 4))) gui_key_char(ch);
        break;
    case EV_KEYUP:
        if (sym == KEY_ESCAPE) gui_escape_up();
        else if (sym == KEY_ENTER || sym == KEY_PAD_ENTER) gui_enter_up();
        else if (sym == KEY_F1) key_about(KEY_F1);
        break;
    case EV_MOUSEMOTION:
        gui_mouse_motion(u16(4), u16(6), d[2] & 1);
        break;
    case EV_MOUSEDOWN:
        if (d[2] == 1) gui_mouse_down(u16(4), u16(6));
        else if (d[2] == 4) gui_wheel(120);
        else if (d[2] == 5) gui_wheel(-120);
        break;
    case EV_MOUSEUP:
        if (d[2] == 1) gui_mouse_up(u16(4), u16(6));
        else if (d[2] == 3) main_menu_right_click(u16(4), u16(6));
        break;
    }
    return true;
}

GLOBAL(uint32_t, g_joy_state, 0x4a4cfc);  // JoyState * (0: none)

// The first joystick's state, read every tick (0x470e70) into *g_joy_state (0x50 bytes): up to six axes
// scaled to -100..100 (axis / 32767 * 100, truncated), two words the original leaves uninitialised (its
// stack contents, copied here from the same guest stack slots), the first hat's up / right / down / left
// as 0 or 1, and up to 32 button bytes. Returns whether there is a state to fill.
bool joystick_poll() {
    CPU *c = guest::cpu();
    // the original's frame: ebp = esp - 4, aligned down to 16, minus 0x78; the words at +0x30, +0x34
    const uint32_t frame = ((ESP - 4) & ~0xfu) - 0x78;
    const uint32_t junk0 = guest::at<uint32_t>(frame + 0x30), junk1 = guest::at<uint32_t>(frame + 0x34);
    const uint32_t dest = g_joy_state;
    const uint32_t j = guest::call_import(IMP_SDL_JoystickOpen, 0u);
    int naxes = static_cast<int>(guest::call_import(IMP_SDL_JoystickNumAxes, j));
    if (naxes > 6) naxes = 6;
    int32_t axes[6] = {};
    for (int a = naxes - 1; a >= 0; a--) {
        const int16_t v = static_cast<int16_t>(guest::call_import(IMP_SDL_JoystickGetAxis, j, static_cast<uint32_t>(a)));
        axes[a] = guest::to_int(static_cast<float>(v) / 32767.0f * 100.0f);
    }
    int32_t hat[4] = {};
    if (static_cast<int>(guest::call_import(IMP_SDL_JoystickNumHats, j)) > 0) {
        const uint8_t h = static_cast<uint8_t>(guest::call_import(IMP_SDL_JoystickGetHat, j, 0u));
        for (int i = 0; i < 4; i++) hat[i] = (h & (1 << i)) != 0;
    }
    uint8_t buttons[32] = {};
    if (static_cast<int>(guest::call_import(IMP_SDL_JoystickNumButtons, j)) > 0) {
        for (int i = 0; i < static_cast<int>(guest::call_import(IMP_SDL_JoystickNumButtons, j)); i++) {
            if (i >= 32) break;
            buttons[i] = static_cast<uint8_t>(guest::call_import(IMP_SDL_JoystickGetButton, j, static_cast<uint32_t>(i)));
        }
    }
    if (!dest) return false;
    uint8_t *d = &guest::at<uint8_t>(dest);
    std::memcpy(d, axes, sizeof axes);
    std::memcpy(d + 0x18, &junk0, 4);
    std::memcpy(d + 0x1c, &junk1, 4);
    std::memcpy(d + 0x20, hat, sizeof hat);
    std::memcpy(d + 0x30, buttons, sizeof buttons);
    return true;
}
