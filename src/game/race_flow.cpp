// Moving between the phases of a race and of a season.
#include "game/menu.h"
#include "game/driver.h"
#include "game/race_options.h"
#include "game/race_state.h"
#include "game/render.h"
#include "game/replay.h"
#include "game/ghost.h"
#include "game/objects.h"
#include "game/track.h"
#include "orig/render.h"
#include "game/selected_tracks.h"
#include "game/sound.h"
#include "game/lang.h"
#include "game/text.h"
#include "game/video.h"
#include "orig/race.h"

#include <cstring>

#include "game/fmath.h"
#include "game/gui.h"
#include "bridge/imports.h"

GLOBAL(uint32_t, g_draw_track, 0x4a4d20);           // function that draws the track view (by display mode)

// Draws the race view and every car's gauges into the locked back surface (left locked). While racing, each
// player's engine sound follows the car: volume from g_unknown_4a47f4, playback rate from the car's speed
// (|speed| * 4 + 50, divided by a model value, scaled by 10000, or 5000 below g_water_level, / 22050).
void race_draw_scene() {
    blit_surface(2, 1);
    SdlSurfaceRef back = g_back;  // (lock_surface, inlined)
    if (sdl::LockSurface(back) == 0) {
        g_pixels = sdl::surface(back)->pixels;
        g_pitch = sdl::surface(back)->pitch;
        g_locked = back;
    }
    guest::call_regs(g_draw_track, 0, 0);
    guest::call_regs(0x419840, 0, 0);
    for (int p = 0; p < g_player_count; p++) {
        hud_draw_gauges(p);
        if (!g_racing) continue;
        const uint8_t *car = g_car_states[p].raw;
        float speed = *reinterpret_cast<const float *>(car + 0xa0);
        float pitch = speed > 0.0f ? speed * 4.0f + 50.0f : 50.0f - speed * 4.0f;
        pitch = pitch / *reinterpret_cast<const float *>(&g_race_models[p][0xa8]);
        uint32_t sound = g_engine_sound[p];
        if (sound == 0) continue;
        sound_set_volume(sound, -10000 - guest::to_int(g_unknown_4a47f4 * -0.93f));
        float scale = g_water_level > *reinterpret_cast<const float *>(car + 0x5c) ? 5000.0f : 10000.0f;
        if (sound <= static_cast<uint32_t>(g_sounds.size())) {
            if (SoundRef *s = g_sounds[static_cast<int>(sound) - 1].get())
                s->rate = static_cast<float>(guest::to_int(pitch * scale)) / 22050.0f;
        }
    }
}

static void unlock_and_present() {
    if (g_locked) {
        g_pitch = 0;
        g_pixels = 0;
        sdl::UnlockSurface(g_locked);
        g_locked = SdlSurfaceRef{};
    }
    frame_present();
}

// Draws one race frame and shows it (frame_present).
void race_render() {
    race_draw_scene();
    unlock_and_present();
}

// For each palette entry (stored as bytes b, g, r, -) the colour of the dark grey ramp (1, 2, ...) of
// its brightness: 1 - trunc((0.6 g + 0.3 r + 0.1 b) * -0.07). The backdrop of in-race boxes.
static void grey_table(const uint8_t *palette, uint8_t grey[256]) {
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = palette + i * 4;
        float l = static_cast<float>(c[1]) * 0.6f + static_cast<float>(c[2]) * 0.3f;
        l += static_cast<float>(static_cast<double>(c[0])) * 0.1f;
        grey[i] = static_cast<uint8_t>(1 - guest::to_int(l * -0.07f));
    }
}

// The race frame as the backdrop of an in-race box (info, results): redrawn without sounds, and the box
// area (44 units wide, centred; rows from (h - 5b - 38u) / 2 to (h + 5(6u - b)) / 2) turned into a dark
// grey ramp: colour 1 - trunc((0.6 g + 0.3 b + 0.1 r) * -0.07) of each pixel's palette entry.
void race_render_dimmed() {
    blit_surface(2, 1);
    SdlSurfaceRef back = g_back;  // (lock_surface, inlined)
    if (sdl::LockSurface(back) == 0) {
        g_pixels = sdl::surface(back)->pixels;
        g_pitch = sdl::surface(back)->pitch;
        g_locked = back;
    }
    guest::call_regs(g_draw_track, 0, 0);
    guest::call_regs(0x419840, 0, 0);
    for (int p = 0; p < g_player_count; p++) hud_draw_gauges(p);
    guest::Local<uint8_t[0x400]> palette;  // SDL_Color[256]: r, g, b, unused
    palette_store(reinterpret_cast<uint32_t *>(*palette));
    int u = g_font_size, b = g_menu_border;
    int pitch = static_cast<int>(g_pitch);
    int cx = g_screen_w / 2;
    int end = (g_screen_h + (u * 6 - b) * 5) / 2 * pitch;
    uint8_t grey[256];
    grey_table(*palette, grey);
    for (int row = (g_screen_h - b * 5 - u * 38) / 2 * pitch; row < end; row += pitch) {
        uint8_t *line = &guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(row));
        for (int x = cx - u * 22; x < cx + u * 22; x++) line[x] = grey[line[x]];
    }
    if (g_locked) {
        g_pitch = 0;
        g_pixels = 0;
        sdl::UnlockSurface(g_locked);
        g_locked = SdlSurfaceRef{};
    }
}

static void free_global(uint32_t addr) {
    uint32_t &p = guest::at<uint32_t>(addr);
    if (p) {
        guest::call_cdecl(0x496bd7, p);  // free
        p = 0;
    }
}

// Leaves the race: frees the ghost buffers and the replay, stops the engine and ambient sounds, frees the
// track's data (objects, pits, height map, shading tables, images, depth buffer, ...), shows the main
// menu and resets the display mode.
void race_free() {
    if (g_ghost_buf_a) guest::call_cdecl(0x496bd7, g_ghost_buf_a);
    g_ghost_buf_a = nullptr;
    if (g_ghost_buf_b) guest::call_cdecl(0x496bd7, g_ghost_buf_b);
    g_ghost_buf_b = nullptr;
    replay_reset();
    g_racing = 0;
    for (uint32_t s : g_engine_sound) sound_stop_handle(s);
    sound_stop_handle(g_skid_sound);
    static const uint32_t kTrackData[] = {
        0x4a47ec, 0x4a473c, 0x4a477c, 0x4a47e8, 0x4a4780 /* pits */, 0x4a4744 /* objects */,
        0x4a4748 /* shade light */, 0x4a478c /* shade dark */, 0x4a47a0 /* height map */, 0x4a4764,
        0x4a476c /* track image */, 0x4a4784 /* depth buffer */, 0x4a47e4, 0x4a4774,
    };
    for (uint32_t a : kTrackData) free_global(a);
    main_menu_show();
    guest::call_import(IMP_SDL_ShowCursor, 1u);
    palette_reset();
}


