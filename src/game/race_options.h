// The race options of the main menu and the Options screen (race length and unit, starting order, ...),
// kept in options.set and in saved games.
#pragma once
#include "game/file.h"

// Field meanings are known only where named; the rest keep their offsets.
struct RaceOptions {
    uint16_t length;        // +0x00, in the unit below
    uint8_t b02[7];
    uint8_t b09, b0a, b0b, b0c;
    uint8_t length_unit;    // +0x0d: 0 laps, 1 km, 2 minutes
    uint8_t b0e, pad0f;
    int32_t i10, i14;       // +0x10 screen width, +0x14 height
    uint8_t b18, b19, b1a, b1b, b1c, pad1d;
    uint16_t w1e;
    uint8_t b20, b21, b22;
    char text[3][0x17];     // +0x23, +0x3a, +0x51: 22 characters and a terminator each
};
static_assert(sizeof(RaceOptions) == 0x68);
GLOBAL(RaceOptions, g_race_options, 0x4a4d48);

int race_options_write(GameFile *f);
int race_options_read(GameFile *f, RaceOptions *o);
void race_options_save();
void race_options_load();
