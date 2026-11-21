// The user interface texts, loaded from lang/<language>.lng: entry #n of the file is g_lang[n].
#pragma once
#include "bridge/msvc.h"

constexpr int LANG_COUNT = 169;
GLOBAL(MsString[LANG_COUNT], g_lang, 0x4a5ba0);

// entry numbers used by rewritten code
enum Lang {
    L_OOPS = 0,
    L_ON = 5,
    L_LOADING = 16,
    L_LOAD_TITLE = 90,  // file dialog titles
    L_SAVE_TITLE = 91,
    L_FD_FILENAME = 93,  // file dialog captions
    L_FD_FILES = 94,
    L_GAME = 38,         // second word of the file dialog title
    L_EXIT = 17,
    L_RACE_LENGTH = 19,
    L_SELECT_FIRST = 20,
    L_END_SEASON = 30,
    L_MAIN_TITLE = 34,
    L_ADV_TITLE = 36,  // "Advanced options"
    L_OFF = 6,
    L_LENGTH_LAPS = 53,
    L_LENGTH_KM = 54,
    L_START_RANDOM = 55,
    L_START_POINTS = 56,
    L_START_POINTS_REVERSED = 57,
    L_CREATE_DRIVER = 70,
    L_DELETE_DRIVER = 71,  // title of the confirmation
    L_CONTROLS = 75,
    L_ACCELERATE = 76,  // .. 79: the four controls
    L_UNASSIGNED = 80,
    L_NEW_DRIVER_NAME = 81,  // captions in the Create driver dialog
    L_NEW_DRIVER_KIND = 82,
    L_HUMAN = 83,
    L_DRIVER_NAME_EMPTY = 85,   // Create driver refused: no name
    L_DRIVER_NAME_EXISTS = 86,  // Create driver refused: the name is taken
    L_COMPUTER = 84,
    L_PAUSE = 87,  // title of the pause menu
    L_AI_SKILL = 89,
    L_REALLY_DELETE = 102,  // start of the question, the name follows in quotes
    L_ARE_YOU_SURE = 116,
    L_END_RACE = 117,
    L_SELECTED = 133,
    L_ALL_TRACKS = 128,      // the two entries at the top of the folder list
    L_DEFAULT_TRACKS = 129,
    L_SEVERAL_FOLDERS = 130,  // folder button caption when more than one folder is marked
    L_DELETE_FOLDER_TITLE = 140,  // folder deletion: title, "select one folder", confirmation, failure
    L_DELETE_FOLDER_SELECT_ONE = 141,
    L_DELETE_FOLDER_CONFIRM = 142,
    L_DELETE_FOLDER_FAILED = 143,
    L_MOVE_TRACKS = 144,
    L_SELECT_TRACK_FIRST = 147,
    L_LENGTH_MINUTES = 164,
    L_SKIP_RACE_Q = 167,
    L_QUIT_TO_MENU_Q = 168,
};
bool lang_find_entry(const char *text, uint32_t len, uint32_t n, MsString *out);
void lang_load(const MsString *name);
