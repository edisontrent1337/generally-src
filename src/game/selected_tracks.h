// The tracks selected in the main menu (a std::vector<std::string> of track names): the tracks of a
// season, raced in order.
#pragma once
#include "bridge/msvc.h"

GLOBAL(MsVector<MsString>, g_selected_tracks, 0x4aafdc);
GLOBAL(int32_t, g_current_track, 0x4a4d00);  // index of the track being raced

// results of the original (HRESULT-like codes)
constexpr uint32_t ST_OK = 0x06400280, ST_NOT_FOUND = 0x86400282, ST_BAD_INDEX = 0x86400284;

MsVector<MsString> *selected_tracks_init();
void selected_tracks_free();
void selected_track_add(const char *name);
uint32_t selected_track_remove(int index);
uint32_t selected_track_find(const char *name);
uint32_t selected_track_find_nocase(const char *name);
const char *selected_track_name(int index);
int selected_track_count();
