// The file dialog (save / load a replay): a title, a list of files and a text field for the name.
#pragma once
#include "game/gui.h"

GLOBAL(ptr32<Widget>, g_file_dialog_widgets, 0x4a4c70);
enum { FD_TITLE = 2, FD_FILES = 3, FD_NAME = 4 };

void file_dialog_file_clicked(Widget *list);
void file_dialog_fill();
void file_dialog_draw();
void file_dialog_open(Widget *button);
void file_dialog_closed(Widget *button);
void file_dialog_delete_clicked(Widget *button);
