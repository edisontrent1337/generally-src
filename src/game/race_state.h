// Global race state.
#pragma once
#include "game/gui.h"
#include "game/race_options.h"
#include "game/vec3.h"

enum GameState : uint8_t {
    GS_INFO = 0,       // the info box before the start
    GS_RACING = 1,     // 1, 2: racing
    GS_FINISHED = 4,
    GS_RESULTS = 5,
    GS_REPLAY = 10,
};
GLOBAL(uint8_t, g_game_state, 0x4a47e3);

// A pit box and its crew of eight: 0, 1 jack the car up at the front and back, 2..5 change the tyres, 6 refuels,
// 7 repairs. A crew member's timer is 0 while walking up, 1.. while working, 0xff when done.
struct PitBox {
    uint8_t state;           // 0..5 car being serviced, 6 free, 7 reset, > 7 countdown after a car left
    uint8_t crew_timer[8];   // +0x01
    uint8_t pad09[3];
    float facing_x[8];       // +0x0c: the direction each crew member faces (x / z)
    float facing_z[8];       // +0x2c
    Vec3 crew_pos[8];        // +0x4c
    Vec3 crew_vel[8];        // +0xac, per second
    Vec3 crew_target[8];     // +0x10c: where each crew member walks to
    Vec3 centre;             // +0x16c
    Vec3 heading;            // +0x178: (cos a, 0, -sin a)
    uint8_t car;             // +0x184: the car using it (shared pits)
    uint8_t pad185[3];
};
static_assert(sizeof(PitBox) == 0x188);
GLOBAL(uint8_t, g_pit_count, 0x4a475e);
GLOBAL(ptr32<PitBox>, g_pits, 0x4a4780);
GLOBAL(uint8_t, g_pit_shared, 0x4a4732);       // nonzero: each car uses the pit box of its own number
// The crew's places when idle: around the box, facing outwards (crew_target, facing_x/z).
void pit_crew_home(PitBox &b);
// One tick of the pit crews: cars entering, stopping in and leaving the boxes, the crews' work and their
// physics (0x41c760).
void pit_crews_step();

GLOBAL(int32_t, g_replay_frames, 0x4a51b8);  // recorded replay frames
GLOBAL(float, g_replay_step, 0x4aafe8);      // 1 / g_replay_rate: recorded frames per race frame
GLOBAL(float, g_replay_pos, 0x4a51c0);       // replay frame shown
GLOBAL(int32_t, g_start_light, 0x4a4040);    // start lights shown: 0..3, -1 = none
GLOBAL(uint32_t, g_start_sound, 0x4a4dc4);   // sound handle of the start beep
GLOBAL(uint8_t, g_race_substate, 0x4a47e2);
GLOBAL(uint32_t, g_unknown_4a4774, 0x4a4774);  // 256 records of 0x1c bytes, cleared at race start
GLOBAL(uint16_t, g_unknown_4a4794, 0x4a4794);  // next slot of the particle list
GLOBAL(Vec3, g_gravity, 0x4a4054);             // per second

// An entry of the particle list (g_unknown_4a4774): smoke, spray, dust, splashes, sparks.
struct Particle {
    uint8_t type, life;
    uint16_t pad;
    Vec3 pos, vel;
};
static_assert(sizeof(Particle) == 0x1c);

inline Particle &particle(uint16_t i) { return guest::at<Particle>(g_unknown_4a4774 + i * 0x1cu); }