// Player p's three pedal gauges in the HUD (car fields +0xde, +0xe2, +0xe0, 0..0xffff, colours 0xf7, 0xf4,
// 0xf5): vertical bars 2 font units high and 3 frame widths wide at the bottom of the player's screen
// strip, each a dark 0xf1 frame (one text-shadow wider at the top and bottom) behind the filled part.
void hud_draw_gauges(int p) {
    int unit = g_font_size, border = g_menu_border, shadow = g_text_shadow;
    int width = border * 3;
    int top = g_screen_h - (unit + border) * 2;
    int bottom = top + unit * 2;
    int x = (unit * 3 - border * 10 - 1) / 2 - guest::to_int(static_cast<float>((p + 2) * g_screen_w) * -0.125f) -
            guest::to_int(static_cast<float>(unit) * -0.5f);
    const uint8_t *car = g_car_states[p].raw;
    static constexpr int kField[3] = {0xde, 0xe2, 0xe0};
    static constexpr uint8_t kColour[3] = {0xf7, 0xf4, 0xf5};
    for (int g = 0; g < 3; g++) {
        uint16_t v;
        std::memcpy(&v, car + kField[g], 2);
        int level = (0xffff - static_cast<int>(v)) * unit * 2 / 0xffff;
        for (int y = level - shadow + top; y < bottom + shadow; y++)
            guest::call_cdecl(0x497d10, g_pixels + static_cast<uint32_t>(g_pitch * y + x), 0xf1u, width);
        for (int y = level + top; y < bottom; y++)
            guest::call_cdecl(0x497d10, g_pixels + static_cast<uint32_t>(g_pitch * y + shadow + x),
                              static_cast<uint32_t>(kColour[g]), width - shadow * 2);
        x += width + 1;
    }
}

// A volume option (0..100) as millibels: -10000 - trunc(log10(v * 1e8 + 1) * -1000), i.e. 0 at 100.
static int option_millibels(uint8_t v) {
    return -10000 - guest::to_int(guest_log10f(static_cast<float>(v) * 100000000.0f + 1.0f) * -1000.0f);
}

// The engine volume option (b20) as the factor g_unknown_4a47f4 = log10(v * 1e8 + 1) * 1000.
void engine_volume_apply() {
    g_unknown_4a47f4 = guest_log10f(static_cast<float>(g_race_options.b20) * 100000000.0f + 1.0f) * 1000.0f;
    g_unknown_4a4790 = 10;
}

// The effects volume option (b22) for the start beep and the two other effect sounds.
void effects_volume_apply() {
    const uint32_t sounds[3] = {g_start_sound, g_cheer_sound, g_fuel_sound};
    for (uint32_t s : sounds) {
        int v = option_millibels(g_race_options.b22);
        sound_set_volume(s, v);
    }
}

// Race start: applies the volume options (engine volume factor from option b20, the effects volume b22
// to the start beep and two other sounds) and copies each driver's controls into its car: the four key
// codes (car +0x48) and joystick bindings (car +0x18) of a human, the skill (car +0x4c) of the computer.
void race_controls_init() {
    engine_volume_apply();
    effects_volume_apply();
    for (int i = 0; i < g_player_count; i++) {
        const Driver *d = g_player_driver[i].get();
        uint8_t *car = g_car_states[i].raw;
        if (d->key[0] == 0) {
            std::memcpy(car + 0x4c, &d->key[1], 4);
        } else {
            std::memcpy(car + 0x48, d->key, 16);
            std::memcpy(car + 0x18, d->joy, 48);
        }
    }
}

// Continues whatever the game was doing before it was paused.
void game_resume() {
    uint8_t s = g_game_state;
    if (s == GS_INFO) {
        info_box_show();
    } else if (s < 3) {
        g_game_state = GS_RACING;
        g_race_substate = 0;
    } else if (s == GS_RESULTS) {
        results_continue();
    } else if (s == 6 || s == 7) {
        next_race();
    }
    g_racing = 1;
    g_unknown_4a4788 = 0;
    g_resume_ticks = guest::call_import(IMP_SDL_GetTicks);
}

// Skip race (pause menu): a race in progress is finished when it has a result (a single player with a
// lap done, or the leader at the full distance); otherwise the records are saved and the season moves on.
void race_skip() {
    if (g_game_state >= GS_FINISHED) {
        game_resume();
        return;
    }
    if ((g_player_count == 1 && g_leader_laps > 0) || g_leader_laps >= g_race_laps) {
        race_render();
        g_game_state = GS_FINISHED;
        return;
    }
    track_records_save();
    if (g_current_track + 1 < g_track_count)
        next_race();
    else
        season_results();
}

// Frees the replay recording.
void replay_reset() {
    for (int i = 0; i < g_player_count; i++) {
        if (g_replay_buffers[i]) guest::call_cdecl(0x496bd7, g_replay_buffers[i].a);  // free
        g_replay_buffers[i] = nullptr;
    }
    g_replay_head = 0;
    g_replay_frames = 0;
    g_replay_pos = 0.0f;
    g_replay_mark_out = -1.0f;
    g_replay_mark_in = -1.0f;
}

// The start countdown, every 70 ticks of the start phase: lights 0..2 beep, light 3 (go) beeps higher.
void start_light_step(uint8_t light) {
    uint32_t h = g_start_sound;
    if (h) {
        MsVector<ptr32<SoundRef>> &v = g_sounds;
        if (h <= static_cast<uint32_t>(v.size()) && v[static_cast<int>(h - 1)])
            v[static_cast<int>(h - 1)]->rate = light < 3 ? 48000.0f / 44100.0f : 64000.0f / 44100.0f;
        sound_play(h);
    }
    g_start_light = light;
    race_render();
}

// The loading screen while a track loads: the back buffer cleared to colour 1 with the loading text in
// the middle, no start lights.
void show_loading() {
    if (g_back) sdl::FillRect(g_back, nullptr, 1);
    g_start_light = -1;
    guest::call_import(IMP_SDL_Flip, sdl::GetVideoSurface());
    lock_surface(SURF_BACK);
    int s = g_font_size;
    const MsString *text = &g_lang[L_LOADING];
    draw_text_at(text, g_screen_w / 2 - static_cast<int>((text->size * static_cast<uint32_t>(s)) >> 1),
                 g_screen_h / 2 - s, 0xf3, 0);
    unlock_surface();
    frame_present();
}

