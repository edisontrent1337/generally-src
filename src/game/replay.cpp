// Replay recording (see replay.h).
#include "game/replay.h"

#include <cstring>

#include "game/file.h"
#include "game/driver.h"
#include "game/ghost.h"
#include "game/race_options.h"
#include "game/selected_tracks.h"
#include "game/menu.h"
#include "game/race_state.h"
#include "game/file_dialog.h"
#include "game/sound.h"
#include "game/input.h"
#include "game/fmath.h"

// every field of a frame in file order, with its size
static constexpr struct { uint8_t off, size; } kFields[] = {
    {0, 2}, {2, 2}, {4, 2}, {6, 1}, {7, 1}, {8, 1}, {9, 1}, {10, 1}, {11, 1}, {12, 4}, {16, 1}, {17, 1},
};

// Reads g_replay_frames frames; returns the bytes read.
int replay_frames_read(GameFile *f, ReplayFrame *frames) {
    int total = 0;
    for (int32_t n = g_replay_frames; n != 0; n--, frames++)
        for (auto fl : kFields) total += file_read(f, reinterpret_cast<uint8_t *>(frames) + fl.off, fl.size);
    return total;
}

int replay_frames_write(GameFile *f, const ReplayFrame *frames, uint32_t count) {
    int total = 0;
    for (; count != 0; count--, frames++)
        for (auto fl : kFields) total += file_write(f, reinterpret_cast<const uint8_t *>(frames) + fl.off, fl.size);
    return total;
}

// Writes the recorded replay: a header (tag 0x50520124, the rate, the frame count), then per player with
// a buffer a chunk tagged "PLR1".."PLR6" with its frames, oldest first. With both range marks set only
// the marked range is written. The ring buffer wraps, so a chunk is written in up to two parts.
void replay_write(GameFile *f) {
    if (g_replay_frames <= 0) return;
    guest::Local<uint32_t> tag;
    guest::Local<int32_t> count;
    *tag = 0x50520124;
    file_write(f, tag.get(), 4);
    int start;
    if (g_replay_mark_in < 0.0f || g_replay_mark_out < 0.0f) {
        *count = g_replay_frames;
        start = g_replay_head;
    } else {
        *count = static_cast<int>(g_replay_mark_out) - static_cast<int>(g_replay_mark_in);
        start = (g_replay_head + static_cast<int>(g_replay_mark_in)) % g_replay_frames;
    }
    file_write(f, &g_replay_rate, 4);
    file_write(f, count.get(), 4);
    int wrapped = *count - g_replay_frames + start;  // frames after the wrap
    int first = g_replay_frames - start;
    if (*count < first) first = *count;
    for (uint32_t t = 0x31524c50, i = 0; t < 0x37524c50; t += 0x1000000, i++) {  // "PLR1".."PLR6"
        ReplayFrame *frames = reinterpret_cast<ReplayFrame *>(g_replay_buffers[i].get());
        if (!frames) continue;
        *tag = t;
        file_write(f, tag.get(), 4);
        replay_frames_write(f, frames + start, static_cast<uint32_t>(first));
        if (wrapped > 0) replay_frames_write(f, frames, static_cast<uint32_t>(wrapped));
    }
}

