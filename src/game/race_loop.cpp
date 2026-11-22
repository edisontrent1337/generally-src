// The race's main loop step (0x4061c0): fixed-rate ticks driven by SDL_GetTicks, each running the game
// state machine, then one redraw for the ticks run.
#include <cmath>
#include <cstdint>
#include <cstring>

#include "game/car_physics.h"
#include "game/fmath.h"
#include "game/ghost.h"
#include "game/gui.h"
#include "game/input.h"
#include "game/lang.h"
#include "game/menu.h"
#include "game/race_options.h"
#include "game/race_state.h"
#include "game/render.h"
#include "game/replay.h"
#include "game/sound.h"
#include "game/selected_tracks.h"
#include "game/text.h"
#include "game/track.h"
#include "game/video.h"
#include "bridge/imports.h"

GLOBAL(uint8_t, g_catch_up, 0x4a4de8);        // nonzero: run every tick owed (else drop all but the remainder)
GLOBAL(int32_t, g_firework_wait, 0x4ab710);  // ticks to the next firework (game state 8 / 9)
GLOBAL(uint32_t, g_draw_view, 0x4a4d20);     // function that draws the track view (by display mode)

namespace {
constexpr int KEY_ESCAPE = 0x1b, KEY_F1 = 0x11a;  // SDL 1.2 key numbers

// Any key held, except F11 and F12 (SDL 1.2 keys 292, 293).
bool any_key(const uint8_t *keys) {
    for (int k = 1; k < 0x143; k++)
        if (k != 0x124 && k != 0x125 && (keys[k] & 1)) return true;
    return false;
}

int64_t time_now64() {
    uint32_t lo = guest::call_cdecl(guest::at<uint32_t>(IMP__time64), 0u);
    CPU *c = guest::cpu();
    return static_cast<int64_t>((static_cast<uint64_t>(EDX) << 32) | lo);
}

uint32_t ticks() { return guest::call_import(IMP_SDL_GetTicks); }

// The volume of a looping effect from its strength: -10000 - trunc(log10(option * min(v, 1e8) + 1) * -1000).
int effect_volume(float v, uint8_t option) {
    if (v > 100000000.0f) v = 100000000.0f;
    return -10000 - guest::to_int(guest_log10f(static_cast<float>(option) * v + 1.0f) * -1000.0f);
}

// A firework of the season end (states 8 and 9): an impact record at a random point of the track,
// colour 9 - trunc(r * -5), lifetime 100, rising at 30; the next comes after r * 700000 / size^2 ticks.
void firework() {
    const float size = g_track_size;
    const float *r = &guest::at<float>(g_random_table);
    uint8_t i = g_random_index;
    const uint16_t idx = g_unknown_4a4794;
    uint8_t *rec = &guest::at<uint8_t>(g_unknown_4a4774 + 0x1cu * idx);
    rec[0] = static_cast<uint8_t>(9 - static_cast<uint8_t>(guest::to_int(r[static_cast<uint8_t>(i + 1)] * -5.0f)));
    rec[1] = 100;
    float *at = reinterpret_cast<float *>(rec + 4);
    at[0] = r[static_cast<uint8_t>(i + 2)] * size;
    at[1] = terrain_height(Vec3(at[0], at[1], at[2]));
    at[2] = r[static_cast<uint8_t>(i + 3)] * size;
    float *vel = reinterpret_cast<float *>(rec + 0x10);
    vel[0] = 0.0f;
    vel[1] = 30.0f;
    vel[2] = 0.0f;
    g_unknown_4a4794 = static_cast<int>(idx) + 1 < static_cast<int>(g_race_options.w1e) ? static_cast<uint16_t>(idx + 1) : 0;
    g_random_index = static_cast<uint8_t>(i + 4);
    g_firework_wait = guest::to_int(r[g_random_index] * 700000.0f / (size * size));
}

// One tick of the racing states (2: racing, 3: all finished, counting down to the results). The time
// limit clock starts on the first tick and moves on by any pause; the start lights go out at substate
// 70; the race clock (g_unknown_4a47f8) runs until 6000 ticks after the winner's last crossing. Then
// the cars move (ghost recording with one player, replay recording, each car's step, every pair's
// collision) and the skid and crash sounds follow this tick's amounts.
void race_tick() {
    if (g_race_clock_start < 0 || g_race_clock_end < 0) {
        g_race_clock_start = static_cast<int32_t>(time_now64());
        g_race_clock_end = g_race_clock_start + g_time_limit;
    }
    if (g_pause_clock > 0) {  // back from the pause menu: the clock end moves on by the pause
        g_race_clock_end = g_race_clock_end + (static_cast<int32_t>(time_now64()) - g_pause_clock);
        g_pause_clock = -1;
    }
    if (g_game_state == 2) {
        if (g_race_substate == 70) {
            g_start_light = -1;
            g_race_substate = static_cast<uint8_t>(g_race_substate + 1);
        } else if (g_race_substate <= 70) {
            g_race_substate = static_cast<uint8_t>(g_race_substate + 1);
        }
        // the race ends 6000 ticks after the leader's last crossing once the leader has finished
        if (g_leader_laps < g_race_laps ||
            g_unknown_4a47f8 - *reinterpret_cast<int32_t *>(g_car_states[g_finish_order[0]].raw + 0x10) < 6000) {
            g_unknown_4a47f8 = g_unknown_4a47f8 + 1;
        } else {
            g_game_state = 3;
            g_race_substate = 0;
        }
    } else {
        g_race_substate = static_cast<uint8_t>(g_race_substate + 1);
        if (g_race_substate >= 200) g_game_state = 4;
    }
    lock_surface(2);
    if (g_player_count == 1) {
        uint8_t *c = g_car_states[0].raw;
        ghost_record(reinterpret_cast<Vec3 *>(c + 0x58), reinterpret_cast<Vec3 *>(c + 0x7c),
                     reinterpret_cast<Vec3 *>(c + 0x88), reinterpret_cast<Vec3 *>(c + 0x94));
    }
    replay_record();
    for (int i = 0; i < g_player_count; i++) car_step(i);
    for (int i = 0; i < g_player_count - 1; i++)
        for (int j = i + 1; j < g_player_count; j++) car_car_collide(i, j);
    pit_crews_step();

    sound_set_volume(g_skid_sound, effect_volume(g_skid_amount, g_race_options.b21));
    g_skid_amount = 0.0f;
    if (g_impact_energy > 1.0f) {
        sound_set_volume(g_damage_sound, effect_volume(g_impact_energy, g_race_options.b22));
        g_impact_energy = 0.0f;
        sound_play(g_damage_sound);
    }
    unlock_surface();
}

// The timed race's clock "mm:ss" (empty: a single character string at 0x49cc4c) above the lap counter.
void draw_race_clock() {
    const int rem = g_race_clock_end - static_cast<int32_t>(time_now64());
    const int minutes = guest::to_int(static_cast<float>(std::floor(static_cast<double>(static_cast<float>(rem) / 60.0f))));
    const int seconds = rem % 60;
    guest::Local<MsString> t;
    t->init();
    t->assign(&guest::at<char>(0x49cc4c), 1);
    if (rem > 0) {
        uint32_t s = guest::call_cdecl(0x46f320, 0x49cc50u, minutes, seconds);  // "%02d:%02d"
        t->assign(&guest::at<char>(s), static_cast<uint32_t>(std::strlen(&guest::at<char>(s))));
    }
    const int u = g_font_size;
    const int y = static_cast<int>(static_cast<double>(g_screen_h - g_menu_border * 2) - static_cast<double>(u) * 4.2);
    const float lenf = static_cast<float>(static_cast<double>(t->size));
    const int x = guest::to_int(static_cast<float>(g_screen_w) * 0.25f - (lenf + 0.5f) * static_cast<float>(u));
    draw_text_at(t.get(), x, y, 0xf2, 0xf1);
    t->destroy();
}

void fill(int x, int y, int w, int h, uint32_t colour) {
    if (!g_back) return;
    guest::Local<SdlRect> r;
    r->x = static_cast<int16_t>(x);
    r->y = static_cast<int16_t>(y);
    r->w = static_cast<uint16_t>(w);
    r->h = static_cast<uint16_t>(h);
    sdl::FillRect(g_back, r.get(), colour);
}

// The replay viewer's bottom bar: the panel, the position bar (with the marked range from the first
// mark), the help line texts, speed and time; F1 held shows the key help in the top left.
void draw_replay_hud() {
    const int u = g_font_size, b = g_menu_border, h = g_screen_h, w = g_screen_w;
    const int y1 = h - b * 5 - u * 4;
    fill(u * 20, y1, w - u * 20, h - y1, 0xf0);
    const int y2 = h - u * 4 - b, x2 = u * 21 - b;
    fill(x2, y2, (w - u * 10 - x2) + b, (h - u * 3) - y2, 0xf1);
    const float span = static_cast<float>(w - u * 31);
    const float pos = g_replay_frames == 0 ? 0.0f : g_replay_pos / static_cast<float>(g_replay_frames - 1);
    const int px = guest::to_int(pos * span + 0.5f);
    float m = -1.0f;
    if (!(0.0f > g_replay_mark_in) && g_replay_frames != 0) m = g_replay_mark_in / static_cast<float>(g_replay_frames - 1);
    const int mx = guest::to_int(m * span + 0.5f);
    if (mx > 0 && mx < px) {
        fill_rect(0xfa, static_cast<int16_t>(u * 21), static_cast<int16_t>(h - u * 4), static_cast<uint16_t>(mx),
                  static_cast<uint16_t>(u - b));
        fill_rect_corners(0xfc, static_cast<int16_t>(u * 21 + mx), static_cast<int16_t>(h - u * 4),
                          static_cast<int16_t>(px + u * 21), static_cast<int16_t>(h - u * 3 - b));
    } else {
        fill(u * 21, h - u * 4, px, u - b, 0xfa);
    }
    lock_surface(1);
    guest::call_regs(g_draw_view, 0, 0);
    const int ty = h - (u + b) * 2;
    draw_text_at(&g_lang[152], u * 22, ty, 0xf2, 0xf1);
    draw_text_at(&g_lang[153], w - (static_cast<int>(g_lang[153].size) + 9) * u, ty, 0xf2, 0xf1);
    draw_text_at(&g_replay_speed_text, w - u * 8, ty, 0xf2, 0xf1);
    draw_text_at(&g_replay_clock_text, w - u * 9, -b * 3 - u * 4 + h, 0xf2, 0xf1);
    if (g_keys.get()[KEY_F1] & 1) {
        // key names (guest strings) and their lang texts 154..161, two rows apart
        static constexpr uint32_t kKeys[8] = {0x49cc5c, 0x49cc64, 0x49cc6c, 0x49cc74, 0x49cc7c, 0x49cc84, 0x49cc8c, 0x49cc48};
        for (int k = 0; k < 8; k++) {
            guest::Local<MsString> name;
            name->construct(kKeys[k]);
            draw_text_at(name.get(), u * 2, u * 2 * (k + 1), 0xf5, 1);
            name->destroy();
            draw_text_at(&g_lang[154 + k], u * 12, u * 2 * (k + 1), 0xf2, 0xf1);
        }
    }
    unlock_surface();
    frame_present();
}
}  // namespace

