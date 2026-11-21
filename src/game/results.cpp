// The results box after a race (0x446010), over the dimmed race view.
#include <cstdint>
#include <cstring>

#include "game/ghost.h"
#include "game/lang.h"
#include "game/menu.h"
#include "game/race_state.h"
#include "game/render.h"
#include "game/selected_tracks.h"
#include "game/text.h"
#include "game/track.h"
#include "game/video.h"
#include "bridge/imports.h"

namespace {
// The camera of the small car pictures (as in the info box): right, up and forward axes, distance 120.
constexpr float kCarView[9] = {-0.6396f, -0.4264f, -0.6396f, 0.0f, 0.8321f, -0.5547f, 0.6396f, -0.4264f, -0.6396f};

struct Layout {
    int u, b;  // font unit, border
    int x0;    // left edge of the box
};

uint8_t *car(int p) { return g_car_states[p].raw; }
int32_t finish_time(int p) { return *reinterpret_cast<int32_t *>(car(p) + 0x10); }
uint16_t laps_done(int p) { return *reinterpret_cast<uint16_t *>(car(p) + 0xd4); }

// sprintf (0x46f320) into s: the result is the game's shared buffer.
void format_into(MsString *s, uint32_t fmt, uint32_t a, uint32_t b = 0, uint32_t c = 0) {
    uint32_t r = guest::call_cdecl(0x46f320, fmt, a, b, c);
    s->assign(&guest::at<char>(r), static_cast<uint32_t>(std::strlen(&guest::at<char>(r))));
}

// A guest C string drawn through the scratch string (emptied again afterwards).
void draw_cstr(MsString *tmp, const char *s, uint32_t n, int x, int y, uint8_t colour, uint8_t shadow) {
    tmp->assign(s, n);
    draw_text_at(tmp, x, y, colour, shadow);
    tmp->destroy();
}

// The place marker: a 2(b + u) square of colour 0xf6 with a 2u square of 0xf5 inside, and the place
// number (format at 0x49cdd8) in colour 1, without shadow.
void place_marker(MsString *tmp, const Layout &l, int y, int place) {
    const int pitch = static_cast<int>(g_pitch);
    for (int r = y - l.b; r < l.u * 2 + l.b + y; r++)
        std::memset(&guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(r * pitch + l.x0)), 0xf6,
                    static_cast<size_t>((l.b + l.u) * 2));
    for (int r = y; r < l.u * 2 + y; r++)
        std::memset(&guest::at<uint8_t>(g_pixels + static_cast<uint32_t>(r * pitch + l.b + l.x0)), 0xf5,
                    static_cast<size_t>(l.u * 2));
    uint32_t s = guest::call_cdecl(0x46f320, 0x49cdd8u, place);
    draw_cstr(tmp, &guest::at<char>(s), static_cast<uint32_t>(std::strlen(&guest::at<char>(s))),
              (l.b - guest::to_int(static_cast<float>(l.u) * -0.5f)) + l.x0, y, 1, 0);
}

// A car picture and the driver's name (colour 0xf2) of one row.
void car_and_name(MsString *tmp, const Layout &l, int y, uint32_t colour_base, const uint8_t *model, const char *name) {
    car_preview_draw(colour_base, model, l.x0 + l.u * 6, l.u + y, kCarView, 120.0f);
    draw_cstr(tmp, name, static_cast<uint32_t>(std::strlen(name)), l.x0 + l.u * 10, y, 0xf2, 1);
}

// "s.hh" of a time in frames (format at 0x49cd3c).
void time_text(MsString *s, int t) {
    format_into(s, 0x49cd3cu, static_cast<uint32_t>(t / 100 % 100), static_cast<uint32_t>(t / 10 % 10),
                static_cast<uint32_t>(t % 10));
}

// The race's fastest lap: its driver's car and name, and the time.
void fastest_lap_row(MsString *tmp, MsString *text, const Layout &l, int y) {
    const int p = g_unknown_4a4798;
    car_and_name(tmp, l, y, static_cast<uint32_t>(p), g_race_models[p], g_player_name[p]);
    time_text(text, g_fastest_lap);
    draw_text_at(text, l.x0 + l.u * 30, y, 0xf5, 1);
}
}  // namespace