// Sets up the replay recording: the rate (rate > 0 overrides gr.ini; 1..100) and the ring buffer size
// (the configured length at 100 frames per second, 10..360000 frames, at least min_capacity), then
// allocates one buffer per player. When the size changed the old buffers are freed first; when it did
// not, the original allocates new ones anyway and the old buffers leak (kept).
void replay_init(int min_capacity, int rate) {
    int old_capacity = g_replay_capacity;
    int r = rate > 0 ? rate : g_cfg_replay_rate;
    if (r < 1) r = 1;
    else if (r > 100) r = 100;
    g_replay_rate = r;
    int capacity = g_cfg_replay_length * 100 / r;
    g_replay_step = 1.0f / static_cast<float>(r);
    if (capacity < 10) capacity = 10;
    else if (capacity > 360000) capacity = 360000;
    if (min_capacity > capacity) capacity = min_capacity;
    g_replay_capacity = capacity;
    for (int i = 0; i < g_player_count; i++) {
        if (g_replay_buffers[i] && old_capacity != g_replay_capacity) {
            guest::call_cdecl(0x496bd7, g_replay_buffers[i].a);  // free
            g_replay_buffers[i] = nullptr;
        }
        // operator new((capacity + 1) * 18), saturating on overflow
        uint64_t bytes = static_cast<uint64_t>(static_cast<uint32_t>(g_replay_capacity + 1)) * sizeof(ReplayFrame);
        uint32_t n = bytes >> 32 ? 0xffffffffu : static_cast<uint32_t>(bytes);
        g_replay_buffers[i] = ptr32<uint8_t>::from_addr(guest::call_cdecl(0x496c14, n));
    }
    g_replay_head = 0;
    g_replay_frames = 0;
    g_replay_pos = 0.0f;
    g_replay_speed = 1.0f;
    g_replay_mark_out = -1.0f;
    g_replay_mark_in = -1.0f;
}

// Loads the season's current track and gets the race ready: the info box is shown and the start saved
// as "Autosave". A track that fails to load is skipped (or the season ends).
void race_load_track() {
    show_loading();
    if (g_player_count == 1) ghost_init();
    replay_init(0, 0);
    g_timed_race = 0;
    g_time_limit = -1;
    g_race_clock_start = -1;
    g_race_clock_end = -1;
    g_pause_clock = -1;
    if (g_length_unit == 2) {
        g_timed_race = 1;
        g_unknown_4a4792 = 0;
        g_time_limit = static_cast<uint16_t>(g_race_length) * 60;
    }
    if (track_load() == -1) {
        if (g_current_track + 1 < g_track_count) {
            g_current_track = g_current_track + 1;
            race_load_track();
        } else {
            season_results();
        }
        return;
    }
    race_setup();
    race_setup_view();
    info_box_show();
    g_game_state = GS_INFO;
    g_unknown_4a4788 = 0;
    g_resume_ticks = guest::call_import(IMP_SDL_GetTicks);
    guest::Local<MsString> name;
    name->init();
    name->assign(&guest::at<char>(0x49cce8), 8);  // "Autosave"
    save_game(name.get());
    name->destroy();
}

// End of a race: credits each player's driver with the race, the place and its points (points table
// b02 of the race options; a player who neither finished more laps nor in less time than the one ahead
// shares that one's place) and, with a best lap equal to the race's fastest, the fastest-lap points; the
// points also go to the season standings. Then saves the driver files.
void race_record_results() {
    const uint8_t *points = g_race_options.b02;
    int shared = 0;
    for (int i = 0; i < g_player_count; i++) {
        int p = g_finish_order[i];
        if (i == 0) {
            shared = 0;
        } else {
            int ahead = g_finish_order[i - 1];
            bool fewer_laps = car_laps(p) != static_cast<uint32_t>(g_leader_laps) && car_laps(p) < car_laps(ahead);
            if (fewer_laps || car_race_time(p) > car_race_time(ahead)) shared = i;
        }
        Driver *d = g_player_driver[p].get();
        d->races++;
        g_player_driver[p]->race_places[i]++;
        g_player_driver[p]->points += points[shared];
        g_season_points[p] += points[shared];
        if (static_cast<int>(car_best_lap(i)) <= g_fastest_lap) {
            g_player_driver[i]->fastest_laps++;
            g_player_driver[i]->points += points[6];
            g_season_points[i] += points[6];
        }
    }
    for (int i = 0; i < g_player_count; i++) {
        guest::Local<MsString> name;
        name->init();
        name->assign(g_player_name[i], static_cast<uint32_t>(std::strlen(g_player_name[i])));
        driver_save(g_player_driver[i].get(), name.get());
        name->destroy();
    }
}

// End of a season: ranks the players by points (players with equal points share the better place),
// counts the season and the place in each driver's record and saves the driver files.
void season_record_places() {
    int order[6];
    for (int i = 0; i < g_player_count; i++) {  // insertion sort, highest points first (stable)
        order[i] = i;
        for (int j = i; j > 0 && g_season_points[order[j]] > g_season_points[order[j - 1]]; j--) {
            order[j] = order[j - 1];
            order[j - 1] = i;
        }
    }
    int place = 0;
    for (int i = 0; i < g_player_count; i++) {
        if (i == 0) place = 0;
        else if (g_season_points[order[i]] < g_season_points[order[i - 1]]) place = i;
        Driver *d = g_player_driver[order[i]].get();
        d->seasons++;
        g_player_driver[order[i]]->places[place]++;
    }
    for (int i = 0; i < g_player_count; i++) {
        guest::Local<MsString> name;
        name->init();
        name->assign(g_player_name[i], static_cast<uint32_t>(std::strlen(g_player_name[i])));
        driver_save(g_player_driver[i].get(), name.get());
        name->destroy();
    }
}