void race_loop_step() {
    uint32_t delta = ticks() - g_resume_ticks;
    if (delta > 200) delta = 200;
    g_unknown_4a4788 = static_cast<int32_t>(delta);
    if (!g_catch_up && delta >= g_unknown_4a4790) g_resume_ticks = ticks() - delta % g_unknown_4a4790;
    if (static_cast<uint32_t>(g_unknown_4a4788) < g_unknown_4a4790) return;

    int frames = 0;  // ticks that need a redraw
    do {
        g_unknown_4a4788 = g_unknown_4a4788 - g_unknown_4a4790;
        if (g_catch_up) g_resume_ticks = ticks() - static_cast<uint32_t>(g_unknown_4a4788);
        keyboard_snapshot();
        joystick_poll();
        const uint8_t state = g_game_state;
        const uint8_t *keys = g_keys.get();
        if (state < 8 && (keys[KEY_ESCAPE] & 1)) {
            g_pause_clock = static_cast<int32_t>(time_now64());
            pause_menu_open();
            return;
        }
        switch (state) {
        case 0:  // waiting: any key starts the countdown
            if (any_key(keys)) {
                g_race_substate = 0;
                g_game_state = 1;
            }
            break;
        case 1:  // the start lights; holding a control before the start counts up the car's false start
            if (g_unknown_4a47f8 == 0) {
                for (int i = 0; i < g_player_count; i++) {
                    uint8_t *c = g_car_states[i].raw;
                    uint32_t key = *reinterpret_cast<uint32_t *>(c + 0x48);
                    if (key && control_value(key, *reinterpret_cast<uint32_t *>(c + 0x18), *reinterpret_cast<uint32_t *>(c + 0x1c),
                                             *reinterpret_cast<uint32_t *>(c + 0x20)))
                        c[1]++;
                    else
                        c[1] = 0;
                }
            }
            if (g_race_substate % 70 == 0) start_light_step(static_cast<uint8_t>(g_race_substate / 70));
            g_race_substate = static_cast<uint8_t>(g_race_substate + 1);
            if (g_race_substate > 210) {
                g_race_substate = 0;
                g_game_state = 2;
            }
            break;
        case 2:
        case 3:
            frames++;
            race_tick();
            break;
        case 4: {  // the race is over: engines and skids silent, results
            g_game_state = 5;
            for (int i = 0; i < g_player_count; i++) sound_set_attenuation(g_engine_sound[i], 5.0f);
            sound_set_attenuation(g_skid_sound, 5.0f);
            if (g_player_count != 1) race_record_results();
            track_records_save();
            race_results_draw();
            return;
        }
        case 5:  // results: a key goes on to the standings or (last track) the season end
            if (any_key(keys)) {
                if (g_current_track + 1 < g_track_count) season_standings();
                else season_end();
            }
            return;
        case 6:  // waiting for all keys up
            if (!any_key(keys)) g_game_state = 7;
            return;
        case 7:  // standings: a key loads the next track
            if (any_key(keys)) {
                g_current_track = g_current_track + 1;
                race_load_track();
            }
            return;
        case 8:  // season end, keys still held from before
        case 9:  // season end: a key leaves
            if (state == 8) {
                if (!any_key(keys)) g_game_state = 9;
            } else if (any_key(keys)) {
                race_free();
                return;
            }
            frames++;
            if (g_firework_wait < 1) firework();
            else g_firework_wait = g_firework_wait - 1;
            break;
        case 10:  // the replay viewer
            if (!replay_controls()) return;
            frames = 1;
            break;
        case 11:  // leaving the replay: once Escape is up, back to the race as it was
            if (!(keys[KEY_ESCAPE] & 1)) {
                g_replay_mark_out = -1.0f;
                g_replay_mark_in = -1.0f;
                replay_restore_start();
                game_resume();
            }
            return;
        default:
            break;
        }
    } while (static_cast<uint32_t>(g_unknown_4a4788) >= g_unknown_4a4790);
    if (frames <= 0) return;

    blit_surface(2, 1);
    const uint8_t state = g_game_state;
    if (state < 8) {
        lock_surface(SURF_BACK);
        guest::call_regs(g_draw_view, 0, 0);
        guest::call_regs(0x419840, static_cast<uint32_t>(frames), 0);  // cars, effects (not rewritten yet)
        for (int i = 0; i < g_player_count; i++) hud_draw_gauges(i);
        if (g_timed_race && !g_unknown_4a4792) draw_race_clock();
        unlock_surface();
        frame_present();
    } else if (state == 10) {
        draw_replay_hud();
    } else {
        lock_surface(1);
        guest::call_regs(0x419840, static_cast<uint32_t>(frames), 0);
        unlock_surface();
        frame_present();
    }
}
