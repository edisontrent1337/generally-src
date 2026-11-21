// The name entry dialog: a text field over the main menu or the Driver options screen, used to rename a
// driver (or to name a new one).
#pragma once
#include "game/driver.h"
#include "game/gui.h"

GLOBAL(ptr32<Driver>, g_name_driver, 0x4a4c88);     // the driver being renamed (null: a new driver)
GLOBAL(MsString, g_name_text, 0x4a6b78);            // the text being edited
GLOBAL(ptr32<GuiState>, g_name_saved_gui, 0x4a4c7c); // the Driver options screen below (null: the main menu)
GLOBAL(ptr32<Widget>, g_name_widgets, 0x4a511c);

void name_entry_open(Driver *d, const MsString *name);
void name_entry_close();
void dopt_rename_clicked(Widget *w);
void stats_reset_clicked(Widget *button);

void driver_stats_draw();