// The sound behind handle h, or null (inlined lookup).
// sounds\<a>\<b><ext> as built by the original (each step a new string, freed in reverse order).
static uint32_t load_sound_path(bool construct_root, uint32_t a_cstr, const MsString *a, uint32_t b, uint32_t ext) {
    guest::Local<MsString> s[6];
    if (construct_root) {
        s[0]->construct(0x49d7ecu);  // "sounds"
    } else {
        s[0]->init();
        s[0]->assign(&guest::at<char>(0x49d7ec), 6);
    }
    MsString::concat_move(s[1].get(), s[0].get(), 0x49ccfcu);  // + "\\"
    if (a) MsString::concat_move(s[2].get(), s[1].get(), a);
    else MsString::concat_move(s[2].get(), s[1].get(), a_cstr);
    MsString::concat_move(s[3].get(), s[2].get(), 0x49ccfcu);
    MsString::concat_move(s[4].get(), s[3].get(), b);
    uint32_t h;
    if (ext) {
        MsString::concat_move(s[5].get(), s[4].get(), ext);
        h = sound_load(s[5].get());
        s[5]->destroy();
    } else {
        h = sound_load(s[4].get());
    }
    for (int i = 4; i >= 0; i--) s[i]->destroy();
    return h;
}

// Loads each player's engine sound from the sound set: sounds\<set>\<car name>.wav, else the set's
// engine.wav, else sounds\default\engine.wav. A player with the same car as an earlier one shares its
// sample; players on the fallback sound share the first one's. Nothing without an audio device.
void engine_sounds_load(const MsString *set) {
    if (!g_audio) return;
    uint8_t generic[6] = {};
    for (int p = 0; p < 6; p++) {
        uint32_t h = g_engine_sound[p];
        if (SoundRef *s = sound_ref(h)) {
            sound_ref_free(s);
            g_sounds[static_cast<int>(h) - 1] = nullptr;
        }
        g_engine_sound[p] = 0;
        if (p >= g_player_count || !g_player_driver[p]) continue;
        const Driver *d = g_player_driver[p].get();
        int first_generic = -1, same_car = -1;
        for (int j = 0; j < p; j++) {
            if (first_generic < 0 && generic[j]) first_generic = j;
            if (same_car < 0 && d->car == g_player_driver[j]->car) same_car = generic[j] ? first_generic : j;
        }
        if (same_car >= 0) {
            uint32_t other = g_engine_sound[same_car];
            if (other) g_engine_sound[p] = sound_share(other);
            continue;
        }
        uint32_t car_name = guest::addr(&g_cars.get()[d->car]);
        g_engine_sound[p] = load_sound_path(false, 0, set, car_name, 0x49db24u);  // ".wav"
        if (g_engine_sound[p]) continue;
        generic[p] = 1;
        if (first_generic < 0) {
            g_engine_sound[p] = load_sound_path(false, 0, set, 0x49db2cu, 0);  // "engine.wav"
            if (!g_engine_sound[p]) g_engine_sound[p] = load_sound_path(true, 0x49daecu, nullptr, 0x49db2cu, 0);  // "default"
        } else if (g_engine_sound[first_generic]) {
            g_engine_sound[p] = sound_share(g_engine_sound[first_generic]);
        }
    }
}

// Loads the sound set's effects (0x4875b0): skid, damage, ding, yeah and fuel (.wav at 0x49dae0,
// 0x49daf4, 0x49db00, 0x49db0c, 0x49db18), each from sounds\<set>\, else from sounds\default\. The
// previous samples are freed first. Nothing without an audio device.
void sound_effects_load(const MsString *set) {
    if (!g_audio) return;
    uint32_t *slot[5] = {&g_skid_sound, &g_damage_sound, &g_start_sound, &g_cheer_sound, &g_fuel_sound};
    static const uint32_t kFile[5] = {0x49dae0, 0x49daf4, 0x49db00, 0x49db0c, 0x49db18};
    for (auto *s : slot) {
        uint32_t h = *s;
        if (SoundRef *r = sound_ref(h)) {
            sound_ref_free(r);
            g_sounds[static_cast<int>(h) - 1] = nullptr;
        }
        *s = 0u;
    }
    for (int i = 0; i < 5; i++) {
        uint32_t h = load_sound_path(false, 0, set, kFile[i], 0);
        *slot[i] = h;
        if (!h) *slot[i] = load_sound_path(true, 0x49daecu /* "default" */, nullptr, kFile[i], 0);
    }
}

static uint32_t malloc_guest(uint32_t n) { return guest::call_cdecl(0x496c14, n); }

// frees a guest pointer variable and clears it
static void free_and_clear(uint32_t &p) {
    if (p) {
        guest::call_cdecl(0x496bd7, p);
        p = 0;
    }
}

// The race's work buffers, (re)allocated at race start: the 0x1c00-byte record table (cleared), the
// random table (srand(ticks), 256 x rand() / 32768), depth buffer and unshaded track image of the screen's
// size, the projection (pixels per unit = screen width, depth unit 0.009155273), the tile map, height map
// and the two shade tables.
void alloc_race_buffers() {
    free_and_clear(g_unknown_4a4774);
    uint32_t b = malloc_guest(0x1c00);
    g_unknown_4a4774 = b;
    g_unknown_4a4794 = 0;
    guest::call_cdecl(0x497d10, b, 0u, 0x1c00u);  // memset
    guest::call_cdecl(guest::at<uint32_t>(IMP_srand), guest::call_import(IMP_SDL_GetTicks));
    free_and_clear(g_random_table);
    g_random_table = malloc_guest(0x400);
    for (int i = 0; i < 256; i++) {
        int r = static_cast<int>(guest::call_cdecl(guest::at<uint32_t>(IMP_rand)));
        guest::at<float>(g_random_table + 4u * static_cast<uint32_t>(i)) = static_cast<float>(r) * 3.0517578125e-05f;
    }
    free_and_clear(g_depth_buf);
    uint64_t depth = static_cast<uint64_t>(static_cast<uint32_t>(g_screen_h * g_screen_w)) * 2;
    g_depth_buf = malloc_guest(depth >> 32 ? 0xffffffffu : static_cast<uint32_t>(depth));
    free_and_clear(g_track_image);
    g_track_image = malloc_guest(static_cast<uint32_t>(g_screen_h * g_screen_w));
    g_start_light_depth = static_cast<float>(g_screen_w);
    guest::at<uint32_t>(0x4a402c) = 0x3c160000;  // g_depth_scale = 0.009155273
    g_screen_aspect = static_cast<float>(g_screen_h) / g_start_light_depth;
    free_and_clear(g_tile_map);
    free_and_clear(g_unknown_4a47a0);
    free_and_clear(g_shade_dark);
    free_and_clear(g_shade_light);
    g_tile_map = malloc_guest(0x40000);
    g_unknown_4a47a0 = malloc_guest(0x1000);
    g_shade_dark = malloc_guest(0x30);
    g_shade_light = malloc_guest(0x30);
}

