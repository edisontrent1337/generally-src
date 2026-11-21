// Program entry (WinMain) and the main function.
#include "game/main.h"

#include <cstring>

#include "game/file.h"
#include "game/input.h"
#include "game/race_options.h"
#include "game/race_state.h"
#include "game/video.h"
#include "game/driver.h"
#include "game/gui.h"
#include "game/input.h"
#include "game/menu.h"
#include "game/render.h"
#include "game/sound.h"
#include "game/text.h"
#include "orig/sdl.h"

GLOBAL(ptr32<Widget>, g_widgets_4a5124, 0x4a5124);  // a screen's 17 widgets (not named yet)
GLOBAL(uint32_t, g_buffer_4a4cfc, 0x4a4cfc);        // malloc'ed buffer (not named yet)
GLOBAL(uint32_t, g_unknown_4a4db0, 0x4a4db0);
GLOBAL(uint32_t, g_unknown_4a4e00, 0x4a4e00);
GLOBAL(uint32_t, g_unknown_4a4dbc, 0x4a4dbc);

static void free_block(uint32_t &p) {  // free() and forget
    if (!p) return;
    guest::call_cdecl(0x496bd7, p);
    p = 0;
}

static void free_screens() {
    if (g_back) sdl::FreeSurface(g_back);
    if (g_background) sdl::FreeSurface(g_background);
    g_back = SdlSurfaceRef{};
    g_background = SdlSurfaceRef{};
}

// Frees what the game allocated: with `everything`, also the drivers, the menus' widgets and the audio
// device (at exit or before an error message); without, only what a change of display mode rebuilds
// (cars, font, surfaces, key state, sounds).
void game_free(bool everything) {
    guest::call_import(IMP_SDL_ShowCursor, 1u);
    if (everything) {
        if (g_driver_list) {
            for (int i = 0; i < g_player_count; i++)
                if (g_player_driver[i]) guest::call_cdecl(0x496c1d, g_player_driver[i]);  // operator delete
            for (uint32_t i = 0; i < static_cast<uint32_t>(g_driver_list->items.size()); i++) {
                uint32_t d = g_driver_list->items[static_cast<int>(i)]->data;
                if (d) guest::call_cdecl(0x496c1d, d);
            }
        }
        g_driver_list = nullptr;
        g_track_list = nullptr;
        gui_close();
        static const struct { uint32_t addr; int count; } kWidgetArrays[] = {
            {0x4a4f0c, 0x1e}, {0x4a5124, 0x11}, {0x4a4c80, 0x15}, {0x4a4c8c, 0x18},
            {0x4a5178, 2},    {0x4a4df4, 6},    {0x4a4c70, 5},    {0x4a517c, 3},
        };
        for (auto a : kWidgetArrays) {
            ptr32<Widget> &w = guest::at<ptr32<Widget>>(a.addr);
            if (w) widgets_free(&w, a.count);
        }
    }
    g_car_count = 0;
    free_block(g_disc_widths);
    free_block(guest::at<uint32_t>(guest::addr(&g_cars)));
    free_block(g_font);
    free_screens();
    free_block(guest::at<uint32_t>(guest::addr(&g_keys)));
    if (g_buffer_4a4cfc) guest::call_cdecl(0x496bd7, g_buffer_4a4cfc);
    g_buffer_4a4cfc = 0;
    for (uint32_t &s : g_engine_sound) s = 0;
    g_unknown_4a4db0 = 0;
    g_unknown_4a4e00 = 0;
    g_start_sound = 0;
    g_unknown_4a4dbc = 0;
    g_skid_sound = 0;
    sounds_free();
    if (everything) {
        audio_close();
        sounds_free();
        free_screens();
    }
}

// WinMain: the game's folder is the program path from the command line (quoted or up to the first
// space) up to and including its last backslash (a quoted path also loses that backslash and the quote);
// then runs the game. The scan back to the backslash has no lower bound, as in the original.
uint32_t win_main() {
    g_racing = 0;
    uint32_t cmd = guest::call_import(IMP_GetCommandLineA);
    char *s = &guest::at<char>(cmd);
    int len = static_cast<int>(guest::call_import(IMP_lstrlenA, cmd));
    int end = 1;
    char stop = s[0] == '"' ? '"' : ' ';
    if (len > 1)
        while (s[end] != stop && ++end < len) {}
    while (s[end - 1] != '\\') end--;
    s[end] = 0;
    if (s[0] == '"') {
        s[end - 1] = 0;
        cmd++;
        s++;
    }
    uint32_t n = guest::call_import(IMP_lstrlenA, cmd);
    g_game_dir = ptr32<char>::from_addr(guest::call_cdecl(0x496c14, n + 1));  // malloc
    std::strcpy(g_game_dir.get(), s);
    return static_cast<uint32_t>(game_main());
}

// the original's message strings (title, init failure, no display modes)
static constexpr uint32_t STR_ERROR_TITLE = 0x49d764;
static constexpr uint32_t STR_INIT_FAILED = 0x49d76c;
static constexpr uint32_t STR_NO_RESOLUTIONS = 0x49d794;
static constexpr uint32_t STR_GAME_NAME = 0x49cd88;  // window caption

static void error_box(uint32_t text) {
    game_free(true);
    guest::call_import(IMP_MessageBoxA, 0u, text, STR_ERROR_TITLE, 0u);
}

// The game: seeds rand with the tick count, loads the race options, starts SDL (video, audio, joystick),
// sets the window caption, builds all screens (0x478d40), and runs the event loop until it reports the
// end (sleeping 1 ms per round while a race runs, 25 ms otherwise). On exit everything is freed, the race
// options are saved and SDL is shut down. Returns 0, or -1 if the game could not start.
int game_main() {
    enter_game_dir();
    guest::call_import(IMP_srand, guest::call_import(IMP_SDL_GetTicks));
    race_options_load();
    if (static_cast<int>(guest::call_import(IMP_SDL_Init, 0x230u)) == -1) return -1;
    uint32_t caption = guest::call_import(IMP_malloc, 15u);  // never freed
    std::strcpy(&guest::at<char>(caption), &guest::at<char>(STR_GAME_NAME));
    guest::call_import(IMP_SDL_WM_SetCaption, caption, 0u);
    if (static_cast<int>(guest::call_regs(0x478d40, 0, 0)) < 0) {  // game_init (not rewritten yet)
        error_box(STR_INIT_FAILED);
        return -1;
    }
    guest::Local<MsVector<DisplayMode>> modes;
    display_modes(modes.get());
    int result;
    if (modes->size() == 0) {
        error_box(STR_NO_RESOLUTIONS);
        result = -1;
    } else {
        joysticks_open();
        while (events_poll(0)) guest::call_import(IMP_SDL_Delay, g_racing ? 1u : 25u);
        game_free(true);
        race_options_save();
        if (g_game_dir) guest::call_cdecl(0x496bd7, g_game_dir);  // free
        guest::call_import(IMP_SDL_Quit);
        result = 0;
    }
    if (modes->first)
        guest::call_stdcall(0x401ed0, modes->first, (modes->end.a - modes->first.a) / static_cast<uint32_t>(sizeof(DisplayMode)));
    return result;
}
