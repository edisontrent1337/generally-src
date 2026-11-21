// The Advanced options screen: palette, language, sound set, display mode and volumes. Four of its
// settings are dropdowns: a button showing the current value that opens a list box.
#pragma once
#include "game/gui.h"

GLOBAL(ptr32<Widget>, g_adv_widgets, 0x4a4c80);
constexpr int ADV_COUNT = 21;

// widget indices
enum {
    ADV_BACK = 1,        // hidden while the screen is opened over the pause menu
    ADV_RESOLUTION = 5,  // dropdown button, list ADV_RESOLUTION_LIST
    ADV_LANGUAGE_LIST = 13,
    ADV_LANGUAGE = 14,
    ADV_SOUNDSET_LIST = 15,
    ADV_SOUNDSET = 16,
    ADV_RESOLUTION_LIST = 18,
    ADV_PALETTE_LIST = 19,
    ADV_PALETTE = 20,
};

GLOBAL(ptr32<Widget>, g_options_widgets, 0x4a5124);  // the Options screen (17 widgets)
GLOBAL(ptr32<GuiState>, g_saved_gui, 0x4a5118);  // the screen below, when opened from the pause menu

void adv_open_dropdown(int list, int button);
void adv_pick(Widget *list, int button);
void adv_draw_labels();
void adv_fill_resolutions(Widget *list);
void adv_fill_palettes(Widget *list);
void adv_fill_languages(Widget *list);
void adv_fill_soundsets(Widget *list);
void adv_options_done(Widget *button);
