// Entry adapters: Advanced options dropdowns.
#include "bridge/guest.h"
#include "game/advanced_options.h"
#include "game/menu_page.h"

using namespace abi;

REWRITE(48bea0, "adv_resolution_clicked", 1, 4, RET_NONE) { adv_open_dropdown(ADV_RESOLUTION_LIST, ADV_RESOLUTION); }
REWRITE(48c150, "adv_palette_clicked", 1, 4, RET_NONE) { adv_open_dropdown(ADV_PALETTE_LIST, ADV_PALETTE); }
REWRITE(48c400, "adv_language_clicked", 1, 4, RET_NONE) { adv_open_dropdown(ADV_LANGUAGE_LIST, ADV_LANGUAGE); }
REWRITE(48c6b0, "adv_soundset_clicked", 1, 4, RET_NONE) { adv_open_dropdown(ADV_SOUNDSET_LIST, ADV_SOUNDSET); }
REWRITE(48c050, "adv_resolution_picked", 1, 4, RET_NONE) { adv_pick(argp<Widget>(c, 0), ADV_RESOLUTION); }
REWRITE(48c300, "adv_palette_picked", 1, 4, RET_NONE) { adv_pick(argp<Widget>(c, 0), ADV_PALETTE); }
REWRITE(48c5b0, "adv_language_picked", 1, 4, RET_NONE) { adv_pick(argp<Widget>(c, 0), ADV_LANGUAGE); }
REWRITE(48c860, "adv_soundset_picked", 1, 4, RET_NONE) { adv_pick(argp<Widget>(c, 0), ADV_SOUNDSET); }
REWRITE(473010, "adv_fill_resolutions", 0, 0, RET_NONE) { adv_fill_resolutions(ecx<Widget>(c)); }
REWRITE(473120, "adv_fill_palettes", 0, 0, RET_NONE) { adv_fill_palettes(ecx<Widget>(c)); }
REWRITE(4732a0, "adv_fill_languages", 0, 0, RET_NONE) { adv_fill_languages(ecx<Widget>(c)); }
REWRITE(473420, "adv_fill_soundsets", 0, 0, RET_NONE) { adv_fill_soundsets(ecx<Widget>(c)); }
REWRITE(47e000, "adv_draw_labels", 0, 0, RET_NONE) { adv_draw_labels(); }
REWRITE(47d3e0, "draw_page", 2, 0, RET_NONE) {
    draw_page(edx<MsString>(c), argp<PageLabel>(c, 0), arg(c, 1));
}
REWRITE(478bd0, "page_label_construct", 0, 0, RET_EAX) { ret(c, guest::addr(page_label_construct(ecx<PageLabel>(c)))); }
REWRITE(478c00, "page_label_construct_with", 4, 16, RET_EAX) {
    ret(c, guest::addr(page_label_construct(ecx<PageLabel>(c), static_cast<int32_t>(arg(c, 0)), static_cast<int32_t>(arg(c, 1)),
                                            static_cast<uint8_t>(arg(c, 2)), argp<MsString>(c, 3))));
}
REWRITE(481d00, "adv_options_done", 1, 4, RET_NONE) { adv_options_done(argp<Widget>(c, 0)); }