// Moves the particle slot on, wrapping at the list size.
inline void next_particle() {
    const uint16_t s = g_unknown_4a4794;
    g_unknown_4a4794 = static_cast<uint32_t>(s) + 1 < g_race_options.w1e ? static_cast<uint16_t>(s + 1) : uint16_t(0);
}
GLOBAL(float, g_skid_amount, 0x4a4734);  // summed tyre skid this tick (drives the skid sound)
GLOBAL(uint32_t, g_unknown_4a47a0, 0x4a47a0);  // the height map buffer (0x1000 bytes)
GLOBAL(uint8_t, g_racing, 0x4a4797);  // the race runs (0 in menus and while paused)
GLOBAL(int32_t, g_unknown_4a4788, 0x4a4788);
// races limited by time (length unit minutes)
GLOBAL(uint8_t, g_length_unit, 0x4a4d55);        // 0 laps, 1 km, 2 minutes
GLOBAL(uint8_t, g_timed_race, 0x4a4733);
GLOBAL(int32_t, g_time_limit, 0x4a4754);         // seconds, -1 when not timed
GLOBAL(int32_t, g_race_clock_start, 0x4a47dc);   // _time64() at the start, -1 before
GLOBAL(int32_t, g_race_clock_end, 0x4a4740);     // start + limit, moved on by pauses
GLOBAL(int32_t, g_pause_clock, 0x4a4758);        // _time64() when paused, -1 otherwise
GLOBAL(uint8_t, g_unknown_4a4792, 0x4a4792);
GLOBAL(int32_t[6], g_season_points, 0x4a47a4);  // per player
GLOBAL(uint32_t, g_resume_ticks, 0x4a47d8);   // SDL_GetTicks() at the last resume
GLOBAL(int32_t, g_leader_laps, 0x4a47fc);     // laps completed by the leader
GLOBAL(int32_t, g_race_laps, 0x4a4760);
GLOBAL(int32_t[6], g_finish_order, 0x4a47c0);   // player of each place
GLOBAL(int32_t, g_fastest_lap, 0x4a4770);
GLOBAL(int32_t, g_unknown_4a47f8, 0x4a47f8);
GLOBAL(int32_t, g_unknown_4a4798, 0x4a4798);
GLOBAL(int32_t, g_unknown_4a479c, 0x4a479c);       // the race's fastest lap, frames
// replay recording: one buffer per player
GLOBAL(ptr32<uint8_t>[6], g_replay_buffers, 0x4a51c8);
GLOBAL(int32_t, g_replay_head, 0x4a51c4);    // oldest recorded frame in the ring buffers
GLOBAL(float, g_replay_mark_out, 0x4a51e8);  // replay range marks (-1: none); a save keeps only the range
GLOBAL(float, g_replay_mark_in, 0x4a51bc);
GLOBAL(float, g_replay_speed, 0x4a51e4);   // frames advanced per frame: direction * num / den (or the hold keys' speed)
GLOBAL(int32_t, g_replay_speed_num, 0x4a4064);  // 1..8
GLOBAL(int32_t, g_replay_speed_den, 0x4a4028);  // 1..8
GLOBAL(int32_t, g_replay_direction, 0x4a4060);  // 1 forward, 0 stopped, -1 backward
GLOBAL(uint8_t, g_replay_mark_state, 0x4ab714); // range mark key: 0 none, 1 start marked (key held), 2 start marked
GLOBAL(uint8_t, g_replay_slow_held, 0x4ab715);
GLOBAL(uint8_t, g_replay_fast_held, 0x4ab716);
GLOBAL(uint8_t, g_replay_slower_held, 0x4ab717);
GLOBAL(uint8_t, g_replay_faster_held, 0x4ab718);
GLOBAL(MsString, g_replay_clock_text, 0x4a51ec);  // replay HUD: time shown
GLOBAL(MsString, g_replay_speed_text, 0x4a5b88);  // replay HUD: speed, "<sign><num>:<den>"
GLOBAL(float, g_water_level, 0x4a4ecc);  // height of the water plane; engines under it sound an octave lower
GLOBAL(int32_t, g_replay_rate, 0x4a4050);      // a frame is recorded every g_replay_rate race frames (1..100)
GLOBAL(int32_t, g_replay_capacity, 0x4a404c);  // frames per ring buffer
GLOBAL(int32_t, g_cfg_replay_rate, 0x4a4dcc);  // gr.ini settings
GLOBAL(int32_t, g_cfg_replay_length, 0x4a4dc8);