// Reads a replay written by replay_write: re-initialises the recording for the file's frame count and
// rate (files before version 0x104 have no rate: 0), then fills each player buffer from its "PLR<n>"
// chunk; frames come back with b11 one higher (it is stored decremented). A missing chunk is stepped back
// over and ends the replay there: that player's and all later buffers are freed (with the first missing,
// no frames remain). A file without the header (or newer than 0x124) gets an empty replay.
void replay_read(GameFile *f) {
    guest::Local<uint32_t[2]> head;  // version, "RP" (2 bytes each)
    (*head)[0] = (*head)[1] = 0;
    file_read(f, &(*head)[0], 2);
    file_read(f, &(*head)[1], 2);
    uint16_t version = static_cast<uint16_t>((*head)[0]), magic = static_cast<uint16_t>((*head)[1]);
    if (version == 0 || magic == 0) {
        replay_init(0, 0);
        return;
    }
    if (magic != 0x5052 || version > 0x124) {
        file_seek(f, -4, 1);
        replay_init(0, 0);
        return;
    }
    guest::Local<int32_t> rate, frames;
    *rate = 0;
    if (version > 0x103) file_read(f, rate.get(), 4);
    file_read(f, frames.get(), 4);
    replay_init(*frames, *rate);
    g_replay_frames = *frames;
    for (uint32_t t = 0x31524c50, i = 0; t < 0x37524c50; t += 0x1000000, i++) {  // "PLR1".."PLR6"
        if (!g_replay_buffers[i]) continue;
        guest::Local<uint32_t> tag;
        *tag = 0;
        file_read(f, tag.get(), 4);
        if (*tag != t) {
            file_seek(f, -4, 1);
            if (i == 0) g_replay_frames = 0;
            for (; i < 6; i++) {
                if (g_replay_buffers[i]) guest::call_cdecl(0x496bd7, g_replay_buffers[i]);  // free
                g_replay_buffers[i] = nullptr;
            }
            return;
        }
        ReplayFrame *buf = reinterpret_cast<ReplayFrame *>(g_replay_buffers[i].get());
        replay_frames_read(f, buf);
        for (int k = 0; k < g_replay_frames; k++) buf[k].b11++;
    }
}

// Saves the cars' positions, orientations and wheels (and the game state) for the replay, which starts
// from there.
void replay_save_start() {
    for (int p = 0; p < g_player_count; p++) {
        const uint8_t *car = g_car_states[p].raw;
        ReplayStartCar &s = g_replay_start_cars[p];
        std::memcpy(s.position, car + 0x58, sizeof s.position);
        std::memcpy(s.orientation, car + 0x7c, sizeof s.orientation);
        int layout = g_race_models[p][0xbd];
        for (int k = 0; k < 4; k++) std::memcpy(&s.wheels[k], car + 0xec + 0xc * (layout + k), 4);
        std::memcpy(&s.a0, car + 0xa0, 4);
    }
    g_replay_start_state = g_game_state;
}

// Puts the cars back where replay_save_start found them (and the game state). The wheels' world positions
// are recomputed from the car's position and orientation axes, except their height (y), which is restored.
void replay_restore_start() {
    for (int p = 0; p < g_player_count; p++) {
        uint8_t *car = g_car_states[p].raw;
        const ReplayStartCar &s = g_replay_start_cars[p];
        std::memcpy(car + 0x58, s.position, sizeof s.position);
        std::memcpy(car + 0x7c, s.orientation, sizeof s.orientation);
        auto f = [car](int off) { return *reinterpret_cast<const float *>(car + off); };
        int layout = g_race_models[p][0xbd];
        const float *local = reinterpret_cast<const float *>(&g_race_models[p][0x504]);
        for (int k = 0; k < 4; k++, local += 3) {
            float *wheel = reinterpret_cast<float *>(car + 0xe8 + 0xc * (layout + k));
            float x = f(0x58) + f(0x7c) * local[0] + f(0x88) * local[1] + f(0x94) * local[2];
            float y = f(0x5c) + f(0x80) * local[0] + f(0x8c) * local[1] + f(0x98) * local[2];
            float z = f(0x60) + f(0x84) * local[0] + f(0x90) * local[1] + f(0x9c) * local[2];
            wheel[0] = x;
            wheel[1] = y;
            wheel[2] = z;
            std::memcpy(&wheel[1], &s.wheels[k], 4);
        }
        std::memcpy(car + 0xa0, &s.a0, 4);
    }
    g_game_state = static_cast<uint8_t>(g_replay_start_state);
}

