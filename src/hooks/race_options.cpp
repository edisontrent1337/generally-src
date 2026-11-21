// Entry adapters: race options.
#include "bridge/guest.h"
#include "game/race_options.h"

using namespace abi;

REWRITE(443ac0, "race_options_write", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(race_options_write(ecx<GameFile>(c)))); }
REWRITE(442d30, "race_options_read", 0, 0, RET_EAX) {
    ret(c, static_cast<uint32_t>(race_options_read(ecx<GameFile>(c), edx<RaceOptions>(c))));
}
REWRITE(4722d0, "race_options_save", 0, 0, RET_NONE) { race_options_save(); }
REWRITE(472050, "race_options_load", 0, 0, RET_NONE) { race_options_load(); }
