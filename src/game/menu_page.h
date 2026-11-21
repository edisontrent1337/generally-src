// Menu pages: the common frame (title, "GeneRally", header and footer lines) and captions, laid out
// on an 80x60 grid of g_font_size-pixel units centred on the screen.
#pragma once
#include "bridge/msvc.h"

// A caption on a menu page, at (x, y) in layout units.
struct PageLabel {
    int32_t x, y, color;
    MsString text;
};
static_assert(sizeof(PageLabel) == 0x24);

PageLabel *page_label_construct(PageLabel *l);
PageLabel *page_label_construct(PageLabel *l, int32_t x, int32_t y, uint8_t color, const MsString *text);

int layout_x0();  // top left corner of the layout grid, pixels
int layout_y0();
void draw_page(const MsString *title, const PageLabel *labels, int count);
void draw_dialog(const int32_t box[4], const MsString *title, const PageLabel *labels, int count);
