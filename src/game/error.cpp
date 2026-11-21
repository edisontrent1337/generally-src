// The game's exception class: {vftable, message}. what() (0x401fc0) returns the message, or the
// original's "Unknown exception" literal when there is none.
#include "bridge/guest.h"

using namespace abi;

static constexpr uint32_t STR_UNKNOWN_EXCEPTION = 0x49b4d0;

struct GameError {
    uint32_t vftable;
    ptr32<const char> message;
};

static uint32_t game_error_what(const GameError *e) { return e->message ? e->message.a : STR_UNKNOWN_EXCEPTION; }

REWRITE(401fc0, "game_error_what", 0, 0, RET_EAX) { ret(c, game_error_what(ecx<GameError>(c))); }
