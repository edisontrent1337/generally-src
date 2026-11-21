// A car crossing the finish line (0x40dbe0): lap times, the running order, fuel, records and the
// race HUD's place / lap / time boxes along the bottom of the screen.
#include <cstdint>
#include <cstring>

#include "game/ghost.h"
#include "game/gui.h"
#include "game/menu.h"
#include "game/race_options.h"
#include "game/race_state.h"
#include "game/sound.h"
#include "game/text.h"
#include "game/track.h"
#include "game/video.h"
#include "bridge/imports.h"

namespace {
// Fields of g_car_states[i] used here.
struct Car {
    uint8_t *s;
    explicit Car(int i) : s(g_car_states[i].raw) {}
    uint8_t &place() const { return s[0]; }  // 0 = leading
    uint8_t &state() const { return s[2]; }  // 0 racing, 1 short of fuel ("F"), 2 finished
    int32_t &lap_start() const { return *reinterpret_cast<int32_t *>(s + 0x10); }  // frame of the last crossing
    int32_t &lap_time() const { return *reinterpret_cast<int32_t *>(s + 0x14); }   // frames (at most 6000)
    int32_t human() const { return *reinterpret_cast<int32_t *>(s + 0x48); }
    uint32_t ai_level() const { return *reinterpret_cast<uint32_t *>(s + 0x4c); }
    uint16_t &laps() const { return *reinterpret_cast<uint16_t *>(s + 0xd4); }
    uint16_t &best_lap() const { return *reinterpret_cast<uint16_t *>(s + 0xdc); }
    uint16_t fuel() const { return *reinterpret_cast<uint16_t *>(s + 0xe0); }
    // records only count for humans and for computer drivers up to level 100
    bool counts_for_records() const { return human() != 0 || ai_level() <= 100; }
};

// The running order as the original indexes it: order_at(k) is g_finish_order[k - 1] (k = 0 reads the
// word before the array).
int32_t order_at(int k) { return guest::at<int32_t>(0x4a47bcu + 4u * static_cast<uint32_t>(k)); }

int64_t time_now64() {
    uint32_t lo = guest::call_cdecl(guest::at<uint32_t>(IMP__time64), 0u);
    CPU *c = guest::cpu();
    return static_cast<int64_t>((static_cast<uint64_t>(EDX) << 32) | lo);
}

uint8_t *pixel_row(int row) { return &guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(row * static_cast<int>(g_pitch))); }

// Left edge of player p's HUD column: (p + 2) * width / 8.
int column_x(int p) { return guest::to_int(static_cast<float>((p + 2) * g_screen_w) * 0.125f); }

// Draws a short C string (host or guest memory: it is copied into guest memory first).
void draw_cstr(const char *s, int x, int y, uint8_t colour, uint8_t shadow) {
    guest::Local<char[16]> copy;
    std::strncpy(*copy, s, 15);
    guest::Local<MsString> t;
    t->init();
    t->assign(*copy, static_cast<uint32_t>(std::strlen(*copy)));
    draw_text_at(t.get(), x, y, colour, shadow);
    t->destroy();
}

// Player o's place box: cleared (colour 0xf1, 2 units wide) and the place in "%-2d".
void draw_place(int o) {
    const int u = g_font_size, b = g_menu_border, h = g_screen_h;
    const int x0 = column_x(o);
    for (int row = h - b * 3 - u * 4; row < h + (-b * 3 - u * 2); row++) std::memset(pixel_row(row) + u * 4 + x0, 0xf1, u * 2);
    guest::Local<char[16]> buf;
    guest::call_cdecl(0x46f350, guest::addr(buf.get()), 0x49ccbcu, static_cast<int>(Car(o).place()) + 1);  // "%-2d"
    draw_cstr(*buf, u * 4 + x0, h - b * 3 - u * 4, 0xf5, 0);
}

// "s.hh" of t frames as "dd.dd".
void format_time(char out[6], int t) {
    out[0] = static_cast<char>('0' + t / 1000 % 10);
    out[1] = static_cast<char>('0' + t / 100 % 10);
    out[2] = '.';
    out[3] = static_cast<char>('0' + t / 10 % 10);
    out[4] = static_cast<char>('0' + t % 10);
    out[5] = 0;
}