// Starts a race (0x43b080): engine sounds of the options' sound set, the volume options, the race
// buffers and shade tables; each player's car model is copied to its race slot, its season points are
// reset and its engine starts looping silently (as does the ambient sound); then the first track is
// loaded.
void race_start() {
    {
        guest::Local<MsString> sound_set;
        sound_set->init();
        const char *p = g_race_options.text[1];
        sound_set->assign(p, static_cast<uint32_t>(std::strlen(p)));
        engine_sounds_load(sound_set.get());
        sound_set->destroy();
    }
    engine_volume_apply();
    alloc_race_buffers();
    shade_tables_init();
    g_current_track = 0;
    for (int i = 0; i < g_player_count; i++) {
        std::memcpy(g_race_models[i], &g_cars.get()[g_player_driver[i]->car], 0x540);
        uint32_t s = g_engine_sound[i];
        g_season_points[i] = 0;
        if (!s) continue;
        sound_set_attenuation(s, 5.0f);
        sound_play_loop(s);
    }
    if (g_skid_sound) {
        sound_set_attenuation(g_skid_sound, 5.0f);
        sound_play_loop(g_skid_sound);
    }
    effects_volume_apply();
    g_racing = 1;
    race_load_track();
}

// Between two races of a season (0x4473e0). While nobody has season points yet, the next track is
// simply loaded. Otherwise (game state 6) the race view is dimmed and the standings drawn into it:
// track name and race length (lang 1 for one lap, else "<laps>" + lang 2), lang 13, lang 14 +
// "<race>/<races>" + lang 15, then the players with points, best first (a stable insertion sort): a
// place box and number where the points drop, the car (0x443cc0, seen from the front left), the name
// and the points. Then the frame is shown.
void season_standings() {
    int total = 0;
    for (int i = 0; i < g_player_count; i++) total += g_season_points[i];
    guest::Local<MsString> s, t, tmp;
    s->init();
    if (total == 0) {
        g_current_track = g_current_track + 1;
        race_load_track();
        s->destroy();
        return;
    }
    race_render_dimmed();
    g_game_state = 6;
    lock_surface(SURF_BACK);
    const char *track = g_current_track >= 0 && g_current_track < g_selected_tracks.size()
                            ? g_selected_tracks[g_current_track].c_str()
                            : nullptr;
    uint32_t slash = guest::call_cdecl(guest::at<uint32_t>(IMP_strrchr), guest::addr(track), 0x5cu);
    const int f = g_font_size;
    const int x = g_screen_w / 2 - f * 18;
    int y = (g_screen_h - f * 36 - g_menu_border * 5) / 2;
    const char *name = &guest::at<char>(slash + 1);
    t->init();
    t->assign(name, static_cast<uint32_t>(std::strlen(name)));
    draw_text_at(t.get(), x, y, 0xf2, 1);
    t->destroy();
    if (g_leader_laps == 1) {
        MsString::concat(tmp.get(), 0x49cd18u, &g_lang[1]);
    } else {
        uint32_t n = guest::call_cdecl(0x46f320, 0x49cd1cu, g_leader_laps);  // "%d"
        MsString::concat(tmp.get(), n, &g_lang[2]);
    }
    s->destroy();
    s->move_assign(tmp.get());
    tmp->destroy();
    draw_text_at(s.get(), (0x24 - static_cast<int>(s->size)) * f + x, y, 0xf2, 1);  // right-aligned
    y += f * 4;
    draw_text_at(&g_lang[13], x, y, 0xf5, 1);
    y += f * 2;
    {
        uint32_t n = guest::call_cdecl(0x46f320, 0x49ccc4u, g_current_track + 1, g_track_count);  // "%d/%d"
        MsString::concat(tmp.get(), &g_lang[14], n);
        MsString::concat_move(t.get(), tmp.get(), &g_lang[15]);
        s->destroy();
        s->move_assign(t.get());
        t->destroy();
        tmp->destroy();
    }
    draw_text_at(s.get(), x, y, 0xf5, 1);
    y += f * 3;

    int order[kMaxPlayers];
    for (int i = 0; i < g_player_count; i++) {
        order[i] = i;
        for (int j = i; j > 0 && g_season_points[order[j]] > g_season_points[order[j - 1]]; j--) {
            order[j] = order[j - 1];
            order[j - 1] = i;
        }
    }
    for (int k = 0; k < g_player_count; k++) {
        int p = order[k];
        if (g_season_points[p] < 1) break;
        int font = g_font_size;
        if (k == 0 || g_season_points[p] < g_season_points[order[k - 1]]) {
            // place box: a frame (0xf6) around a square (0xf5), the place number on it
            int b = g_menu_border;
            for (int row = y - b; row < y + 2 * font + b; row++)
                guest::call_cdecl(0x497d10, g_pixels + static_cast<uint32_t>(g_pitch * row + x), 0xf6u, static_cast<uint32_t>((b + font) * 2));
            for (int row = y; row < y + 2 * font; row++)
                guest::call_cdecl(0x497d10, g_pixels + static_cast<uint32_t>(g_pitch * row + b + x), 0xf5u, static_cast<uint32_t>(2 * font));
            uint32_t n = guest::call_cdecl(0x46f320, 0x49cdd8u, k + 1);
            t->init();
            t->assign(&guest::at<char>(n), static_cast<uint32_t>(std::strlen(&guest::at<char>(n))));
            draw_text_at(t.get(), (g_menu_border - guest::to_int(static_cast<float>(g_font_size) * guest::at<float>(0x49e018))) + x, y, 0xf1, 0);
            t->destroy();
            font = g_font_size;
        }
        // the car (view axes and zoom as the original passes them)
        draw_car_preview(static_cast<uint32_t>(p), g_race_models[p], x + font * 6, font + y, 120.0f);
        t->init();
        const char *driver = g_player_name[p];
        t->assign(driver, static_cast<uint32_t>(std::strlen(driver)));
        draw_text_at(t.get(), x + g_font_size * 10, y, 0xf2, 1);
        t->destroy();
        uint32_t n = guest::call_cdecl(0x46f320, 0x49cd80u, g_season_points[p]);
        s->assign(&guest::at<char>(n), static_cast<uint32_t>(std::strlen(&guest::at<char>(n))));
        font = g_font_size;
        draw_text_at(s.get(), (0x24 - static_cast<int>(s->size)) * font + x, y, 0xf5, 1);
        y = font + y + font * 2;
    }
    unlock_surface();
    frame_present();
    s->destroy();
}

