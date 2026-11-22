#include "game/ini.h"

#include "game/file.h"
#include "game/input.h"
#include "game/race_state.h"

#include <cstring>

// MSVC array new: count in front, elements constructed by the `eh vector constructor iterator'
static uint32_t array_size(uint32_t n, uint32_t elem) {
    uint64_t b = static_cast<uint64_t>(n) * elem;
    uint32_t s = b > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(b);
    return s > 0xfffffffbu ? 0xffffffffu : s + 4;
}
static uint32_t array_new(uint32_t n, uint32_t elem, uint32_t ctor, uint32_t dtor) {
    uint32_t p = guest::call_cdecl(0x496c14, array_size(n, elem));
    if (!p) return 0;
    guest::at<uint32_t>(p) = n;
    guest::call_stdcall(0x4971ad, p + 4, elem, n, ctor, dtor);
    return p + 4;
}
static constexpr uint32_t STRING_CTOR = 0x402840, STRING_DTOR = 0x4027a0;  // the sections' (name only)
static constexpr uint32_t ENTRY_CTOR = 0x46fd60, ENTRY_DTOR = 0x46fd90;
// delete[] of entries / sections (the arrays' `vector deleting destructor')
static void delete_entries(uint32_t p) { guest::call_regs(0x46fcf0, p, 0, p); }
static void delete_sections(uint32_t p) { guest::call_regs(0x46f580, p, 0, p); }
// `eh vector destructor iterator' + delete[] (inlined in ini_clear)
static void destroy_array(uint32_t p, uint32_t elem, uint32_t dtor) {
    uint32_t n = guest::at<uint32_t>(p - 4);
    guest::call_stdcall(0x4970c6, p, elem, n, dtor);
    guest::call_cdecl(0x4970b8, p - 4, n * elem + 4);
}

static uint32_t cstr_len(const char *s) { return static_cast<uint32_t>(std::strlen(s)); }
static int stricmp_guest(const char *a, const char *b) {
    return static_cast<int>(guest::call_import(IMP__stricmp, guest::addr(a), guest::addr(b)));
}

IniEntry *ini_entry_construct(IniEntry *e) {
    e->key.init();
    e->sval.init();
    return e;
}

IniFile *ini_construct(IniFile *ini) {
    ini->cap = 32;
    ini->count = 0;
    uint32_t p = array_new(32, sizeof(IniSection), STRING_CTOR, STRING_DTOR);
    ini->sections = p ? ptr32<IniSection>(&guest::at<IniSection>(p)) : nullptr;
    return ini;
}

// Frees everything and starts over with room for 10 sections.
void ini_clear(IniFile *ini) {
    if (!ini->sections) return;
    for (uint32_t i = 0; i < ini->count; i++) {
        uint32_t e = ini->sections.get()[i].entries.a;
        if (e) destroy_array(e, sizeof(IniEntry), ENTRY_DTOR);
    }
    if (ini->sections) destroy_array(ini->sections.a, sizeof(IniSection), STRING_DTOR);
    ini->cap = 10;
    ini->count = 0;
    uint32_t p = array_new(10, sizeof(IniSection), STRING_CTOR, STRING_DTOR);
    ini->sections = p ? ptr32<IniSection>(&guest::at<IniSection>(p)) : nullptr;
}

void ini_destruct(IniFile *ini) { ini_clear(ini); }

IniSection *ini_find_section(IniFile *ini, const MsString *name) {
    for (uint32_t i = 0; i < ini->count; i++) {
        IniSection &s = ini->sections.get()[i];
        if (stricmp_guest(name->c_str(), s.name.c_str()) == 0) return &s;
    }
    return nullptr;
}

// The section called name, created (with room for 10 entries) if missing.
IniSection *ini_add_section(IniFile *ini, const MsString *name) {
    if (IniSection *s = ini_find_section(ini, name)) return s;
    if (ini->count >= ini->cap) {
        ini->cap += 10;
        uint32_t p = array_new(ini->cap, sizeof(IniSection), STRING_CTOR, STRING_DTOR);
        if (ini->sections) {
            guest::call_cdecl(0x498168, p, ini->sections.a, ini->count * static_cast<uint32_t>(sizeof(IniSection)));
            if (ini->sections) delete_sections(ini->sections.a);
        }
        ini->sections = ptr32<IniSection>(&guest::at<IniSection>(p));
    }
    IniSection *s = &ini->sections.get()[ini->count];
    s->count = 0;
    s->cap = 10;
    uint32_t e = array_new(10, sizeof(IniEntry), ENTRY_CTOR, ENTRY_DTOR);
    s->entries = e ? ptr32<IniEntry>(&guest::at<IniEntry>(e)) : nullptr;
    if (&s->name != name) s->name.assign(*name);
    ini->count++;
    return s;
}

IniEntry *ini_find(IniSection *s, const MsString *key) {
    for (uint32_t i = 0; i < s->count; i++) {
        IniEntry &e = s->entries.get()[i];
        if (stricmp_guest(key->c_str(), e.key.c_str()) == 0) return &e;
    }
    return nullptr;
}

