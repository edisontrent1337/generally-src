#include "game/selected_tracks.h"

#include <cstring>

MsVector<MsString> *selected_tracks_init() {
    g_selected_tracks.first = nullptr;
    g_selected_tracks.last = nullptr;
    g_selected_tracks.end = nullptr;
    return &g_selected_tracks;
}

void selected_tracks_free() {
    MsVector<MsString> &v = g_selected_tracks;
    if (!v.first) return;
    guest::call_regs(0x4050e0, v.first.a, v.last.a, 0u, 0u);  // destroy the strings
    guest::call_stdcall(0x404c00, v.first, (v.end.a - v.first.a) / static_cast<uint32_t>(sizeof(MsString)));
    v.first = nullptr;
    v.last = nullptr;
    v.end = nullptr;
}

void selected_track_add(const char *name) {
    guest::Local<MsString> s;
    s->init();
    s->assign(name, static_cast<uint32_t>(std::strlen(name)));
    guest::call_thiscall(0x477130, &g_selected_tracks, s.get());  // push_back
    s->destroy();
}

uint32_t selected_track_remove(int index) {
    MsVector<MsString> &v = g_selected_tracks;
    if (index < 0 || index >= v.size()) return ST_BAD_INDEX;
    MsString *at = &v[index];
    // erase: move the following strings down (0x490080), destroy the last one
    guest::call_regs(0x490080, guest::addr(at + 1), v.last.a, guest::addr(at), static_cast<uint32_t>(index));
    guest::call_regs(0x4050e0, v.last.a - 0x18, v.last.a, 0u, static_cast<uint32_t>(index));
    v.last.a -= 0x18;
    return ST_OK;
}

// Index of the folder with exactly this name.
uint32_t selected_track_find(const char *name) {
    MsVector<MsString> &v = g_selected_tracks;
    int n = v.size();
    if (n < 1) return ST_BAD_INDEX;
    size_t len = std::strlen(name);
    for (int i = 0; i < n; i++)
        if (v[i].size == len && std::memcmp(v[i].c_str(), name, len) == 0) return static_cast<uint32_t>(i);
    return ST_NOT_FOUND;
}

uint32_t selected_track_find_nocase(const char *name) {
    MsVector<MsString> &v = g_selected_tracks;
    if (v.size() < 1) return ST_BAD_INDEX;
    for (int i = 0; i < v.size(); i++)
        if (ms_stricmp(v[i].c_str(), name) == 0) return static_cast<uint32_t>(i);
    return ST_NOT_FOUND;
}

const char *selected_track_name(int index) {
    MsVector<MsString> &v = g_selected_tracks;
    if (index < 0 || index >= v.size()) return nullptr;
    return v[index].c_str();
}

int selected_track_count() { return g_selected_tracks.size(); }