// Sets up a race (0x43c130): the number of laps, the starting grid and each car's start state.
//  - laps: the race length (laps), or the km over the track length rounded up, or 3 for timed races;
//  - grid: random, or (option +0x0e of the race options) 1: in player order / by points, best first,
//    2: reversed / worst first (player order in the first race of a season, by points afterwards);
//  - each car is put on its grid slot (the first track objects: position and heading), its
//    orientation built from the ground normal there and the heading, and its wheels set on the ground.
void race_grid_setup() {
    g_unknown_4a4794 = 0;
    guest::call_cdecl(0x497d10, g_unknown_4a4774, 0u, 0x1c00u);  // memset
    const int n = g_player_count;
    if (g_race_options.length_unit == 0) {
        int laps = static_cast<uint16_t>(g_race_options.length);
        g_race_laps = laps <= 0 ? 1 : laps;
    } else if (g_race_options.length_unit == 1) {
        float metres = static_cast<float>(static_cast<double>(static_cast<uint32_t>(g_track_length)));
        int laps = 1 - guest::to_int((static_cast<float>(static_cast<uint16_t>(g_race_options.length)) * -1000.0f) / metres);
        g_race_laps = laps <= 0 ? 1 : laps;
    } else {
        g_race_laps = 3;
    }
    g_leader_laps = 0;
    g_unknown_4a47f8 = 0;
    g_fastest_lap = kGhostFrames;
    g_unknown_4a4798 = -1;
    g_unknown_4a479c = -1;
    auto order = [](int i) -> int32_t & { return guest::at<int32_t>(guest::addr(g_finish_order) + 4u * static_cast<uint32_t>(i)); };
    auto place = [](int car) -> uint8_t & { return g_car_states[car].raw[0]; };
    for (int i = 0; i < n; i++) order(i) = -1;

    // random grid: car i takes a random one of the free slots
    for (int i = 0, left = n; i < n; i++, left--) {
        uint8_t k = ++g_random_index;
        int pick = guest::to_int(static_cast<float>(left) * guest::at<float>(g_random_table + 4u * k));
        int free = 0, slot = 0;
        for (;;) {
            if (order(slot) < 0) free++;
            if (free > pick) break;
            if (++slot >= n) break;
        }
        order(slot) = i;
        place(i) = static_cast<uint8_t>(slot);
    }
    uint8_t mode = g_race_options.b0e;
    if (g_current_track == 0 && mode != 0) {
        for (int i = 0; i < n; i++) {
            int car = mode == 1 ? i : n - 1 - i;
            order(i) = car;
            place(car) = static_cast<uint8_t>(i);
        }
    } else if (mode == 1 || mode == 2) {
        // insertion sort of the slots by season points (mode 1: most first, 2: fewest first)
        for (int i = 1; i < n; i++) {
            for (int j = i; j > 0; j--) {
                int cur = order(j), prev = order(j - 1);
                bool swap = mode == 1 ? g_season_points[cur] > g_season_points[prev] : g_season_points[cur] < g_season_points[prev];
                if (!swap) break;
                order(j) = prev;
                order(j - 1) = cur;
                place(prev) = static_cast<uint8_t>(j);
                place(cur) = static_cast<uint8_t>(j - 1);
            }
        }
    }

    const float extra_mass = static_cast<float>(g_race_options.b0c) * 65535.0f * 1.52587890625e-05f;
    for (int i = 0; i < g_player_count; i++) {
        uint8_t *s = g_car_states[i].raw;
        auto I32 = [s](int off) -> int32_t & { return *reinterpret_cast<int32_t *>(s + off); };
        auto F = [s](int off) -> float & { return *reinterpret_cast<float *>(s + off); };
        auto V = [s](int off) -> Vec3 & { return *reinterpret_cast<Vec3 *>(s + off); };
        const uint8_t *model = g_race_models[i];
        const Driver *d = g_player_driver[i].get();
        const int32_t *dw = reinterpret_cast<const int32_t *>(d);
        // the controls: keys and joystick bindings (AI drivers: only the skill, at +0x4c; +0x54 = 100)
        if (d->key[0] == 0) {
            I32(0x48) = 0;
            I32(0x4c) = d->key[1];
            I32(0x50) = 0;
            I32(0x54) = 100;
        } else {
            for (int k = 0; k < 4; k++) I32(0x48 + 4 * k) = d->key[k];
            for (int k = 0; k < 12; k++) I32(0x18 + 4 * k) = dw[0x4c / 4 + k];
        }
        I32(0xd4) = 0;
        *reinterpret_cast<uint16_t *>(s + 0xdc) = kGhostFrames;
        uint8_t k = ++g_random_index;
        *reinterpret_cast<uint16_t *>(s + 0xd8) = 0xff00;
        s[0xda] = 0xff;
        I32(0x10) = 0;
        I32(0x14) = 0;
        *reinterpret_cast<uint16_t *>(s + 1) = 0;
        *reinterpret_cast<int32_t *>(s + 3) = 10;
        I32(0xc) = guest::to_int(guest::at<float>(g_random_table + 4u * k) * 20.0f);
        *reinterpret_cast<uint16_t *>(s + 7) = 0;
        I32(0x64) = I32(0x68) = I32(0x6c) = 0;
        I32(0x70) = I32(0x74) = I32(0x78) = 0;
        float mass = *reinterpret_cast<const float *>(model + 0xb0) + extra_mass;
        F(0xc8) = mass;
        *reinterpret_cast<uint16_t *>(s + 0xe2) = 0;
        I32(0xde) = -1;
        F(0xa0) = 0.2f;
        I32(0xd0) = 0;
        F(0xc4) = 1200.0f / (mass + 200.0f);

        // position and orientation: row 1 = the ground normal, row 2 the heading, row 0 their cross product
        const TrackObject &slot = g_objects.get()[s[0]];
        Vec3 &pos = V(0x58), &r0 = V(0x7c), &r1 = V(0x88), &r2 = V(0x94);
        pos = slot.pos;
        terrain_sample(&pos, &r1);
        r2 = slot.heading;
        float a1 = r1.x, b1 = r1.y, c1 = r1.z;
        r0.x = b1 * r2.z - c1 * r2.y;
        r0.y = r2.x * c1 - a1 * r2.z;
        r0.z = a1 * r2.y - b1 * r2.x;
        float x = r0.x, y = r0.y, z = r0.z;
        float len = guest_sqrtf((x * x + y * y) + z * z) + 0.001f;
        y = y / len;
        z = z / len;
        x = x / len;
        a1 = r1.x;
        c1 = r1.z;
        r0.z = z;
        r0.y = y;
        r0.x = x;
        r2.x = y * c1 - b1 * z;
        r2.y = z * a1 - x * c1;
        r2.z = b1 * x - y * a1;
        // raised by the model's ride height along the normal
        float h = *reinterpret_cast<const float *>(model + 0x14);
        pos.y = b1 * h + pos.y;
        pos.x = pos.x + h * a1;
        pos.z = h * c1 + pos.z;
        F(0xcc) = 0;

        // the wheels (model +0x504) in the world, on the ground
        const Vec3 *wheel = reinterpret_cast<const Vec3 *>(model + 0x504);
        Vec3 *out = reinterpret_cast<Vec3 *>(s + 0xe8) + model[0xbd];
        const float lim = g_track_size - 0.001f;
        for (int w = 0; w < 4; w++) {
            const Vec3 &m = wheel[w];
            Vec3 &o = out[w];
            o.x = ((pos.x + m.x * r0.x) + r1.x * m.y) + m.z * r2.x;
            o.y = ((m.x * r0.y + pos.y) + m.y * r1.y) + r2.y * m.z;
            o.z = ((r0.z * m.x + pos.z) + m.y * r1.z) + r2.z * m.z;
            float gx = o.x, gz = o.z;
            if (0.0f > gx) gx = 0.0f;
            else if (gx > lim) gx = lim;
            if (0.0f > gz) gz = 0.0f;
            else if (gz > lim) gz = lim;
            gx = gx / g_height_cell;
            gz = gz / g_height_cell;
            int ix = guest::to_int(gx), iz = guest::to_int(gz);
            float fx = gx - static_cast<float>(ix), fz = gz - static_cast<float>(iz);
            const uint8_t *hm = g_height_map.get() + iz * 64 + ix;
            float ux = 1.0f - fx, uz = 1.0f - fz;
            float hy = static_cast<float>(hm[0]) * ux * uz + static_cast<float>(hm[1]) * fx * uz;
            hy = hy + static_cast<float>(hm[0x40]) * ux * fz;
            hy = hy + static_cast<float>(hm[0x41]) * fx * fz;
            o.y = hy * 0.05f;
        }
    }
}

