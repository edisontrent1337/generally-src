// Replay recording: per player a ring buffer of one ReplayFrame per race frame (g_replay_buffers).
#pragma once
#include "game/file.h"

// One car in one recorded frame. In files every field is written on its own (18 bytes, no padding).
struct ReplayFrame {
    int16_t x, y, angle;
    uint8_t b6[6];
    uint8_t v0c[4];  // a 4-byte value (unaligned in the 18-byte record)
    uint8_t b10, b11;
};
static_assert(sizeof(ReplayFrame) == 0x12);

int replay_frames_read(GameFile *f, ReplayFrame *frames);
int replay_frames_write(GameFile *f, const ReplayFrame *frames, uint32_t count);
void replay_write(GameFile *f);
void replay_read(GameFile *f);
int car_states_write(GameFile *f);
int car_states_read(GameFile *f);
void game_save(const MsString *name);
int game_load(const MsString *name);
int replay_controls();
int replay_advance();
void replay_record();
