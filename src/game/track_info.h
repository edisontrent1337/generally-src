// The Track info dialog (right-click on a track in the main menu): preview, length, author, records;
// Reset clears the track's hot-lap records and ghost.
#pragma once
#include "game/gui.h"

GLOBAL(ptr32<char>, g_info_track, 0x4a4c74);          // the track shown ("\name" or "\folder\name", 60 bytes)
GLOBAL(ptr32<Widget>, g_track_info_widgets, 0x4a4c9c);
GLOBAL(char[36], g_track_author, 0x4a4c50);     // from the track file, empty if none
GLOBAL(char[0x1f5], g_track_description, 0x4a4f20);  // from the track file, empty if none

void track_info_open(const char *track);
void track_info_draw();
void track_info_reset_clicked(Widget *w);
void track_info_description_clicked(Widget *w);