// After a race (0x447c50): unless that was the season's last race with points scored, the race is
// freed (the next race or the menu follows). Otherwise the final standings: the race screen dimmed
// into the background surface, the places recorded (season_record_places), the image turned to grey
// shades through the palette, the title (the string at 0x49cd88), lang 11 and 12, and the players with
// points ranked as in season_standings. The palette's first 30 entries are then darkened to browns,
// entries 30..34 get fixed colours, and the sound g_cheer_sound plays.
void season_end() {
    guest::Local<MsString> points_text;
    points_text->init();
    int total = 0;
    for (int i = 0; i < g_player_count; i++) total += g_season_points[i];
    if (total == 0 || g_current_track < g_track_count - 1) {
        race_free();
        points_text->destroy();
        return;
    }
    race_render_dimmed();
    blit_surface(SURF_BACK, SURF_BACKGROUND);
    g_game_state = 8;
    season_record_places();
    lock_surface(SURF_BACKGROUND);

    // grey shades: a palette index for each colour by its brightness
    guest::Local<uint32_t[256]> pal;
    palette_store(*pal);
    guest::Local<uint8_t[256]> grey;
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = reinterpret_cast<const uint8_t *>(&(*pal)[i]);
        float v = static_cast<float>(c[1]) * 0.6f + static_cast<float>(c[2]) * 0.3f;
        v = v + static_cast<float>(static_cast<double>(c[0])) * 0.1f;
        (*grey)[i] = static_cast<uint8_t>(1 - static_cast<uint8_t>(guest::to_int(v * -0.1137f)));
    }
    const int w = g_screen_w;
    for (int row = 0; row < g_screen_h; row++) {
        uint8_t *line = &guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(g_pitch * row));
        for (int x = 0; x < w; x++) line[x] = (*grey)[line[x]];
    }

    const int f = g_font_size;
    const int x = w / 2 + f * -0x12;
    int y = (g_menu_border * -5 + f * -0x24 + g_screen_h) / 2;
    {
        guest::Local<MsString> title;
        title->init();
        title->assign(&guest::at<char>(0x49cd88), 9);
        draw_text_at(title.get(), x, y, 0xf2, 1);
        title->destroy();
    }
    y += f * 4;
    draw_text_at(&g_lang[11], x, y, 0xf5, 1);
    y += f * 2;
    draw_text_at(&g_lang[12], x, y, 0xf5, 1);
    y = f * 3 + y;

    int order[kMaxPlayers];
    for (int i = 0; i < g_player_count; i++) {
        order[i] = i;
        for (int j = i; j > 0 && g_season_points[order[j]] > g_season_points[order[j - 1]]; j--) {
            order[j] = order[j - 1];
            order[j - 1] = i;
        }
    }
    guest::Local<MsString> t;
    for (int k = 0; k < g_player_count; k++) {
        int p = order[k];
        if (g_season_points[p] < 1) break;
        int font = g_font_size;
        if (k == 0 || g_season_points[p] < g_season_points[order[k - 1]]) {
            int b = g_menu_border;
            for (int row = y - b; row < b + font * 2 + y; row++)
                guest::call_cdecl(0x497d10, g_pixels + static_cast<uint32_t>(g_pitch * row + x), 0xf6u, static_cast<uint32_t>((b + font) * 2));
            for (int row = y; row < font * 2 + y; row++)
                guest::call_cdecl(0x497d10, g_pixels + static_cast<uint32_t>(g_pitch * row + b + x), 0xf5u, static_cast<uint32_t>(font * 2));
            uint32_t n = guest::call_cdecl(0x46f320, 0x49cdd8u, k + 1);
            t->init();
            t->assign(&guest::at<char>(n), static_cast<uint32_t>(std::strlen(&guest::at<char>(n))));
            draw_text_at(t.get(), (g_menu_border - guest::to_int(static_cast<float>(g_font_size) * -0.5f)) + x, y, 0xf1, 0);
            t->destroy();
            font = g_font_size;
        }
        draw_car_preview(static_cast<uint32_t>(p), g_race_models[p], x + font * 6, font + y, 120.0f);
        t->init();
        const char *driver = g_player_name[p];
        t->assign(driver, static_cast<uint32_t>(std::strlen(driver)));
        draw_text_at(t.get(), x + g_font_size * 10, y, 0xf2, 1);
        t->destroy();
        uint32_t n = guest::call_cdecl(0x46f320, 0x49cd80u, g_season_points[p]);
        points_text->assign(&guest::at<char>(n), static_cast<uint32_t>(std::strlen(&guest::at<char>(n))));
        font = g_font_size;
        draw_text_at(points_text.get(), (0x24 - static_cast<int>(points_text->size)) * font + x, y, 0xf5, 1);
        y = font * 3 + y;
    }
    unlock_surface();

    for (int i = 0; i < 30; i++) {
        const uint8_t *c = reinterpret_cast<const uint8_t *>(&(*pal)[i]);
        (*pal)[i] = (static_cast<uint32_t>(c[1]) * 3 >> 3) * 0x100 + (static_cast<uint32_t>(c[0]) * 6 >> 3);
    }
    static const uint32_t kFixed[5] = {0x00ff0000, 0x00ffff00, 0x0000ff00, 0x00c0ffff, 0x00ff80ff};
    for (int i = 0; i < 5; i++) (*pal)[30 + i] = kFixed[i];
    palette_load(*pal);
    g_unknown_4a4794 = 0;
    guest::call_cdecl(0x497d10, g_unknown_4a4774, 0u, 0x1c00u);  // memset
    sound_play(g_cheer_sound);
    points_text->destroy();
}