static void zero_words(uint8_t *base, uint32_t off, int n) {
    for (int i = 0; i < n; i++) reinterpret_cast<uint32_t *>(base + off)[i] = 0;
}

// CarState's constructor (run for the six cars by a static initializer): zeroes the vectors
// +0x58..+0x9f (position, velocity, orientation, ...) and the 14 wheel / contact points at +0xe8.
CarState *car_state_construct(CarState *s) {
    zero_words(s->raw, 0x58, 18);
    zero_words(s->raw, 0xe8, 14 * 3);
    return s;
}

// The car model record's constructor (0x540 bytes): zeroes its vector tables (30 at +0x180, 30 at +0x2e8,
// 15 at +0x450) and the five vectors at +0x504 (the wheel offsets and the two after them).
uint8_t *car_model_construct(uint8_t *m) {
    zero_words(m, 0x180, 30 * 3);
    zero_words(m, 0x2e8, 30 * 3);
    zero_words(m, 0x450, 15 * 3);
    zero_words(m, 0x504, 15);
    return m;
}

// PitBox's constructor (the track loader makes g_pit_count of them): zeroes the vectors at +0x4c..+0x183.
PitBox *pit_box_construct(PitBox *b) {
    zero_words(reinterpret_cast<uint8_t *>(b), 0x4c, (0x184 - 0x4c) / 4);
    return b;
}

// The car states' fields as saved (offset, size); kPad writes that many zero bytes (the struct's padding).
namespace {
constexpr int16_t kPad = -1;
struct CarField { int16_t off; uint8_t size; uint8_t count; };  // count fields of size, back to back
constexpr CarField kCarFields[] = {
    {0x00, 1, 4}, {0x04, 5, 1}, {kPad, 3, 1}, {0x0c, 4, 3}, {0x48, 4, 4}, {0x18, 4, 2},
    {0x20, 1, 3}, {kPad, 1, 1}, {0x24, 4, 2}, {0x2c, 1, 3}, {kPad, 1, 1}, {0x30, 4, 2},
    {0x38, 1, 3}, {kPad, 1, 1}, {0x3c, 4, 2}, {0x44, 1, 3}, {kPad, 1, 1},
    {0x58, 4, 18},  // 0x58..0x9c
    {0xa0, 4, 1}, {0xa4, 20, 1}, {0xb8, 4, 1}, {0xbc, 8, 1}, {0xc4, 4, 4}, {0xd4, 2, 2},
    {0xd8, 1, 3}, {0xdc, 2, 4}, {0xe4, 4, 1},
    {0xe8, 4, 42},  // 0xe8..0x18c
    {0x190, 1, 1},
};
}  // namespace

// Writes the state of every car in the race field by field; returns the bytes written.
int car_states_write(GameFile *f) {
    guest::Local<uint32_t> zero;
    *zero = 0;
    int total = 0;
    for (int i = 0; i < g_player_count; i++) {
        uint8_t *car = g_car_states[i].raw;
        for (const CarField &fd : kCarFields)
            for (int k = 0; k < fd.count; k++)
                total += file_write(f, fd.off == kPad ? static_cast<void *>(zero.get()) : car + fd.off + k * fd.size, fd.size);
    }
    return total;
}

// Reads the car states written by car_states_write (the padding is skipped); returns the bytes read
// plus 1 for each successful skip.
int car_states_read(GameFile *f) {
    int total = 0;
    for (int i = 0; i < g_player_count; i++) {
        uint8_t *car = g_car_states[i].raw;
        for (const CarField &fd : kCarFields)
            for (int k = 0; k < fd.count; k++)
                total += fd.off == kPad ? file_skip(f, fd.size) : file_read(f, car + fd.off + k * fd.size, fd.size);
    }
    return total;
}

