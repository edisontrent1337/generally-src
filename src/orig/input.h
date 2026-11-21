// Input and main-loop functions of the original that are not rewritten yet.
#pragma once
#include "game/input.h"

inline void race_frame() { guest::call_regs(0x4061c0, 0, 0); }  // simulates and draws one race frame
