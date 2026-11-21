// Entry adapters: ghost car recorder.
#include "game/file.h"
#include "game/ghost.h"

using namespace abi;
#include "bridge/guest.h"

REWRITE(46d8f0, "ghost_init", 0, 0, RET_NONE) { ghost_init(); }
REWRITE(46da50, "ghost_record", 2, 0, RET_NONE) {
    ghost_record(ecx<Vec3>(c), edx<Vec3>(c), argp<Vec3>(c, 0), argp<Vec3>(c, 1));
}
REWRITE(46db00, "ghost_get", 3, 0, RET_EAX) {
    ret(c, ghost_get(static_cast<int>(ECX), edx<Vec3>(c), argp<Vec3>(c, 0), argp<Vec3>(c, 1), argp<Vec3>(c, 2)));
}
REWRITE(46db90, "ghost_lap_done", 0, 0, RET_EAX) {
    ghost_lap_done();
    ret(c, 0);
}
REWRITE(46dbf0, "lap_record_add", 0, 0, RET_NONE) { lap_record_add(); }
REWRITE(46d760, "ghost_read_packed", 1, 0, RET_EAX) {
    ret(c, ghost_read_packed(ecx<GameFile>(c), edx<PackedGhostFrame>(c), static_cast<int>(arg(c, 0))));
}
REWRITE(46d830, "ghost_write_packed", 1, 0, RET_EAX) {
    ret(c, ghost_write_packed(ecx<GameFile>(c), edx<PackedGhostFrame>(c), static_cast<int>(arg(c, 0))));
}
REWRITE(46dd60, "ghost_save", 0, 0, RET_EAX) { ret(c, ghost_save(ecx<GameFile>(c))); }
REWRITE(46df50, "ghost_load", 0, 0, RET_EAX) { ret(c, ghost_load(ecx<GameFile>(c))); }
REWRITE(46e4e0, "replay_save", 0, 0, RET_EAX) { ret(c, replay_save(ecx<GameFile>(c))); }
REWRITE(46e6d0, "replay_load", 0, 0, RET_EAX) { ret(c, replay_load(ecx<GameFile>(c))); }
REWRITE(4422e0, "record_dates_read", 0, 0, RET_EAX) { ret(c, record_dates_read(ecx<GameFile>(c))); }
REWRITE(4432b0, "record_dates_write", 0, 0, RET_EAX) { ret(c, record_dates_write(ecx<GameFile>(c))); }
REWRITE(496890, "track_records_write", 0, 0, RET_NONE) { track_records_write(ecx<MsString>(c)); }
REWRITE(43ff50, "track_records_save", 0, 0, RET_NONE) { track_records_save(); }
REWRITE(442fa0, "car_models_write", 1, 0, RET_EAX) {
    ret(c, static_cast<uint32_t>(car_models_write(ecx<GameFile>(c), edx<uint8_t>(c), argi(c, 0))));
}
REWRITE(4401d0, "track_checksum_write", 0, 0, RET_NONE) { track_checksum_write(ecx<GameFile>(c), static_cast<int32_t>(EDX)); }
REWRITE(43d120, "track_file_checksum", 0, 0, RET_EAX) { ret(c, track_file_checksum(ecx<MsString>(c), edx<uint32_t>(c))); }
REWRITE(43d070, "track_checksum_matches", 0, 0, RET_EAX) {
    ret(c, static_cast<uint32_t>(track_checksum_matches(ECX, edx<uint32_t>(c))));
}
REWRITE(441f50, "car_models_read", 1, 0, RET_EAX) { ret(c, static_cast<uint32_t>(car_models_read(ecx<GameFile>(c), edx<uint8_t>(c), argi(c, 0)))); }
REWRITE(43cd50, "track_find_by_checksum", 1, 0, RET_EAX) { ret(c, track_find_by_checksum(ecx<char>(c), edx<uint32_t>(c), argp<MsString>(c, 0))); }