// Saves the game in progress as saves\<name>.gam: version 0x124 (2 bytes), game state, race options,
// the time left of a timed race, the drivers and their cars, the season's tracks (40-byte name and the
// track file's checksum each), the car states, results so far, the ghost (time trial) and the replay.
void game_save(const MsString *name) {
    enter_game_dir();
    guest::call_cdecl(guest::at<uint32_t>(IMP__mkdir), 0x49cd08u);  // "saves"
    guest::Local<GameFile> f;
    file_construct(f.get());
    guest::Local<MsString> dir, t1, t2, path;
    dir->init();
    dir->assign(&guest::at<char>(0x49cd08), 5);             // "saves"
    MsString::concat_move(t1.get(), dir.get(), 0x49ccfcu);  // + "\\"
    MsString::concat_move(t2.get(), t1.get(), name);
    MsString::concat_move(path.get(), t2.get(), 0x49cd10u);  // + ".gam"
    bool opened = file_open(f.get(), path.get(), FM_OUT | FM_TRUNC | FM_BINARY);
    path->destroy();
    t2->destroy();
    t1->destroy();
    dir->destroy();
    if (opened) {
        GameFile *fp = f.get();
        guest::Local<uint32_t> version;
        *version = 0x124;
        file_write(fp, version.get(), 2);
        file_write(fp, &g_game_state, 1);
        file_write(fp, &g_race_substate, 1);
        race_options_write(fp);
        guest::Local<int32_t> time_left;
        *time_left = g_race_clock_start <= 0 ? g_time_limit : g_race_clock_end - g_pause_clock;
        file_write(fp, &g_timed_race, 1);
        file_write(fp, &g_unknown_4a4792, 1);
        file_write(fp, time_left.get(), 4);
        file_write(fp, &g_time_limit, 4);
        file_write(fp, &g_player_count, 4);
        file_write(fp, &g_player_name, static_cast<uint32_t>(g_player_count * 13));
        for (int i = 0; i < g_player_count; i++) driver_write(fp, g_player_driver[i].get());
        car_models_write(fp, g_race_models[0], g_player_count);
        file_write(fp, &g_track_count, 4);
        file_write(fp, &g_current_track, 4);
        for (int i = 0; i < g_track_count; i++) {
            guest::Local<char[40]> track;
            std::memset(*track, 0, 40);
            std::strcpy(*track, g_selected_tracks[i].c_str());
            file_write(fp, *track, 40);
            guest::Local<uint32_t[4]> sum;
            std::memset(*sum, 0, 16);
            guest::Local<MsString> tname;
            tname->init();
            tname->assign(*track, static_cast<uint32_t>(std::strlen(*track)));
            track_file_checksum(tname.get(), *sum);
            tname->destroy();
            file_write(fp, *sum, 16);
        }
        car_states_write(fp);
        file_write(fp, &g_finish_order, static_cast<uint32_t>(g_player_count * 4));
        file_write(fp, &g_season_points, static_cast<uint32_t>(g_player_count * 4));
        file_write(fp, &g_race_laps, 4);
        file_write(fp, &g_leader_laps, 4);
        file_write(fp, &g_unknown_4a47f8, 4);
        file_write(fp, &g_fastest_lap, 4);
        file_write(fp, &g_unknown_4a4798, 4);
        file_write(fp, &g_unknown_4a479c, 4);
        if (g_player_count == 1) replay_save(fp);
        replay_write(fp);
        file_truncate_here(fp);
    }
    file_close(f.get());
    file_destruct(f.get());
}

// Mutes a sound directly: volume = 128 / 10^(10000 / 2000), i.e. -10000 millibels, as sound_set_volume
// would compute it (the result is 0).
static void sound_mute(uint32_t handle) {
    if (SoundRef *s = sound_ref(handle))
        s->volume = guest::to_int(128.0 / static_cast<double>(guest_powf(10.0f, 5.0f)) + 0.5);
}

static float replay_speed(float direction) {
    return static_cast<float>(g_replay_speed_num) * direction / static_cast<float>(g_replay_speed_den);
}

