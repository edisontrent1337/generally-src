// The menu font: a bitmap of 160 glyphs (ASCII 32..127, then Latin-1 192..255), each g_font_size
// pixels wide and twice as high, stored side by side in one row.
#pragma once
#include "bridge/msvc.h"
#include "game/video.h"

GLOBAL(int32_t, g_font_size, 0x4a4db4);  // also the menu layout unit (8 at 640x480)
GLOBAL(uint32_t, g_font, 0x4a4c84);      // glyph bitmap (guest address), one byte per pixel
GLOBAL(int32_t, g_text_shadow, 0x4a4ee8); // drop shadow offset of draw_text_at, pixels

enum : uint32_t { TEXT_CENTER_X = 1, TEXT_CENTER_Y = 2 };

void draw_text(const MsString *text, const Box *clip, uint8_t color, uint32_t flags);
void draw_text_at(const MsString *text, int x, int y, uint8_t color, uint8_t shadow);
int font_build();
