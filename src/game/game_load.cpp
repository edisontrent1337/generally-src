// Loading a saved game (saves\<name>.gam, written by game_save in replay.cpp).
#include <cstring>

#include "game/driver.h"
#include "game/file.h"
#include "game/fmath.h"
#include "game/ghost.h"
#include "game/gui.h"
#include "game/menu.h"
#include "game/race_options.h"
#include "game/race_state.h"
#include "game/render.h"
#include "game/replay.h"
#include "game/selected_tracks.h"
#include "game/sound.h"
#include "game/track.h"
#include "game/video.h"
#include "orig/sdl.h"
#include "bridge/imports.h"


static uint32_t malloc_guest(uint32_t n) { return guest::call_cdecl(0x496c14, n); }

static void free_global(uint32_t &p) {
    if (p) {
        guest::call_cdecl(0x496bd7, p);
        p = 0;
    }
}

static int32_t time_now() { return static_cast<int32_t>(guest::call_cdecl(guest::at<uint32_t>(IMP__time64), 0u)); }

// The race options of the file replace the current ones, except the fields the player sets in the
// options screens (volumes, view, palette and the like), which are kept.
static void read_race_options(GameFile *f) {
    uint8_t *o = reinterpret_cast<uint8_t *>(&g_race_options);
    uint8_t keep[0x68];
    std::memcpy(keep, o, sizeof keep);
    race_options_read(f, &g_race_options);
    static constexpr struct { uint8_t off, n; } kKept[] = {{0x0e, 1}, {0x10, 9}, {0x1a, 3}, {0x1e, 0x1c}};
    for (auto k : kKept) std::memcpy(o + k.off, keep + k.off, k.n);
}

// The drivers: each record replaces the list entry of the same name (or is added to the list) and the
// driver is put into the next player slot.
static void read_drivers(GameFile *f, int count) {
    const char *name = g_player_name[0];
    Widget *list = &g_menu_widgets.get()[16];
    for (int i = 0; i < count; i++, name += kNameLen) {
        Driver *d = static_cast<Driver *>(guest::op_new(sizeof(Driver)));
        driver_read(f, d);
        guest::Local<MsString> s;
        s->init();
        s->assign(name, static_cast<uint32_t>(std::strlen(name)));
        int found = list_find(g_driver_list.get(), s.get());
        s->destroy();
        if (found < 0) {
            s->init();
            s->assign(name, static_cast<uint32_t>(std::strlen(name)));
            list_add(list, s.get());
            s->destroy();
            ListData *l = g_driver_list.get();
            int last = l->items.size() - 1;
            l->selected = last;
            l->items[last]->data = guest::addr(d);
        } else {
            guest::op_delete(d, sizeof(Driver));
            g_driver_list->selected = found;
        }
        menu_add_driver(list);
    }
}

// The season's tracks. Older files (before 0x103) have 0x38-byte names only. Newer ones have a 40-byte
// name and the track file's checksum: if the current track's file has changed, a track with the
// checksum is looked for (0x43cd50); none: "\()" is selected and the replay is not loaded.
static bool read_tracks(GameFile *f, uint16_t version) {
    bool replay = true;
    guest::call_regs(0x4050e0, g_selected_tracks.first.a, g_selected_tracks.last.a, 0u, 0u);  // destroy the strings
    g_selected_tracks.last = g_selected_tracks.first;
    if (version < 0x103) {
        guest::Local<char[0x38]> name;
        for (int i = 0; i < g_track_count; i++) {
            file_read(f, *name, 0x38);
            selected_track_add(*name);
        }
        return replay;
    }
    guest::Local<char[40]> name;
    guest::Local<uint32_t[4]> sum;
    for (int i = 0; i < g_track_count; i++) {
        std::memset(*name, 0, 40);
        file_read(f, *name, 40);
        file_read(f, *sum, 16);
        if (i != g_current_track || track_checksum_matches(guest::addr(*name), *sum)) {
            selected_track_add(*name);
            continue;
        }
        guest::Local<MsString> found;
        found->init();
        if (track_find_by_checksum(*name, *sum, found.get())) {
            selected_track_add(found->c_str());
        } else {
            guest::Local<uint32_t> missing;
            *missing = 0x29285c;  // "\()"
            selected_track_add(reinterpret_cast<const char *>(missing.get()));
            replay = false;
        }
        found->destroy();
    }
    return replay;
}

// The race's work buffers, sized for the screen, and the random table (srand(SDL_GetTicks())).