// The replay viewer's keys, once per frame (0x407520; game state 10). Returns 0 when the race state was
// left for the save dialog, else 1.
//   Escape          speed back to 1:1, the engines muted, game state 11 (leave the replay)
//   C               first press marks the start of the range to save, the second its end; then the
//                   race is restored, the pause menu opened and the Save dialog shown
//   Page Down / Page Up held   rewind / fast forward: speed num * -4 / den or num * 4 / den, back to
//                   normal on release
//   keypad + / -    speed ratio num:den one step faster / slower (1:8 .. 8:1)
//   Left / Down / Right   backwards / stop / forwards; Home / End: to the first / last frame
// Then the replay advances (0x48e220, returns the time shown, in frames), each engine sound follows
// its car (the recorded volume only at normal speed forwards; pitch from the engine speed, muffled
// under water) and the HUD texts are formatted.
int replay_controls() {
    const uint8_t *keys = g_keys.get();
    if (keys[0x1b] & 1) {  // Escape
        g_replay_speed_den = 1;
        g_replay_speed_num = 1;
        g_replay_direction = 1;
        g_replay_mark_state = 0;
        g_replay_slow_held = 0;
        g_replay_fast_held = 0;
        g_replay_slower_held = 0;
        g_replay_faster_held = 0;
        g_replay_speed = 1.0f;
        for (int i = 0; i < g_player_count; i++)
            if (g_engine_sound[i]) sound_mute(g_engine_sound[i]);
        g_game_state = 0xb;
        return 1;
    }
    bool mark = keys[0x63] & 1;  // C
    if (mark && g_replay_mark_state != 1) {
        if (g_replay_mark_state != 0) {
            g_replay_mark_out = g_replay_pos;
            replay_restore_start();
            game_resume();
            pause_menu_open();
            file_dialog_open(&g_pause_widgets.get()[3]);  // Save
            g_replay_mark_state = 0;
            return 0;
        }
        g_replay_mark_in = g_replay_pos;
        g_replay_mark_state = 1;
    } else if (!mark && g_replay_mark_state == 1) {
        g_replay_mark_state = 2;
    }
    bool slow = keys[0x119] & 1;  // Page Down
    if (slow && !g_replay_slow_held) {
        g_replay_slow_held = 1;
        g_replay_speed = replay_speed(-4.0f);
    } else if (!slow && g_replay_slow_held) {
        g_replay_speed = replay_speed(static_cast<float>(g_replay_direction));
        g_replay_slow_held = 0;
    }
    bool fast = keys[0x118] & 1;  // Page Up
    if (fast && !g_replay_fast_held) {
        g_replay_fast_held = 1;
        g_replay_speed = replay_speed(4.0f);
    } else if (!fast && g_replay_fast_held) {
        g_replay_fast_held = 0;
        g_replay_speed = replay_speed(static_cast<float>(g_replay_direction));
    }
    if (keys[0x10e] & 1) {  // keypad +
        if (!g_replay_faster_held && g_replay_speed_num < 8) {
            if (g_replay_speed_den == 1) g_replay_speed_num = g_replay_speed_num + 1;
            else g_replay_speed_den = g_replay_speed_den - 1;
            g_replay_speed = replay_speed(static_cast<float>(g_replay_direction));
        }
        g_replay_faster_held = 1;
    } else {
        g_replay_faster_held = 0;
    }
    if (keys[0x10d] & 1) {  // keypad -
        if (!g_replay_slower_held && g_replay_speed_den < 8) {
            if (g_replay_speed_num == 1) g_replay_speed_den = g_replay_speed_den + 1;
            else g_replay_speed_num = g_replay_speed_num - 1;
            g_replay_speed = replay_speed(static_cast<float>(g_replay_direction));
        }
        g_replay_slower_held = 1;
    } else {
        g_replay_slower_held = 0;
    }
    int num = g_replay_speed_num, den = g_replay_speed_den;
    if (keys[0x114] & 1) {  // Left
        g_replay_direction = -1;
        g_replay_speed = replay_speed(-1.0f);
    }
    if (keys[0x112] & 1) {  // Down
        g_replay_direction = 0;
        g_replay_speed = replay_speed(0.0f);
    }
    if (keys[0x113] & 1) {  // Right
        g_replay_direction = 1;
        g_replay_speed = static_cast<float>(g_replay_speed_num) / static_cast<float>(g_replay_speed_den);
    }
    if (keys[0x116] & 1) g_replay_pos = 0.0f;                                                     // Home
    if (keys[0x117] & 1) g_replay_pos = static_cast<float>(g_replay_frames) - g_replay_step;  // End

    int shown = replay_advance();
    for (int i = 0; i < g_player_count; i++) {
        float engine = car_engine(i);
        float pitch = engine > 0.0f ? engine * 4.0f + 50.0f : 50.0f - engine * 4.0f;
        pitch = pitch / *reinterpret_cast<const float *>(g_race_models[i] + 0xa8);
        uint32_t snd = g_engine_sound[i];
        if (!snd) continue;
        if (g_replay_direction == 1 && num == 1 && den == 1 && !g_replay_slow_held && !g_replay_fast_held && shown >= 0)
            sound_set_volume(snd, guest::to_int(g_unknown_4a47f4) - 10000);
        else
            sound_mute(snd);
        if (SoundRef *s = sound_ref(snd)) {
            float scale = g_water_level > car_height(i) ? 5000.0f : 10000.0f;  // under water: an octave lower
            s->rate = static_cast<float>(guest::to_int(pitch * scale)) / 22050.0f;
        }
    }

    // the HUD: time (frames as h:mm:ss, or m:ss) and speed
    if (shown < 0) shown = -1 - shown;
    int minutes = shown / 60, hours = minutes / 60;
    uint32_t s;
    if (hours == 0) s = guest::call_cdecl(0x46f320, 0x49cc98u, minutes % 60, shown % 60);
    else s = guest::call_cdecl(0x46f320, 0x49cca4u, hours, minutes % 60, shown % 60);
    g_replay_clock_text.assign(&guest::at<char>(s), static_cast<uint32_t>(std::strlen(&guest::at<char>(s))));
    s = guest::call_cdecl(0x46f320, 0x49ccb4u, g_replay_direction < 0 ? 0x2du : 0x20u, g_replay_speed_num, g_replay_speed_den);
    g_replay_speed_text.assign(&guest::at<char>(s), static_cast<uint32_t>(std::strlen(&guest::at<char>(s))));
    return 1;
}