// The box (36 units wide, centred) after the race: the track's name and the race length; with one player
// the track's lap records (lang 118: up to six, each with a place marker where the time is worse than the
// one above, the holder's car in the player's colours when the holder is the player, the time) and below
// (lang 119) the race's fastest lap with its place among the records; with more players the finishing
// order (lang 10: the winner's average speed in km/h, the others' gap to the winner or the laps behind)
// and the fastest lap (lang 9). The guest string at 0x49cd48 marks a fastest lap that is (4a4798 ==
// 4a479c) the newly set one.
void race_results_draw() {
    guest::Local<MsString> text;  // the right-hand column; kept until the end
    text->init();
    race_render_dimmed();
    lock_surface(SURF_BACK);
    const char *path = nullptr;
    MsVector<MsString> &tracks = g_selected_tracks;
    if (g_current_track >= 0 && g_current_track < tracks.size()) path = tracks[g_current_track].c_str();
    const uint32_t slash = guest::call_cdecl(guest::at<uint32_t>(0x49b228), guest::addr(path), 0x5cu);  // strrchr
    const char *name = &guest::at<char>(slash + 1);
    guest::Local<MsString> tmp;  // scratch string, emptied after each use
    tmp->init();

    Layout l{g_font_size, g_menu_border, 0};
    const int u = l.u;
    l.x0 = g_screen_w / 2 - u * 18;
    int y = (g_screen_h - u * 36 - l.b * 5) / 2;
    const bool solo = g_player_count == 1;
    draw_cstr(tmp.get(), name, static_cast<uint32_t>(std::strlen(name)), l.x0, y, 0xf2, 1);
    race_length_text(text.get());
    draw_text_at(text.get(), (36 - static_cast<int>(text->size)) * u + l.x0, y, 0xf2, 1);
    y += u * 4;
    draw_text_at(&g_lang[solo ? 118 : 10], l.x0, y, 0xf5, 1);
    y = y + u + u * 2;

    if (solo) {
        int k = 1;
        for (; k < 7; k++) {
            const int t = g_record_time[k];
            if (t >= 6000) break;
            if (k == 1 || g_record_time[k - 1] < t) place_marker(tmp.get(), l, y, k);
            const uint32_t own = guest::call_cdecl(guest::at<uint32_t>(0x49b358), guest::addr(g_record_name[k]),
                                                   guest::addr(g_player_name[0]));  // _stricmp
            car_and_name(tmp.get(), l, y, own == 0 ? 0u : 0xffffffffu, g_record_car[k], g_record_name[k]);
            time_text(text.get(), t);
            draw_text_at(text.get(), l.x0 + u * 30, y, 0xf5, 1);
            y = y + u + u * 2;
        }
        y += (22 - k * 3) * u;
        draw_text_at(&g_lang[119], l.x0, y, 0xf5, 1);
        y = y + u + u * 2;
        if (g_unknown_4a4798 == g_unknown_4a479c) {
            draw_cstr(tmp.get(), &guest::at<char>(0x49cd48), 2, l.b + l.x0, y, 0xf4, 1);
        } else if (g_fastest_lap <= g_record_time[6] && g_fastest_lap < 6000) {
            int place = 1;
            while (g_fastest_lap > g_record_time[place] && place < 7) place++;
            uint32_t s = guest::call_cdecl(0x46f320, 0x49cd4cu, place);
            draw_cstr(tmp.get(), &guest::at<char>(s), static_cast<uint32_t>(std::strlen(&guest::at<char>(s))),
                      (l.b - guest::to_int(static_cast<float>(u) * -0.5f)) + l.x0, y, 0xf4, 1);
        }
    } else {
        const int n = g_player_count;
        for (int k = 0; k < n; k++) {
            const int p = g_finish_order[k];
            int x;
            if (k == 0) {
                place_marker(tmp.get(), l, y, 1);
                // the winner's average speed, km/h with two decimals: length * laps * 360 / time * 100
                const int lead = g_finish_order[0];
                const int32_t ft = finish_time(lead);
                const float per = static_cast<float>(laps_done(lead)) * 360.0f / static_cast<float>(ft);
                int v = 0;
                if (ft > 0) {
                    const float len = static_cast<float>(static_cast<double>(static_cast<uint32_t>(g_track_length)));
                    v = guest::to_int(len * per * 100.0f + 0.5f);
                }
                format_into(text.get(), 0x49cd50u, static_cast<uint32_t>(v / 100 % 1000), static_cast<uint32_t>(v / 10 % 10),
                            static_cast<uint32_t>(v % 10));
                x = u * 26;
            } else {
                const int prev = g_finish_order[k - 1];
                if (laps_done(p) == static_cast<uint32_t>(g_leader_laps)) {  // the gap to the winner
                    if (finish_time(p) > finish_time(prev)) place_marker(tmp.get(), l, y, k + 1);
                    const int d = finish_time(p) - finish_time(g_finish_order[0]);
                    format_into(text.get(), 0x49cd60u, static_cast<uint32_t>(d / 100 % 100), static_cast<uint32_t>(d / 10 % 10),
                                static_cast<uint32_t>(d % 10));
                    x = ((d < 1000 ? 1 : 0) + 29) * u;
                } else {  // laps behind (unit word at 0x49cd6c for one, 0x49cd70 for more)
                    if (laps_done(p) < laps_done(prev) || finish_time(p) > finish_time(prev)) place_marker(tmp.get(), l, y, k + 1);
                    const int d = g_leader_laps - laps_done(p);
                    format_into(text.get(), 0x49cd78u, static_cast<uint32_t>(d), d == 1 ? 0x49cd6cu : 0x49cd70u);
                    x = (36 - static_cast<int>(text->size)) * u;
                }
            }
            draw_text_at(text.get(), x + l.x0, y, 0xf5, 1);
            car_and_name(tmp.get(), l, y, static_cast<uint32_t>(p), g_race_models[p], g_player_name[p]);
            y = y + u + u * 2;
        }
        y += (19 - n * 3) * u;
        draw_text_at(&g_lang[9], l.x0, y, 0xf5, 1);
        y = y + u + u * 2;
        if (g_unknown_4a4798 == g_unknown_4a479c) draw_cstr(tmp.get(), &guest::at<char>(0x49cd48), 2, l.b + l.x0, y, 0xf4, 1);
    }
    fastest_lap_row(tmp.get(), text.get(), l, y);
    unlock_surface();
    frame_present();
    text->destroy();
}