// Looks up section/key: stores the int value and the type; 0 if missing.
int ini_get(IniFile *ini, const char *section, const char *key, int32_t *value, int32_t *type) {
    guest::Local<MsString> name;
    name->init();
    name->assign(section, cstr_len(section));
    IniSection *s = ini_find_section(ini, name.get());
    name->destroy();
    if (!s) return 0;
    name->assign(key, cstr_len(key));
    IniEntry *e = ini_find(s, name.get());
    name->destroy();
    if (!e) return 0;
    *value = e->ival;
    *type = e->type;
    return 1;
}

// Adds an entry unless the key exists (an existing entry is returned unchanged).
IniEntry *ini_add(IniSection *s, const char *key, int32_t ival, float fval, const MsString *text, int32_t type) {
    guest::Local<MsString> k;
    k->init();
    k->assign(key, cstr_len(key));
    IniEntry *found = ini_find(s, k.get());
    if (k->cap >= 16) guest::call_stdcall(0x403830, k->ptr, k->cap + 1);
    if (found) return found;
    uint32_t old_cap = s->cap;
    if (s->count >= old_cap) {
        s->cap = old_cap + 10;
        uint32_t p = array_new(old_cap + 10, sizeof(IniEntry), ENTRY_CTOR, ENTRY_DTOR);
        if (s->entries) {
            guest::call_cdecl(0x498168, p, s->entries.a, s->count * static_cast<uint32_t>(sizeof(IniEntry)));
            if (s->entries) delete_entries(s->entries.a);
        }
        s->entries = p ? ptr32<IniEntry>(&guest::at<IniEntry>(p)) : nullptr;
    }
    IniEntry *e = &s->entries.get()[s->count];
    e->key.assign(key, cstr_len(key));
    e->type = type;
    e->ival = ival;
    e->fval = fval;
    if (&e->sval != text) e->sval.assign(*text);
    s->count++;
    return e;
}

IniEntry *ini_set_int(IniSection *s, const char *key, int32_t v) {
    guest::Local<MsStringStream> ss;
    guest::Local<MsString> text;
    ss->construct();
    *ss << v;
    ss->str(text.get());
    IniEntry *e = ini_add(s, key, v, static_cast<float>(v), text.get(), INI_INT);
    if (text->cap >= 16) guest::call_stdcall(0x403830, text->ptr, text->cap + 1);
    ss->destruct();
    return e;
}

IniEntry *ini_set_float(IniSection *s, const char *key, float v) {
    guest::Local<MsStringStream> ss;
    guest::Local<MsString> text;
    ss->construct();
    *ss << v;
    ss->str(text.get());
    IniEntry *e = ini_add(s, key, guest::to_int(v), v, text.get(), INI_FLOAT);
    if (text->cap >= 16) guest::call_stdcall(0x403830, text->ptr, text->cap + 1);
    ss->destruct();
    return e;
}

IniEntry *ini_set_string(IniSection *s, const char *key, const char *v) {
    guest::Local<MsString> text;
    text->init();
    text->assign(v, cstr_len(v));
    IniEntry *e = ini_add(s, key, 0, 0.0f, text.get(), INI_STRING);
    if (text->cap >= 16) guest::call_stdcall(0x403830, text->ptr, text->cap + 1);
    return e;
}