namespace {
// One recorded replay frame of a car, decoded (0x12 bytes in the ring buffer: position x150 as u16,
// two direction vectors x120 as i8, four wheel heights x16 as u8, one more signed byte).
struct ReplaySample {
    float pos[3];
    float a[3];       // first direction (unit)
    float c[3];       // b x a
    float b[3];       // second direction (unit)
    float wheel[4];   // wheel heights
    float extra;
};
static_assert(sizeof(ReplaySample) == 0x44);

void replay_decode(const uint8_t *r, ReplaySample &s) {
    auto u16 = [r](int o) { return static_cast<float>(static_cast<int>(*reinterpret_cast<const uint16_t *>(r + o))); };
    auto i8 = [r](int o) { return static_cast<float>(static_cast<int>(static_cast<int8_t>(r[o]))); };
    s.pos[0] = u16(0) / 150.0f;
    s.pos[1] = u16(2) / 150.0f;
    s.pos[2] = u16(4) / 150.0f;
    float ax = i8(6) / 120.0f, ay = i8(7) / 120.0f, az = i8(8) / 120.0f;
    float bx = i8(9) / 120.0f, by = i8(10) / 120.0f, bz = i8(11) / 120.0f;
    float la = guest_sqrtf((ax * ax + ay * ay) + az * az);
    ax = ax / la;
    ay = ay / la;
    az = az / la;
    s.a[0] = ax;
    s.a[1] = ay;
    s.a[2] = az;
    float lb = guest_sqrtf((by * by + bx * bx) + bz * bz);
    bx = bx / lb;
    bz = bz / lb;
    by = by / lb;
    s.b[0] = bx;
    s.b[1] = by;
    s.b[2] = bz;
    s.c[0] = by * az - bz * ay;
    s.c[1] = ax * bz - bx * az;
    s.c[2] = bx * ay - ax * by;
    for (int k = 0; k < 4; k++) s.wheel[k] = static_cast<float>(static_cast<int>(r[0xc + k])) * 0.0625f;
    s.extra = i8(0x10);
}
}  // namespace

