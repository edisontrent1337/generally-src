// Entry adapters: program entry.
#include "bridge/guest.h"
#include "game/main.h"

using namespace abi;

REWRITE(471810, "win_main", 4, 16, RET_EAX) { ret(c, win_main()); }
REWRITE(4723d0, "game_main", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(game_main())); }
REWRITE(478d40, "game_init", 0, 0, RET_EAX) { ret(c, static_cast<uint32_t>(game_init())); }
REWRITE(47cf50, "game_free", 0, 0, RET_NONE) { game_free(ECX == 0); }