// Reads gr.ini from the current directory into ini (on top of what it holds): "[section]" lines open a
// section; "key = value" lines add an int (digits), a float (digits with a '.') or a string (anything
// else, trailing spaces removed); ';' starts a comment. Returns 0 if the file cannot be read.
int ini_load(IniFile *ini) {
    guest::Local<GameFile> f;
    guest::Local<MsString> name;
    struct Line {
        char key[0x400];
        char value[0x400];  // directly after key: the trimming below may look at key[0x3ff]
    };
    guest::Local<Line> line;
    file_construct(f.get());
    name->init();
    name->assign(&guest::at<char>(0x49d9dc) /* "gr.ini" */, 6);
    bool opened = file_open(f.get(), name.get(), FM_IN | FM_BINARY);
    if (name->cap >= 16) guest::call_stdcall(0x403830, name->ptr, name->cap + 1);
    if (!opened) {
        file_destruct(f.get());
        return 0;
    }
    uint32_t size = static_cast<uint32_t>(file_size(f.get()));
    uint32_t text_p = guest::call_cdecl(0x496c14, size + 1);  // operator new
    guest::call_cdecl(0x497d10, text_p, 0u, size + 1);        // memset
    char *text = &guest::at<char>(text_p);
    uint32_t got = static_cast<uint32_t>(file_read(f.get(), text, size));
    file_close(f.get());
    if (size != got) {
        file_destruct(f.get());
        return 0;
    }
    uint32_t pos = 0;
    IniSection *section = nullptr;
    if (size != 0) {
        do {
            bool comment = false, has_eq = false, numeric = true;
            line->key[0] = 0;
            line->value[0] = 0;
            while (text[pos] == ' ' || text[pos] == '\t') pos++;
            char *start = text + pos;
            bool eol = false;
            for (; pos < size; pos++) {
                char c = text[pos];
                if (c == '\n' || c == '\r') {
                    eol = true;
                    break;
                }
                if (c == '\t') text[pos] = ' ';
                c = text[pos];
                if (c == ';') {
                    comment = true;
                    continue;
                }
                if (comment) continue;
                if (has_eq && numeric && !(c >= '0' && c <= '9') && c != '.' && c != ' ') numeric = false;
                if (c == '=') has_eq = true;
            }
            if (eol)
                while (pos < size && (text[pos] == '\n' || text[pos] == '\r')) pos++;
            if (*start == '[') {
                guest::call_cdecl(0x46f4e0, guest::addr(start), 0x49cddcu /* "[%[^]]]" */, guest::addr(line->key));
                name->init();
                name->assign(line->key, cstr_len(line->key));
                section = ini_add_section(ini, name.get());
                if (name->cap >= 16) guest::call_stdcall(0x403830, name->ptr, name->cap + 1);
                name->init();
            } else if (has_eq) {
                guest::call_cdecl(0x46f4e0, guest::addr(start), 0x49cde4u /* "%[^ =] = %[^;\r\n]" */,
                                  guest::addr(line->key), guest::addr(line->value));
                if (numeric) {
                    if (guest::call_import(IMP_strchr, guest::addr(line->value), static_cast<uint32_t>('.'))) {
                        guest::call_import(IMP_atof, guest::addr(line->value));
                        float v = static_cast<float>(guest::st0());
                        ini_set_float(section, line->key, v);
                    } else {
                        int v = static_cast<int>(guest::call_import(IMP_atoi, guest::addr(line->value)));
                        ini_set_int(section, line->key, v);
                    }
                } else {
                    int i = static_cast<int>(cstr_len(line->value)) - 1;
                    while (line->value[i] == ' ') {  // (i may reach -1: key[0x3ff] is looked at)
                        if (static_cast<uint32_t>(i) >= 0x400) guest::call_cdecl(0x496d71);  // range check failure
                        line->value[i] = 0;
                        i--;
                    }
                    ini_set_string(section, line->key, line->value);
                }
            }
        } while (pos < size);
    }
    guest::call_cdecl(0x496bd7, text_p);  // free
    file_destruct(f.get());
    return 1;
}

// gr.ini settings (globals 0x4a4dc8..0x4a4df0, int32 each).
GLOBAL(int32_t, g_cfg_video_memory, 0x4a4dd0);
GLOBAL(int32_t, g_cfg_faster_preview, 0x4a4dd4);
GLOBAL(int32_t, g_cfg_tyre_autosort, 0x4a4dd8);
GLOBAL(int32_t, g_cfg_alt_speed, 0x4a4de8);

// Reads gr.ini: every setting starts at its default and is replaced by an integer value of the same
// name in the file (other types are ignored); screenshot sizes are capped at 2560 x 1920. The section
// and key names are the original's strings (addresses in the table).
void settings_load() {
    g_cfg_replay_rate = 5;
    g_cfg_replay_length = 10;
    g_cfg_video_memory = 0;
    g_cfg_faster_preview = 0;
    g_cfg_tyre_autosort = 1;
    g_cfg_view_angle = 0x7fff;
    g_cfg_rotation = 0x7fff;
    g_cfg_zoom = 0;
    g_cfg_alt_speed = 0;
    g_screenshot_w = 320;
    g_screenshot_h = 240;
    guest::Local<IniFile> ini;
    ini_construct(ini.get());
    if (ini_load(ini.get())) {
        static const struct { uint32_t section, key; int32_t *value; } kSettings[] = {
            {0x49d5ec, 0x49d9e4, &g_cfg_replay_rate},     // Replay
            {0x49d5ec, 0x49d9f0, &g_cfg_replay_length},
            {0x49da04, 0x49d9f8, &g_cfg_video_memory},   // Graphics
            {0x49da04, 0x49da10, &g_cfg_faster_preview},
            {0x49da04, 0x49da24, &g_cfg_tyre_autosort},
            {0x49da40, 0x49da34, &g_cfg_view_angle},     // Viewport
            {0x49da40, 0x49da4c, &g_cfg_rotation},
            {0x49da40, 0x49da58, &g_cfg_zoom},
            {0x49da78, 0x49da60, &g_cfg_alt_speed},
            {0x49da78, 0x49da80, &g_screenshot_w},
            {0x49da78, 0x49da90, &g_screenshot_h},
        };
        guest::Local<int32_t> value, type;
        for (const auto &s : kSettings) {
            if (ini_get(ini.get(), &guest::at<char>(s.section), &guest::at<char>(s.key), value.get(), type.get()) &&
                *type == INI_INT)
                *s.value = *value;
        }
        if (g_screenshot_w > 0xa00) g_screenshot_w = 0xa00;
        if (g_screenshot_h > 0x780) g_screenshot_h = 0x780;
    }
    ini_clear(ini.get());
}