// Loads saves\<name>.gam and continues the saved race (paused) or, when its track cannot be loaded,
// the next race of the season or the season's end. 0 when loaded, -1 if the file cannot be opened or
// has an unknown version (newer than 0x124, or 0x120).
int game_load(const MsString *name) {
    enter_game_dir();
    bool replay = true;
    guest::Local<GameFile> f;
    file_construct(f.get());
    guest::Local<MsString> dir, t1, t2, path;
    dir->init();
    dir->assign(&guest::at<char>(0x49cd08), 5);             // "saves"
    MsString::concat_move(t1.get(), dir.get(), 0x49ccfcu);  // + "\\"
    MsString::concat_move(t2.get(), t1.get(), name);
    MsString::concat_move(path.get(), t2.get(), 0x49cd10u);  // + ".gam"
    bool opened = file_open(f.get(), path.get(), FM_IN | FM_BINARY);
    path->destroy();
    t2->destroy();
    t1->destroy();
    dir->destroy();
    GameFile *fp = f.get();
    guest::Local<uint16_t> version;
    if (opened) {
        show_loading();
        file_read(fp, version.get(), 2);
    }
    if (!opened || *version > 0x124 || *version == 0x120) {
        file_close(fp);
        file_destruct(f.get());  // closes again
        return -1;
    }
    file_read(fp, &g_game_state, 1);
    file_read(fp, &g_race_substate, 1);
    read_race_options(fp);
    guest::Local<int32_t> time_left;
    *time_left = -1;
    file_read(fp, &g_timed_race, 1);
    file_read(fp, &g_unknown_4a4792, 1);
    file_read(fp, time_left.get(), 4);
    file_read(fp, &g_time_limit, 4);
    if (g_timed_race) {
        g_race_clock_start = *time_left - g_time_limit + time_now();
        g_race_clock_end = time_now() + *time_left;
        g_pause_clock = time_now();
    }
    g_pause_clock = time_now();
    guest::Local<int32_t> count;
    *count = 0;
    file_read(fp, count.get(), 4);
    file_read(fp, &g_player_name, static_cast<uint32_t>(*count * kNameLen));
    g_player_count = 0;
    read_drivers(fp, *count);
    car_models_read(fp, g_race_models[0], g_player_count);
    file_read(fp, &g_track_count, 4);
    file_read(fp, &g_current_track, 4);
    if (!read_tracks(fp, *version)) replay = false;
    car_states_read(fp);
    std::memcpy(&guest::at<uint8_t>(0x4a4eec), &guest::at<uint8_t>(0x49e130), 16);
    std::memcpy(&guest::at<uint8_t>(0x4a4efc), &guest::at<uint8_t>(0x49e130), 8);
    // a car driven by a human (keys set in the saved state) gets its driver's current controls
    for (int i = 0; i < g_player_count; i++) {
        uint8_t *car = g_car_states[i].raw;
        if (*reinterpret_cast<uint32_t *>(car + 0x48) == 0) continue;
        const Driver *d = g_player_driver[i].get();
        std::memcpy(car + 0x48, d->key, 16);
        std::memcpy(car + 0x18, d->joy, 48);
    }
    file_read(fp, &g_finish_order, static_cast<uint32_t>(g_player_count * 4));
    file_read(fp, &g_season_points, static_cast<uint32_t>(g_player_count * 4));
    file_read(fp, &g_race_laps, 4);
    file_read(fp, &g_leader_laps, 4);
    file_read(fp, &g_unknown_4a47f8, 4);
    file_read(fp, &g_fastest_lap, 4);
    file_read(fp, &g_unknown_4a4798, 4);
    file_read(fp, &g_unknown_4a479c, 4);
    if (replay) {
        if (g_player_count == 1) replay_load(fp);
        replay_read(fp);
    }
    file_close(fp);

    // set up the race as race_start does
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
    guest::Local<uint32_t[256]> pal;
    palette_store(*pal);
    for (int i = 0; i < g_player_count && i < 6; i++) {
        car_shades(*pal + 192 + 8 * i, g_player_driver[i]->color[0]);
        car_shades(*pal + 196 + 8 * i, g_player_driver[i]->color[1]);
    }
    palette_load(*pal);
    for (int i = 0; i < g_player_count; i++) {
        uint32_t s = g_engine_sound[i];
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
    if (track_load() == -1) {
        if (g_background) sdl::FillRect(g_background, nullptr, 100);
        if (g_current_track + 1 < g_track_count) {
            g_current_track = g_current_track + 1;
            race_load_track();
        } else {
            guest::call_regs(0x447c50, 0, 0);  // season end
        }
    } else {
        guest::call_regs(0x4486c0, 0, 0);
        if (g_game_state == 0) {
            race_info_box();
            pause_menu_open();
        } else {
            race_render();
            if (g_game_state < 5) pause_menu_open();
            else if (g_game_state == 5) guest::call_regs(0x446010, 0, 0);
            else guest::call_regs(0x4473e0, 0, 0);
        }
    }
    file_destruct(f.get());  // closes again
    return 0;
}
