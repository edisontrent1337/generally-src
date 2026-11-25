// Entry adapters: race flow (pause menu buttons are GUI callbacks: one stack argument, the widget).
#include "bridge/guest.h"
#include "game/race_state.h"
#include "game/car_physics.h"
#include "game/objects.h"
#include "game/replay.h"
#include "game/render.h"

using namespace abi;

REWRITE(46f370, "pause_resume_clicked", 1, 4, RET_NONE) { pause_resume_clicked(); }
REWRITE(46f380, "pause_replay_clicked", 1, 4, RET_NONE) { pause_replay_clicked(); }
REWRITE(46f430, "pause_end_season_clicked", 1, 4, RET_NONE) { pause_end_season_clicked(); }
REWRITE(46f490, "pause_end_race_clicked", 1, 4, RET_NONE) { pause_end_race_clicked(); }
REWRITE(43bea0, "game_resume", 0, 0, RET_NONE) { game_resume(); }
REWRITE(43bb70, "race_skip", 0, 0, RET_NONE) { race_skip(); }
REWRITE(46c9c0, "frame_present", 0, 0, RET_NONE) { frame_present(); }
REWRITE(419560, "draw_disc", 3, 0, RET_NONE) {
    draw_disc(static_cast<uint8_t>(ECX), (EDX & 0xff) != 0, argf(c, 0), argf(c, 1), argf(c, 2), XMM_F(1, 0));
}
REWRITE(480dc0, "pause_menu_draw", 0, 0, RET_NONE) { pause_menu_draw(); }
REWRITE(445bc0, "race_render", 0, 0, RET_NONE) { race_render(); }
REWRITE(48d7e0, "replay_save_start", 0, 0, RET_NONE) { replay_save_start(); }
REWRITE(48d8e0, "replay_restore_start", 0, 0, RET_NONE) { replay_restore_start(); }
REWRITE(48deb0, "replay_reset", 0, 0, RET_NONE) { replay_reset(); }
REWRITE(4834a0, "pause_menu_open", 0, 0, RET_NONE) { pause_menu_open(); }
REWRITE(4835a0, "pause_menu_close", 0, 0, RET_NONE) { pause_menu_close(); }
REWRITE(416bc0, "start_light_step", 0, 0, RET_NONE) { start_light_step(static_cast<uint8_t>(ECX)); }
REWRITE(447b70, "show_loading", 0, 0, RET_NONE) { show_loading(); }
REWRITE(48d610, "replay_frames_read", 0, 0, RET_EAX) { ret(c, replay_frames_read(ecx<GameFile>(c), edx<ReplayFrame>(c))); }
REWRITE(48d700, "replay_frames_write", 1, 0, RET_EAX) {
    ret(c, replay_frames_write(ecx<GameFile>(c), edx<ReplayFrame>(c), arg(c, 0)));
}
REWRITE(48dd90, "replay_init", 0, 0, RET_NONE) { replay_init(static_cast<int>(ECX), static_cast<int>(EDX)); }
REWRITE(48efc0, "replay_write", 0, 0, RET_NONE) { replay_write(ecx<GameFile>(c)); }
REWRITE(43ba10, "race_load_track", 0, 0, RET_NONE) { race_load_track(); }
REWRITE(43cbf0, "season_record_places", 0, 0, RET_NONE) { season_record_places(); }
REWRITE(4060b0, "car_state_construct", 0, 0, RET_EAX) { ret(c, guest::addr(car_state_construct(ecx<CarState>(c)))); }
REWRITE(478c50, "car_model_construct", 0, 0, RET_EAX) { ret(c, guest::addr(car_model_construct(ecx<uint8_t>(c)))); }
REWRITE(4196d0, "undraw_disc", 3, 0, RET_NONE) { undraw_disc(argf(c, 0), argf(c, 1), argf(c, 2), XMM_F(1, 0)); }
REWRITE(43ca30, "race_record_results", 0, 0, RET_NONE) { race_record_results(); }
REWRITE(48f0f0, "replay_read", 0, 0, RET_NONE) { replay_read(ecx<GameFile>(c)); }
REWRITE(43bf20, "shade_tables_init", 0, 0, RET_NONE) { shade_tables_init(); }
REWRITE(445df0, "race_render_dimmed", 0, 0, RET_NONE) { race_render_dimmed(); }
REWRITE(43ae60, "object_collide_plane", 7, 0, RET_NONE) {
    object_collide_plane(static_cast<int>(ECX), argf(c, 0), argf(c, 1), argf(c, 2), argf(c, 3), argf(c, 4), argf(c, 5),
                         argf(c, 6));
}
REWRITE(43b7c0, "race_free", 0, 0, RET_NONE) { race_free(); }
REWRITE(43abf0, "object_collide_line", 7, 0, RET_NONE) {
    object_collide_line(static_cast<int>(ECX), argf(c, 0), argf(c, 1), argf(c, 2), argf(c, 3), argf(c, 4), argf(c, 5),
                        argf(c, 6));
}
REWRITE(420560, "car_collide_objects", 0, 0, RET_NONE) { car_collide_objects(static_cast<int>(ECX)); }
REWRITE(42f640, "object_collide_objects", 0, 0, RET_NONE) { object_collide_objects(static_cast<int>(ECX)); }
REWRITE(43a910, "object_collide_sphere", 5, 0, RET_EAX) {
    ret(c, guest::addr(object_collide_sphere(ecx<Vec3>(c), static_cast<int>(EDX), XMM_F(2, 0), argf(c, 1), argf(c, 2),
                                             argf(c, 3), argf(c, 4))));
}
REWRITE(43fc60, "pit_box_construct", 0, 0, RET_EAX) { ret(c, guest::addr(pit_box_construct(ecx<PitBox>(c)))); }
REWRITE(43bbe0, "race_controls_init", 0, 0, RET_NONE) { race_controls_init(); }
REWRITE(41c460, "hud_draw_gauges", 0, 0, RET_NONE) { hud_draw_gauges(static_cast<int>(ECX)); }
REWRITE(443360, "car_states_write", 0, 0, RET_EAX) { ret(c, car_states_write(ecx<GameFile>(c))); }
REWRITE(440520, "game_save", 0, 0, RET_NONE) { game_save(ecx<MsString>(c)); }
REWRITE(440a70, "game_load", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(game_load(ecx<MsString>(c)))); }
REWRITE(442390, "car_states_read", 0, 0, RET_EAX) { ret(c, car_states_read(ecx<GameFile>(c))); }
REWRITE(4883a0, "engine_sounds_load", 0, 0, RET_NONE) { engine_sounds_load(ecx<MsString>(c)); }
REWRITE(40f2f0, "track_redraw_tile", 0, 0, RET_NONE) { track_redraw_tile(static_cast<int>(ECX), static_cast<int>(EDX)); }
REWRITE(418fd0, "undraw_triangle", 9, 0, RET_NONE) {
    undraw_triangle(argf(c, 0), argf(c, 1), argf(c, 2), argf(c, 3), argf(c, 4), argf(c, 5), argf(c, 6), argf(c, 7), argf(c, 8));
}
REWRITE(44f470, "water_pixel", 8, 0, RET_NONE) {
    water_pixel(argf(c, 0), argf(c, 1), argf(c, 2), argf(c, 3), argf(c, 4), argf(c, 5), argi(c, 6), argi(c, 7));
}
REWRITE(407520, "replay_controls", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(replay_controls())); }
REWRITE(43b080, "race_start", 0, 0, RET_NONE) { race_start(); }
REWRITE(4473e0, "season_standings", 0, 0, RET_NONE) { season_standings(); }
REWRITE(44e120, "triangle_for_each_pixel", 18, 0, RET_NONE) {
    float f[18];
    for (int i = 0; i < 18; i++) f[i] = argf(c, i);
    triangle_for_each_pixel(ECX, f, f + 3, f + 6, f + 9, f + 12, f + 15);
}
REWRITE(44ec90, "terrain_pixel", 8, 0, RET_NONE) {
    terrain_pixel(argf(c, 0), argf(c, 1), argf(c, 2), argf(c, 3), argf(c, 4), argf(c, 5), argi(c, 6), argi(c, 7));
}
REWRITE(44e910, "flat_pixel", 9, 0, RET_NONE) {
    flat_pixel(argf(c, 0), argf(c, 1), argf(c, 2), argf(c, 3), argf(c, 4), argf(c, 5), argi(c, 6), argi(c, 7), static_cast<uint8_t>(argi(c, 8)));
}
REWRITE(43c130, "race_grid_setup", 0, 0, RET_NONE) { race_grid_setup(); }
REWRITE(447c50, "season_end", 0, 0, RET_NONE) { season_end(); }
REWRITE(42d3d0, "car_sphere_collide", 5, 0, RET_EAX) {
    ret(c, guest::addr(car_sphere_collide(ecx<Vec3>(c), static_cast<int>(EDX), argf(c, 0), argf(c, 1), argf(c, 2), argf(c, 3),
                                          argf(c, 4), xmm(c, 2), xmm(c, 3))));
}
REWRITE(42de90, "car_cylinder_collide", 7, 0, RET_NONE) {
    car_cylinder_collide(static_cast<int>(ECX), argf(c, 0), argf(c, 1), argf(c, 2), argf(c, 3), argf(c, 4), argf(c, 5), argf(c, 6),
                         xmm(c, 1), xmm(c, 2), xmm(c, 3));
}
REWRITE(42ea10, "car_plane_collide", 7, 0, RET_NONE) {
    car_plane_collide(static_cast<int>(ECX), static_cast<uint8_t>(EDX & 0xff), argf(c, 0), argf(c, 1), argf(c, 2), argf(c, 3), argf(c, 4), argf(c, 5),
                      argf(c, 6), xmm(c, 1), xmm(c, 2), xmm(c, 3));
}
REWRITE(48e220, "replay_advance", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(replay_advance())); }
REWRITE(4875b0, "sound_effects_load", 0, 0, RET_NONE) { sound_effects_load(ecx<MsString>(c)); }
REWRITE(443cc0, "car_preview_draw", 14, 0, RET_NONE) {
    float view[9];
    for (int i = 0; i < 9; i++) view[i] = argf(c, 2 + i);
    car_preview_draw(ECX, &guest::at<uint8_t>(EDX), argi(c, 0), argi(c, 1), view, argf(c, 11));
}
REWRITE(4180f0, "shaded_triangle", 11, 0, RET_NONE) {
    shaded_triangle(static_cast<uint8_t>(ECX), (EDX & 0xff) != 0, argf(c, 0), argf(c, 1), argf(c, 2), argf(c, 3), argf(c, 4),
                    argf(c, 5), argf(c, 6), argf(c, 7), argf(c, 8), XMM_F(3, 0), argf(c, 9), argf(c, 10));
}
REWRITE(4170a0, "box_draw", 12, 0, RET_NONE) {
    Vec3 v[4];
    for (int i = 0; i < 4; i++) v[i] = {argf(c, 3 * i), argf(c, 3 * i + 1), argf(c, 3 * i + 2)};
    box_draw(v[0], v[1], v[2], v[3]);
}
REWRITE(444ae0, "race_info_box", 0, 0, RET_NONE) { race_info_box(); }
REWRITE(41f3c0, "car_car_collide", 0, 0, RET_NONE) { car_car_collide(static_cast<int>(ECX), static_cast<int>(EDX)); }
REWRITE(40dbe0, "lap_line_crossed", 0, 0, RET_NONE) { lap_line_crossed(static_cast<int>(ECX)); }
REWRITE(4061c0, "race_loop_step", 0, 0, RET_NONE) { race_loop_step(); }
REWRITE(446010, "race_results_draw", 0, 0, RET_NONE) { race_results_draw(); }
REWRITE(48df20, "replay_record", 0, 0, RET_NONE) { replay_record(); }
REWRITE(40ef10, "wheel_spray", 0, 0, RET_NONE) { wheel_spray(ECX & 0xff, EDX & 0xff, XMM_F(2, 0), XMM_F(3, 0)); }
REWRITE(407cd0, "car_step", 0, 0, RET_NONE) { car_step(static_cast<int>(ECX)); }
REWRITE(40b890, "ai_drive", 1, 0, RET_NONE) { ai_drive(static_cast<int>(ECX), edx<int32_t>(c), argp<float>(c, 0), XMM_F(3, 0)); }
REWRITE(41c760, "pit_crews_step", 0, 0, RET_NONE) { pit_crews_step(); }
REWRITE(419840, "particles_update", 0, 0, RET_NONE) { particles_update(static_cast<int>(ECX)); }
REWRITE(46a040, "box_draw_pixels", 12, 0, RET_NONE) {
    box_draw_pixels(static_cast<uint8_t>(ECX), Vec3(argf(c, 0), argf(c, 1), argf(c, 2)), Vec3(argf(c, 3), argf(c, 4), argf(c, 5)),
                    Vec3(argf(c, 6), argf(c, 7), argf(c, 8)), Vec3(argf(c, 9), argf(c, 10), argf(c, 11)));
}
REWRITE(40f7f0, "race_view_draw_sorted", 0, 0, RET_NONE) { race_view_draw_sorted(); }
REWRITE(414120, "race_view_draw_unsorted", 0, 0, RET_NONE) { race_view_draw_unsorted(); }
