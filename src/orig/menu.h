// Original functions called by the rewritten menu code, not rewritten yet.
#pragma once
#include "game/menu.h"
#include "game/menu_page.h"
#include "game/message_box.h"



// Starts the season of selected tracks (0x43b080, not rewritten yet).
inline void race_season_start() { guest::call_regs(0x43b080, 0, 0); }

