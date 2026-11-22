// Race flow functions of the original that are not rewritten yet.
#pragma once
#include "bridge/msvc.h"

inline void info_box_show() { race_info_box(); }      // the info box before the start
inline void results_continue() { guest::call_regs(0x446010, 0, 0); }   // the race results screen, shown again
inline void next_race() { guest::call_regs(0x4473e0, 0, 0); }          // loads the next track of the season
inline void season_results() { guest::call_regs(0x447c50, 0, 0); }     // the season results screen
inline void race_setup() { guest::call_regs(0x43c130, 0, 0); }      // places the cars, resets the race
// saves the game (replay and state) under a name in saves/ (0x440520)
inline void save_game(const MsString *name) { guest::call_regs(0x440520, guest::addr(name), 0); }
