// The in-race pause menu's buttons.
#include "game/lang.h"
#include "game/race_state.h"
#include "game/ghost.h"
#include "orig/menu.h"
#include "orig/race.h"

#include "game/advanced_options.h"
#include "game/menu.h"
#include "game/sound.h"
#include "game/text.h"
#include "game/video.h"
#include "game/gui.h"
#include "orig/sdl.h"

static constexpr uint32_t REDRAW_RACE = 0x480dc0;  // pause_menu_draw, the redraw under message boxes
static constexpr uint32_t PAUSE_SCALE = 0x49dc7c;  // float 1.0 of the original

static void fill(int x, int y, int w, int h, uint32_t color) {
    guest::Local<SdlRect> r;
    r->x = static_cast<int16_t>(x);
    r->y = static_cast<int16_t>(y);
    r->w = static_cast<uint16_t>(w);
    r->h = static_cast<uint16_t>(h);
    sdl::FillRect(g_back, r.get(), color);
}

// The pause menu's box in the middle of the screen (22 x 28 font units, framed), with its title (text
// 87) and a line below the title.
void pause_menu_draw() {
    int f = g_font_size, b = g_menu_border;
    int x0 = g_screen_w / 2 - f * 11, x1 = g_screen_w / 2 + f * 11;
    int y0 = g_screen_h / 2 - f * 15, y1 = g_screen_h / 2 + f * 13;
    if (g_back) {
        fill(x0 - b, y0 - b, x1 - (x0 - b) + b, y1 - (y0 - b) + b, 0xf0);  // frame
        if (g_back) fill(x0, y0, x1 - x0, y1 - y0, 100);
    }
    if (g_back) fill(x0 + f * 2, y0 + f * 4, (x1 - f * 2) - (x0 + f * 2), b, 0x5a);  // line
    SdlSurfaceRef back = g_back;  // (lock_surface, inlined)
    if (sdl::LockSurface(back) == 0) {
        g_pixels = sdl::surface(back)->pixels;
        g_pitch = sdl::surface(back)->pitch;
        g_locked = back;
    } else {
        back = g_locked;
    }
    guest::Local<Box> title;
    title->x1 = g_screen_w / 2 - f * 9;
    title->y1 = g_screen_h / 2 - f * 14;
    title->x2 = g_screen_w / 2 + f * 9;
    title->y2 = g_screen_h / 2 - f * 12 + b;
    draw_text(&g_lang[L_PAUSE], title.get(), 0xf0, TEXT_CENTER_X);
    if (back) {
        g_pitch = 0;
        g_pixels = 0;
        sdl::UnlockSurface(back);
        g_locked = SdlSurfaceRef{};
    }
}

// Escape in a race: silences the engines and the ambient sound, shows the race screen with the cursor and
// opens the pause menu (Replay only once something was recorded). Coming back from a screen opened from
// the pause menu (Options, Save), the saved menu is restored instead.
void pause_menu_open() {
    if (!g_saved_gui) {
        g_racing = 0;
        for (int i = 0; i < g_player_count; i++) sound_stop_handle(g_engine_sound[i]);
        sound_stop_handle(g_skid_sound);
    }
    blit_surface(0, 1);
    pause_menu_draw();
    guest::call_import(IMP_SDL_ShowCursor, 1u);
    Widget &replay = g_pause_widgets.get()[PAUSE_REPLAY];
    if (g_replay_frames < 1)
        replay.flags |= WF_HIDDEN;
    else
        replay.flags &= ~WF_HIDDEN;
    if (g_saved_gui) {
        gui_pop(&g_saved_gui);
        g_saved_gui = nullptr;
        return;
    }
    gui_open(3, g_pause_widgets.get(), 6, guest::at<float>(PAUSE_SCALE), 0, 0, 0x473ee0 /* list_draw */,
             GUI_KEYBOARD | GUI_FOCUS_FIRST);
}

// Closes the pause menu: hides the cursor and restarts the engine sounds (silent, looping) and the
// ambient sound; the game clock restarts.
void pause_menu_close() {
    gui_close();
    guest::call_import(IMP_SDL_ShowCursor, 0u);
    for (int i = 0; i < g_player_count; i++) {
        sound_set_volume(g_engine_sound[i], -10000);
        sound_play_loop(g_engine_sound[i]);
    }
    sound_set_volume(g_skid_sound, -10000);
    sound_play_loop(g_skid_sound);
    g_racing = 1;
}

void pause_resume_clicked() {
    pause_menu_close();
    game_resume();
}

// Replay: frees the pit boxes and plays back the last stretch of the race.
void pause_replay_clicked() {
    for (int i = 0; i < g_pit_count; i++) g_pits.get()[i].state = 6;
    pause_menu_close();
    g_start_light = -1;
    game_resume();
    replay_save_start();
    float frames = static_cast<float>(g_replay_frames);
    float pos = frames - g_replay_step - g_replay_step * 2000.0f;
    g_replay_pos = pos;
    if (0.0f > pos) {
        g_replay_pos = 0;
    } else if (pos >= static_cast<float>(g_replay_frames - 1)) {
        g_replay_pos = frames - 1.0f;
    }
    g_game_state = GS_REPLAY;
}

void pause_end_season_clicked() {
    if (!message_box(REDRAW_RACE, &g_lang[L_END_SEASON], &g_lang[L_QUIT_TO_MENU_Q], 2)) {
        pause_menu_close();
        game_resume();
        return;
    }
    pause_menu_close();
    guest::call_import(IMP_SDL_ShowCursor, 1u);
    if (g_game_state < GS_RESULTS) track_records_save();
    season_results();
}

void pause_end_race_clicked() {
    if (message_box(REDRAW_RACE, &g_lang[L_END_RACE], &g_lang[L_SKIP_RACE_Q], 2)) {
        pause_menu_close();
        race_skip();
    } else {
        pause_menu_close();
        game_resume();
    }
}