// The cars as they move (6 records of 0x194 bytes at 0x4a5210; layout mostly unknown yet). The fields a
// replay restores when it starts over:
//   +0x58 position (3 floats), +0x7c orientation (3 x 3 floats), +0xa0 a float,
//   +0xe8 + 0xc * (layout + k), k = 0..3: the wheels' world positions (3 floats; layout: byte +0xbd of
//   the car's model). The model has the wheels' positions relative to the car at +0x504 (4 x 3 floats).
struct CarState {
    uint8_t raw[0x194];
};
static_assert(sizeof(CarState) == 0x194);
// fields known so far
inline int32_t car_race_time(int i);   // +0x10: frames raced (when finished: the finishing time)
inline uint16_t car_laps(int i);       // +0xd4: laps completed
inline uint16_t car_best_lap(int i);   // +0xdc: best lap, frames
GLOBAL(CarState[6], g_car_states, 0x4a5210);
GLOBAL(uint8_t[6][0x540], g_race_models, 0x4a9050);
inline int32_t car_race_time(int i) { return *reinterpret_cast<int32_t *>(g_car_states[i].raw + 0x10); }
inline uint16_t car_laps(int i) { return *reinterpret_cast<uint16_t *>(g_car_states[i].raw + 0xd4); }
inline uint16_t car_best_lap(int i) { return *reinterpret_cast<uint16_t *>(g_car_states[i].raw + 0xdc); }
inline float &car_height(int i) { return *reinterpret_cast<float *>(g_car_states[i].raw + 0x5c); }
inline float &car_engine(int i) { return *reinterpret_cast<float *>(g_car_states[i].raw + 0xa0); }  // model record of each player's car

// What replay_save_start keeps of one car (0x44 bytes, at 0x4ab578)
struct ReplayStartCar {
    uint32_t position[3];     // car +0x58
    uint32_t orientation[9];  // car +0x7c
    uint32_t wheels[4];       // the wheels' y (car +0xec + 0xc * (layout + k))
    uint32_t a0;              // car +0xa0
};
static_assert(sizeof(ReplayStartCar) == 0x44);
GLOBAL(ReplayStartCar[6], g_replay_start_cars, 0x4ab578);
GLOBAL(uint32_t, g_replay_start_state, 0x4a51e0);  // g_game_state when the replay started

CarState *car_state_construct(CarState *s);
uint8_t *car_model_construct(uint8_t *m);
PitBox *pit_box_construct(PitBox *b);
void race_render();
void race_draw_scene();
void race_info_box();
void race_render_dimmed();
void race_free();
void race_controls_init();
void engine_volume_apply();
void alloc_race_buffers();
void race_start();
void race_grid_setup();
void season_standings();
void season_end();
void effects_volume_apply();
void engine_sounds_load(const MsString *sound_set);
void sound_effects_load(const MsString *sound_set);
void hud_draw_gauges(int player);
void replay_save_start();
void replay_restore_start();
void game_resume();
void race_skip();
void replay_reset();
void replay_init(int min_capacity, int rate);
void start_light_step(uint8_t light);
void show_loading();
void race_load_track();
void season_record_places();
void race_record_results();

GLOBAL(uint32_t[6], g_engine_sound, 0x4a4d24);   // sound handle of each player's engine
GLOBAL(uint32_t, g_skid_sound, 0x4a4e04);   // effect sound handles (sound set: skid.wav, damage.wav,
GLOBAL(uint32_t, g_damage_sound, 0x4a4dbc);  // yeah.wav, fuel.wav; g_start_sound is ding.wav)
GLOBAL(uint32_t, g_fuel_sound, 0x4a4db0);
GLOBAL(uint32_t, g_cheer_sound, 0x4a4e00);
GLOBAL(uint16_t, g_unknown_4a4790, 0x4a4790);
GLOBAL(float, g_unknown_4a47f4, 0x4a47f4);  // lowers the engine volume (scaled by -0.93, millibels)
GLOBAL(ptr32<Widget>, g_pause_widgets, 0x4a4df4);  // Resume, Replay, Options, Save, End race, End season
enum { PAUSE_REPLAY = 1 };

void pause_menu_draw();
void pause_menu_open();
void pause_menu_close();
void pause_resume_clicked();
void pause_replay_clicked();
void pause_end_season_clicked();
void pause_end_race_clicked();
void lap_line_crossed(int p);
void race_loop_step();
// The race length ("1 " or "<n> ", then the lap or minute unit text) into out (0x444ae0, 0x446010).
void race_length_text(MsString *out);
void race_results_draw();