// Shows the replay at g_replay_pos and advances it by g_replay_speed * g_replay_step (0x48e220). Each car
// is put between the recorded frames around the position: its centre on a cubic curve through the
// four nearest frames, orientation, wheel heights and +0xa0 blended linearly between the two nearest.
// Returns the time shown in hundredths of a second, or -1 - that at the end (-1 at the start).
int replay_advance() {
    const int n = g_replay_frames;
    const int head = g_replay_head;
    const int last = head > 0 ? head - 1 : n - 1;
    int cur = (guest::to_int(g_replay_pos) + head) % n;
    int next = cur != last ? cur + 1 : cur;
    int prev = cur != head ? cur - 1 : head;
    int next2 = next != last ? next + 1 : next;
    const float f = g_replay_pos - static_cast<float>(guest::to_int(g_replay_pos));
    const float g = 1.0f - f, g2 = g * g, f2 = f * f;
    if (prev < 0) prev += n;
    if (next >= n) next = 0;
    if (next2 >= n) next2 -= n;
    const int idx[4] = {prev, cur, next, next2};
    for (int i = 0; i < g_player_count; i++) {
        const uint8_t *buf = g_replay_buffers[i].get();
        if (!buf) continue;
        ReplaySample s[4];
        for (int k = 0; k < 4; k++) replay_decode(buf + 0x12 * idx[k], s[k]);
        uint8_t *st = g_car_states[i].raw;
        auto F = [st](int off) -> float & { return *reinterpret_cast<float *>(st + off); };
        for (int k = 0; k < 3; k++) {
            float p0 = s[0].pos[k], p1 = s[1].pos[k], p2 = s[2].pos[k], p3 = s[3].pos[k];
            float t1 = (p1 + (p2 - p0) / 6.0f) * 3.0f * g2 * f;
            float t2 = (p2 + (p1 - p3) / 6.0f) * 3.0f * g * f2;
            F(0x58 + 4 * k) = p2 * f2 * f + (t2 + (p1 * g2 * g + t1));
        }
        auto blend = [&](const float &v1, const float &v2) { return v1 * g + v2 * f; };
        for (int k = 0; k < 3; k++) {
            F(0x7c + 4 * k) = blend(s[1].a[k], s[2].a[k]);
            F(0x88 + 4 * k) = blend(s[1].c[k], s[2].c[k]);
            F(0x94 + 4 * k) = blend(s[1].b[k], s[2].b[k]);
        }
        // the wheels: placed by the car's pose, at the recorded heights
        const uint8_t *model = g_race_models[i];
        const float *wm = reinterpret_cast<const float *>(model + 0x504);
        float *w = reinterpret_cast<float *>(st + 0xe8) + 3 * model[0xbd];
        for (int k = 0; k < 4; k++) {
            float wx = wm[3 * k], wy = wm[3 * k + 1], wz = wm[3 * k + 2];
            float x = wz * F(0x94) + ((wx * F(0x7c) + F(0x58)) + wy * F(0x88));
            float y = wz * F(0x98) + ((wx * F(0x80) + F(0x5c)) + wy * F(0x8c));
            float z = wz * F(0x9c) + ((wx * F(0x84) + F(0x60)) + wy * F(0x90));
            w[3 * k] = x;
            w[3 * k + 1] = blend(s[1].wheel[k], s[2].wheel[k]);
            w[3 * k + 2] = z;
            (void)y;  // (computed by the original too, then replaced by the recorded height)
        }
        F(0xa0) = blend(s[1].extra, s[2].extra);
    }
    float pos = g_replay_speed * g_replay_step + g_replay_pos;
    g_replay_pos = pos;
    if (0.0f > pos) {
        g_replay_pos = 0.0f;
        return -1;
    }
    if (!(pos < static_cast<float>(n - 1))) {
        float end = static_cast<float>(n) - 1.0f;
        g_replay_pos = end;
        return -1 - guest::to_int(end * 0.01f / g_replay_step);
    }
    return guest::to_int(pos * 0.01f / g_replay_step);
}