// Player p's lap is the race record (slot 0): time, holder, the car and the date.
void save_record(int p, const Car &car) {
    g_record_time[0] = static_cast<uint16_t>(car.lap_time());
    g_unknown_4a479c = p;
    std::strcpy(g_record_name[0], g_player_name[p]);
    std::memcpy(g_record_car[0], g_race_models[p], 0x540);
    guest::Local<SystemTime> now;
    guest::call_import(IMP_GetSystemTime, guest::addr(now.get()));
    g_record_date[0] = *now;
}
}  // namespace

// Player p crossed the finish line. The lap time (frames since the last crossing, at most 6000) and the
// lap count; a car with more laps than the leader becomes the leader (the others move down a place),
// else it overtakes the cars with fewer laps. A new leader lap ends a timed race's extra laps while the
// clock runs (lap count + 1), else marks the final lap; the leader's last lap cheers. Then the HUD: the
// lap counter "n/laps", the player's fuel box (with "F" when the fuel cannot last, out of the last two
// laps, for some lap distance factor), its lap number, and the lap time (the fastest of the race in
// colour 0xf4, a race record saves name, car and date). When the leader has finished, the car finishes
// (state 2, its best lap shown); when all have, the race ends. The last lap starts with a high beep.
void lap_line_crossed(int p) {
    Car car(p);
    if (car.state() >= 2) return;
    if (g_player_count == 1 && Car(0).counts_for_records()) ghost_lap_done();
    int lap = g_unknown_4a47f8 - car.lap_start();
    car.lap_start() = g_unknown_4a47f8;
    car.lap_time() = lap > 6000 ? 6000 : lap;
    const uint16_t laps = static_cast<uint16_t>(car.laps() + 1);
    car.laps() = laps;

    if (static_cast<int>(laps) > g_leader_laps) {
        g_leader_laps = laps;
        for (int k = car.place(); k > 0; k--) {
            int o = order_at(k);
            g_finish_order[k] = o;
            Car(o).place()++;
            if (Car(o).state() == 0) draw_place(o);
        }
        g_finish_order[0] = p;
        car.place() = 0;
        if (time_now64() < static_cast<int64_t>(g_race_clock_end) && g_timed_race) g_race_laps = g_race_laps + 1;
        else g_unknown_4a4792 = 1;
        if (g_leader_laps == g_race_laps && g_cheer_sound) sound_play(g_cheer_sound);
    } else {
        int k = car.place();
        while (laps > Car(order_at(k)).laps()) {
            int o = order_at(k);
            g_finish_order[k] = o;
            Car(o).place()++;
            if (Car(o).state() == 0) draw_place(o);
            k--;
        }
        g_finish_order[k] = p;
        car.place() = static_cast<uint8_t>(k);
    }

    const int u = g_font_size, b = g_menu_border, h = g_screen_h;
    // the lap counter, centred at a quarter of the width over a cleared box
    {
        const int n = g_leader_laps + 1 > g_race_laps ? g_race_laps : g_leader_laps + 1;
        uint32_t s = guest::call_cdecl(0x46f320, 0x49ccc4u, n, g_race_laps);  // "%d/%d"
        guest::Local<MsString> t;
        t->init();
        t->assign(&guest::at<char>(s), static_cast<uint32_t>(std::strlen(&guest::at<char>(s))));
        const uint32_t len = t->size;
        const float q = static_cast<float>(g_screen_w) * 0.25f, fu = static_cast<float>(u);
        const float lenf = static_cast<float>(static_cast<double>(len));
        const int x = guest::to_int(q - (lenf + 5.0f) * fu);
        const int start = h - (u * 2 + b) * 2, end = h - (u + b) * 2;
        for (int row = start; row < end; row++)
            std::memset(pixel_row(u * 2 + b + row) + u * 4 + x, 0xf0, (len + 1) * static_cast<uint32_t>(u));
        draw_text_at(t.get(), guest::to_int(q - (lenf + 0.5f) * fu), (h - (b + u) * 2) & 0xffff, 0xf2, 0xf1);
        t->destroy();
    }

    // the player's own boxes: fuel / place and lap time
    const int x0 = column_x(p);
    for (int row = h - b * 3 - u * 4; row < h - b * 3 - u * 2; row++) {
        std::memset(pixel_row(row) + u * 4 + x0, 0xf1, u * 5);
        std::memset(pixel_row(u * 2 + b + row) + u * 4 + x0, 0xf1, u * 5);
    }
    const int top = h - b * 3 - u * 4, bottom = h - (u + b) * 2;
    const uint32_t need = static_cast<uint32_t>(g_race_options.b0a) * static_cast<uint32_t>(g_track_length);
    if (g_race_laps - static_cast<int>(car.laps()) > 1 && g_leader_laps < g_race_laps &&
        static_cast<int>(car.fuel()) < guest::to_int(static_cast<float>(static_cast<double>(need)) * 1.8f)) {
        car.state() = 1;
        draw_cstr("F ", u * 4 + x0, top, 0xf4, 0);
        if (car.human() != 0 && g_fuel_sound) sound_play(g_fuel_sound);
    } else {
        car.state() = 0;
        draw_place(p);
    }

    // the lap number, 3 characters
    char num[4];
    const unsigned n = car.laps();
    num[0] = n < 100 ? ' ' : static_cast<char>('0' + n / 100 % 10);
    num[1] = n < 10 ? ' ' : static_cast<char>('0' + n / 10 % 10);
    num[2] = static_cast<char>('0' + n % 10);
    num[3] = 0;
    if (car.lap_time() < static_cast<int>(car.best_lap())) car.best_lap() = static_cast<uint16_t>(car.lap_time());

    char t[6];
    const int holder = g_unknown_4a4798;  // player with the race's fastest lap so far (-1: none)
    if (g_leader_laps >= g_race_laps) {
        // the leader has finished: so does this car
        draw_cstr(num, x0 + u * 6, top, 0xf5, 0);
        if (car.counts_for_records() && car.lap_time() < static_cast<int>(g_record_time[0])) save_record(p, car);
        format_time(t, car.best_lap());
        if (car.lap_time() < g_fastest_lap) {
            draw_cstr(t, x0 + u * 4, bottom, 0xf4, 0);
            if (holder >= 0 && holder != p) {
                char o[6];
                format_time(o, Car(holder).lap_time());
                draw_cstr(o, u * 4 - guest::to_int(static_cast<float>((holder + 2) * g_screen_w) * -0.125f), bottom,
                          Car(holder).state() < 2 ? 0xf2 : 0xf5, 0);
            }
            g_fastest_lap = car.lap_time();
            g_unknown_4a4798 = p;
        } else {
            draw_cstr(t, x0 + u * 4, bottom, holder == p ? 0xf4 : 0xf5, 0);
        }
        car.lap_time() = car.best_lap();
        car.state() = 2;
        int finished = 0;
        for (int i = 0; i < g_player_count; i++) finished += Car(i).state() == 2;
        if (finished == g_player_count) {
            g_game_state = 3;
            g_race_substate = 0;
        }
        return;
    }

    draw_cstr(num, x0 + u * 6, top, 0xf2, 0);
    format_time(t, car.lap_time());
    const bool record = car.counts_for_records() && car.lap_time() < static_cast<int>(g_record_time[0]);
    if (car.lap_time() < g_fastest_lap) {
        if (record) save_record(p, car);
        draw_cstr(t, x0 + u * 4, bottom, record ? 0xf4 : 0xf7, 0);
        if (holder >= 0 && holder != p) {
            char o[6];
            format_time(o, Car(holder).lap_time());
            draw_cstr(o, u * 4 - guest::to_int(static_cast<float>((holder + 2) * g_screen_w) * -0.125f), bottom, 0xf2, 0);
        }
        g_fastest_lap = car.lap_time();
        g_unknown_4a4798 = p;
    } else {
        if (record) save_record(p, car);
        draw_cstr(t, x0 + u * 4, bottom, 0xf2, 0);
    }
    // the last lap: the start beep, higher
    const uint32_t beep = g_start_sound;
    if (static_cast<int>(car.laps()) == g_race_laps - 1 && beep) {
        if (beep <= static_cast<uint32_t>(g_sounds.size())) {
            if (SoundRef *s = g_sounds[static_cast<int>(beep) - 1].get()) {
                const uint32_t bits = 0x3fb9c278u;
                std::memcpy(&s->rate, &bits, 4);
            }
        }
        sound_play(beep);
    }
}
