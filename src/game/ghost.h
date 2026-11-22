// The ghost car: the player's car pose is recorded every frame of a lap; the best lap's recording is
// replayed as the ghost. Two buffers alternate: one is recorded into, the other holds the best lap.
#pragma once
#include "bridge/msvc.h"
#include "game/vec3.h"

struct GhostFrame {
    Vec3 v[4];  // position and orientation vectors of the car
};
static_assert(sizeof(GhostFrame) == 0x30);

constexpr int kGhostFrames = 6000;

GLOBAL(ptr32<GhostFrame>, g_ghost_buf_a, 0x4a4c14);  // kGhostFrames + 1 frames each
GLOBAL(ptr32<GhostFrame>, g_ghost_buf_b, 0x4a4c18);
GLOBAL(ptr32<GhostFrame>, g_ghost_best, 0x4a4c1c);  // the replayed lap, null if none
GLOBAL(uint16_t, g_ghost_best_len, 0x4a4c20);
GLOBAL(ptr32<GhostFrame>, g_ghost_rec, 0x4a4c24);   // the lap being recorded
GLOBAL(uint16_t, g_ghost_rec_len, 0x4a4c28);
GLOBAL(uint16_t, g_ghost_max, 0x4a4044);

// Hot-lap records of the track: 7 entries, kept sorted by lap time from entry 1 on (entry 0 is never
// displaced by a new lap); 4a4e58.. also holds the date of each.
struct SystemTime {
    uint16_t year, month, day_of_week, day, hour, minute, second, ms;
};
static_assert(sizeof(SystemTime) == 16);
constexpr int kRecords = 7;
GLOBAL(uint16_t[kRecords], g_record_time, 0x4a4ed0);      // lap time in frames
GLOBAL(char[kRecords][13], g_record_name, 0x4a4ca0);      // driver name
GLOBAL(uint8_t[kRecords][0x540], g_record_car, 0x4a6b90); // the car's model record
GLOBAL(SystemTime[kRecords], g_record_date, 0x4a4e58);
GLOBAL(uint8_t[0x540], g_race_car, 0x4a9050);  // model record of the car racing (the first driver's)
// checksum of the loaded track file (track_load), the start value of the records' checksum
GLOBAL(uint32_t[4], g_checksum_seed, 0x4a4030);

// In files every second frame is stored, packed: position ×150 as ints, the two direction vectors
// ×120 as signed bytes (the third, their cross product, is recomputed).
struct PackedGhostFrame {
    int32_t pos[3];
    int8_t dir[6];  // v[1], v[3]
    uint16_t pad;
};
static_assert(sizeof(PackedGhostFrame) == 20);

struct GameFile;

void ghost_init();
void lap_record_add();
int ghost_save(GameFile *f);
int ghost_load(GameFile *f);
int replay_save(GameFile *f);
int replay_load(GameFile *f);
int ghost_read_packed(GameFile *f, PackedGhostFrame *p, int n);
int ghost_write_packed(GameFile *f, const PackedGhostFrame *p, int n);
void ghost_record(const Vec3 *a, const Vec3 *b, const Vec3 *c, const Vec3 *d);
int ghost_get(int frame, Vec3 *a, Vec3 *b, Vec3 *c, Vec3 *d);
void ghost_lap_done();
int record_dates_read(GameFile *f);
int record_dates_write(GameFile *f);
int car_models_write(GameFile *f, const uint8_t *models, int count);
int car_models_read(GameFile *f, uint8_t *models, int count);
void track_checksum_write(GameFile *f, int32_t start);
uint32_t track_file_checksum(const MsString *name, uint32_t sum[4]);
bool track_checksum_matches(uint32_t name, const uint32_t expected[4]);
uint32_t track_find_by_checksum(const char *name, const uint32_t sum[4], MsString *out);
void track_records_write(const MsString *name);
void track_records_save();
