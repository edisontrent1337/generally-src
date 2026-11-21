// The race options (see race_options.h).
#include "game/race_options.h"

#include <cstring>

#include "game/video.h"

// the fields in file order with their sizes (strings without their terminator)
static constexpr struct { uint8_t off, size; } kFields[] = {
    {0x00, 2}, {0x02, 7}, {0x09, 1}, {0x0a, 1}, {0x0b, 1}, {0x0c, 1}, {0x0d, 1}, {0x0e, 1}, {0x10, 4},
    {0x14, 4}, {0x18, 1}, {0x19, 1}, {0x1a, 1}, {0x1b, 1}, {0x1c, 1}, {0x1e, 2}, {0x20, 1}, {0x21, 1},
    {0x22, 1},
};

// Writes the global options: the fields, then the three strings each followed by a 0 byte, and a final
// 2-byte 0. Returns the bytes written.
int race_options_write(GameFile *f) {
    const uint8_t *o = reinterpret_cast<const uint8_t *>(&g_race_options);
    guest::Local<uint32_t> zero;
    int total = 0;
    for (auto fl : kFields) total += file_write(f, o + fl.off, fl.size);
    for (int i = 0; i < 3; i++) {
        total += file_write(f, g_race_options.text[i], 0x16);
        total += file_write(f, zero.get(), i < 2 ? 1 : 2);
    }
    return total;
}

// Reads options written by race_options_write into o; the 0 bytes after the strings are skipped (each
// skip counts 1 if the stream is still good) and the strings get their terminators.
int race_options_read(GameFile *f, RaceOptions *o) {
    uint8_t *p = reinterpret_cast<uint8_t *>(o);
    int total = 0;
    for (auto fl : kFields) total += file_read(f, p + fl.off, fl.size);
    for (int i = 0; i < 3; i++) {
        total += file_read(f, o->text[i], 0x16);
        total += file_skip(f, i < 2 ? 1 : 2);
        o->text[i][0x16] = 0;
    }
    return total;
}

// Writes options.set in the game folder: the format version 'J' (74), then the options.
void race_options_save() {
    enter_game_dir();
    guest::Local<GameFile> f;
    file_construct(f.get());
    guest::Local<MsString> name;
    name->init();
    name->assign(&guest::at<char>(0x49d748), 11);  // "options.set"
    bool ok = file_open(f.get(), name.get(), FM_OUT | FM_TRUNC | FM_BINARY);
    name->destroy();
    if (ok) {
        guest::Local<uint32_t> version;
        *version = 'J';
        file_write(f.get(), version.get(), 4);
        race_options_write(f.get());
    }
    file_close(f.get());
    file_destruct(f.get());
}

// The defaults used when options.set is missing or has another version. The three names are copied
// from the exe (0x49d754 the language, 0x49d75c the other two).
static void race_options_defaults() {
    uint8_t *o = reinterpret_cast<uint8_t *>(&g_race_options);
    auto put32 = [&](int off, uint32_t v) { std::memcpy(o + off, &v, 4); };
    auto put16 = [&](int off, uint16_t v) { std::memcpy(o + off, &v, 2); };
    put32(0x00, 0x06090005);  // 5 laps, ...
    put32(0x04, 0x00010204);
    put32(0x08, 0x1e1e1e01);
    put16(0x0c, 0x0032);
    o[0x0e] = 1;
    put32(0x10, 640);  // screen size
    put32(0x14, 480);
    put16(0x1b, 0x0100);
    put16(0x18, 0x0100);
    put32(0x1e, 0x50500100);
    o[0x22] = 0x50;
    o[0x1a] = 1;
    std::strcpy(g_race_options.text[0], &guest::at<char>(0x49d754));
    std::strcpy(g_race_options.text[1], &guest::at<char>(0x49d75c));
    std::strcpy(g_race_options.text[2], &guest::at<char>(0x49d75c));
}

// Reads options.set (version 'J'), else sets the defaults; the screen size comes from the options.
void race_options_load() {
    enter_game_dir();
    guest::Local<GameFile> f;
    file_construct(f.get());
    guest::Local<MsString> name;
    name->init();
    name->assign(&guest::at<char>(0x49d748), 11);  // "options.set"
    bool ok = file_open(f.get(), name.get(), FM_IN | FM_BINARY);
    name->destroy();
    bool use_defaults = true;
    if (ok) {
        guest::Local<uint32_t> version;
        *version = 0;
        file_read(f.get(), version.get(), 4);
        if (*version == 'J') {
            race_options_read(f.get(), &g_race_options);
            use_defaults = false;
        }
    }
    file_close(f.get());
    if (use_defaults) race_options_defaults();
    g_screen_w = g_race_options.i10;
    g_screen_h = g_race_options.i14;
    file_close(f.get());
    file_destruct(f.get());
}