// "<n> <lang one/many>" for the race length: laps (lang 1, 2) or minutes in a timed race (lang 165, 166).
void race_length_text(MsString *out) {
    guest::Local<MsString> tmp;
    const bool timed = g_timed_race != 0;
    const int n = timed ? static_cast<int>(g_race_options.length) : g_race_laps;
    const MsString *unit = &g_lang[timed ? (n == 1 ? 165 : 166) : (n == 1 ? 1 : 2)];
    if (n == 1) MsString::concat(tmp.get(), 0x49cd18u /* "1 " */, unit);
    else MsString::concat(tmp.get(), guest::call_cdecl(0x46f320, 0x49cd1cu /* "%d " */, n), unit);
    out->move_assign(tmp.get());
    tmp->destroy();
}

namespace {
void text_at(const char *s, int x, int y, uint8_t colour) {
    guest::Local<MsString> t;
    t->init();
    t->assign(s, static_cast<uint32_t>(std::strlen(s)));
    draw_text_at(t.get(), x, y, colour, 1);
    t->destroy();
}
}  // namespace

// The box over the race view before the start (0x444ae0), on a dark grey backdrop: the track's name,
// the race length (with a lap count also the distance, more players), and the track record: time
// "s.hh", and if set the holder (with the car, in the holder's colours when the holder races), the
// car's name and the date. One player sees the lap record (records slot 1), more players the race
// record (slot 0), in a box 26 / 28 units high.
void race_info_box() {
    race_draw_scene();
    guest::Local<uint8_t[0x400]> palette;
    palette_store(reinterpret_cast<uint32_t *>(*palette));
    const int u = g_font_size;
    const bool solo = g_player_count == 1;
    const int half_rows = solo ? 13 : 14;
    const int cx = g_screen_w / 2, cy = g_screen_h / 2;
    uint8_t grey[256];
    grey_table(*palette, grey);
    const int pitch = static_cast<int>(g_pitch);
    for (int row = (cy - u * half_rows) * pitch; row < (u * half_rows + cy) * pitch; row += pitch) {
        uint8_t *line = &guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(row));
        for (int x = cx - u * 10; x < cx + u * 10; x++) line[x] = grey[line[x]];
    }
    const int x = cx - u * 7, y = solo ? cy - u * 10 : cy - u * 11;

    // the track's name (after the last '\\')
    const int cur = g_current_track;
    const char *path = cur >= 0 && cur < g_selected_tracks.size() ? g_selected_tracks[cur].c_str() : nullptr;
    const char *name = std::strrchr(path, '\\') + 1;
    text_at(name, x, y, 0xf2);
    draw_text_at(&g_lang[solo ? 120 : 7], x, y + u * 4, 0xf5, 1);
    guest::Local<MsString> line;
    line->init();
    race_length_text(line.get());
    draw_text_at(line.get(), x, y + u * 6, 0xf2, 1);
    int ry = y + u * 10;  // the record block
    if (!solo) {
        int m = g_track_length * g_race_laps;
        uint32_t km = guest::call_cdecl(0x46f320, 0x49cd2cu /* "%d.%d%d%d km" */, m / 1000, m / 100 % 10, m / 10 % 10, m % 10);
        line->assign(&guest::at<char>(km), static_cast<uint32_t>(std::strlen(&guest::at<char>(km))));
        draw_text_at(line.get(), x, y + u * 8, 0xf2, 1);
        ry = y + u * 12;
    }
    const int rec = solo ? 1 : 0;
    draw_text_at(&g_lang[solo ? 121 : 8], x, ry, 0xf5, 1);
    const int time = g_record_time[rec];
    uint32_t ts = guest::call_cdecl(0x46f320, 0x49cd20u /* "%d.%d%ds" */, time / 100, time / 10 % 10, time % 10);
    line->assign(&guest::at<char>(ts), static_cast<uint32_t>(std::strlen(&guest::at<char>(ts))));
    draw_text_at(line.get(), x, ry + u * 2, 0xf2, 1);
    if (time < kGhostFrames) {
        const char *holder = g_record_name[rec];
        text_at(holder, x, ry + u * 4, 0xf2);
        int8_t colour = -1;  // the holder's player colours, if racing (the last player of that name)
        for (int p = 0; p < g_player_count; p++)
            if (ms_stricmp(holder, g_player_name[p]) == 0) colour = static_cast<int8_t>(p);
        draw_car_preview(static_cast<uint32_t>(colour), g_record_car[rec], u + x, u * 7 + ry, 120.0f);
        text_at(reinterpret_cast<const char *>(g_record_car[rec]), u * 3 + x, u * 6 + ry, 0xf2);
        guest::call_import(IMP_GetDateFormatA, 0u, 1u, guest::addr(&g_record_date[rec]), 0u, 0x4ab128u, 0x400u);
        text_at(&guest::at<char>(0x4ab128), x, ry + u * 8, 0xf2);
    }
    line->destroy();
    unlock_and_present();
}