GLOBAL(float, g_replay_speed_max, 0x4ab734);  // highest and lowest car speed recorded
GLOBAL(float, g_replay_speed_min, 0x4a4070);

// Records one replay frame every g_replay_rate race ticks (0x48df20; the countdown is a function-local
// static, first set to the rate). Each car's slot of the ring buffers (0x12 bytes) holds: position * 150
// as three int16, the two orientation vectors (+0x7c, +0x94) * 120 as int8, the four wheels' suspension
// values (clamped to 0..15.96) * 16 as bytes, the speed truncated, and a zero. The ring grows to its
// capacity, then its oldest frame moves on.
void replay_record() {
    int32_t &countdown = guest::at<int32_t>(0x4ab73c);
    guest::static_init(0x4ab738, [&] { countdown = g_replay_rate; });
    countdown++;
    if (countdown < g_replay_rate) return;
    const int frame = (g_replay_frames + g_replay_head) % g_replay_capacity;
    countdown = 0;
    float vmax = g_replay_speed_max, vmin = g_replay_speed_min;
    if (g_player_count > 0) {
        for (int p = 0; p < g_player_count; p++) {
            uint8_t *rec = g_replay_buffers[p].get();
            if (!rec) continue;
            rec += frame * 0x12;
            const uint8_t *car = g_car_states[p].raw;
            const float *f = reinterpret_cast<const float *>(car);
            auto *pos = reinterpret_cast<int16_t *>(rec);
            for (int i = 0; i < 3; i++) pos[i] = static_cast<int16_t>(guest::to_int(f[0x58 / 4 + i] * 150.0f + 0.5f));
            for (int i = 0; i < 3; i++) rec[6 + i] = static_cast<uint8_t>(guest::to_int(f[0x7c / 4 + i] * 120.0f + 0.5f));
            for (int i = 0; i < 3; i++) rec[9 + i] = static_cast<uint8_t>(guest::to_int(f[0x94 / 4 + i] * 120.0f + 0.5f));
            rec[0x10] = static_cast<uint8_t>(guest::to_int(f[0xa0 / 4]));
            const int first = g_race_models[p][0xbd];  // the model's first wheel
            for (int i = 0; i < 4; i++) {
                float s = *reinterpret_cast<const float *>(car + 0xec + (first + i) * 0xc);
                if (0.0f > s) s = 0.0f;
                if (s > 15.96f) s = 15.96f;
                rec[0xc + i] = static_cast<uint8_t>(guest::to_int(s * 16.0f + 0.5f));
            }
            rec[0x11] = 0;
            const float v = f[0xa0 / 4];
            if (v > vmax) vmax = v;
            if (vmin > v) vmin = v;
        }
        g_replay_speed_min = vmin;
        g_replay_speed_max = vmax;
    }
    if (g_replay_frames < g_replay_capacity) {
        g_replay_frames = g_replay_frames + 1;
    } else {
        g_replay_head = g_replay_head + 1;
        if (g_replay_head >= g_replay_capacity) g_replay_head = 0;
    }
}
