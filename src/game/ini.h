// Settings files (gr.ini): sections of named values, each an int, a float or a string. Arrays grow by
// 10 through new[] with the element count stored in front (MSVC's array new).
#pragma once
#include "bridge/msvc.h"

enum IniType : int32_t { INI_INT = 0, INI_FLOAT = 1, INI_STRING = 2 };

struct IniEntry {
    MsString key;
    int32_t type;  // IniType
    int32_t ival;  // also set for floats (truncated)
    float fval;    // also set for ints
    MsString sval; // the value as text
};
static_assert(sizeof(IniEntry) == 0x3c);

struct IniSection {
    MsString name;
    uint32_t count, cap;
    ptr32<IniEntry> entries;
};
static_assert(sizeof(IniSection) == 0x24);

struct IniFile {
    uint32_t count, cap;
    ptr32<IniSection> sections;
};

IniFile *ini_construct(IniFile *ini);
void ini_destruct(IniFile *ini);
void ini_clear(IniFile *ini);
IniSection *ini_find_section(IniFile *ini, const MsString *name);
IniSection *ini_add_section(IniFile *ini, const MsString *name);
int ini_get(IniFile *ini, const char *section, const char *key, int32_t *value, int32_t *type);
IniEntry *ini_find(IniSection *s, const MsString *key);
IniEntry *ini_add(IniSection *s, const char *key, int32_t ival, float fval, const MsString *text, int32_t type);
IniEntry *ini_set_int(IniSection *s, const char *key, int32_t v);
IniEntry *ini_set_float(IniSection *s, const char *key, float v);
IniEntry *ini_set_string(IniSection *s, const char *key, const char *v);
IniEntry *ini_entry_construct(IniEntry *e);
int ini_load(IniFile *ini);
void settings_load();

// gr.ini [Viewport]: race camera overrides (0x7fff / 0: use the track's own)
GLOBAL(int32_t, g_cfg_view_angle, 0x4a4ddc);  // tilt, degrees
GLOBAL(int32_t, g_cfg_rotation, 0x4a4de0);    // rotation, degrees
GLOBAL(int32_t, g_cfg_zoom, 0x4a4de4);        // camera distance, percent
