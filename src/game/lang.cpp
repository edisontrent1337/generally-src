// The language file (see lang.h).
#include "game/lang.h"

#include <cstring>

#include "game/file.h"

// Finds entry n in the text of a .lng file (len bytes): the marker "#NNNN#" (n as four digits), then the
// text between the next two double quotes. False if the marker or the opening quote is missing; a
// missing closing quote takes the text up to the end of the file minus its last character.
bool lang_find_entry(const char *text, uint32_t len, uint32_t n, MsString *out) {
    guest::Local<char[8]> marker;
    char *m = *marker;
    m[0] = '#';
    m[1] = static_cast<char>('0' + (n / 1000) % 10);
    m[2] = static_cast<char>('0' + (n / 100) % 10);
    m[3] = static_cast<char>('0' + (n / 10) % 10);
    m[4] = static_cast<char>('0' + n % 10);
    m[5] = '#';
    m[6] = 0;
    uint32_t i = 0;
    for (;; i++) {
        if (i >= len) return false;
        if (text[i] == '#' && guest::call_import(IMP_strncmp, guest::addr(m), guest::addr(text + i), 6u) == 0) break;
    }
    while (text[i++] != '"')
        if (i >= len) return false;
    if (i >= len) return false;
    uint32_t start = i;
    while (text[i++] != '"' && i < len) {
    }
    if (i > len) i = len;
    out->assign(text + start, i - start - 1);
    return true;
}

static constexpr uint32_t LANG_DEFAULTS = 0x49b5a0;  // const char *[LANG_COUNT]: the built-in texts

static void set_default(int n) {
    const char *s = &guest::at<char>(guest::at<uint32_t>(LANG_DEFAULTS + 4 * static_cast<uint32_t>(n)));
    g_lang[n].assign(s, static_cast<uint32_t>(std::strlen(s)));
}

// Loads lang\<name>.lng into g_lang; entries missing from the file (or every entry, when the file cannot
// be opened) get the built-in text.
void lang_load(const MsString *name) {
    enter_game_dir();
    guest::Local<GameFile> f;
    file_construct(f.get());
    guest::Local<MsString> dir, t1, t2, path;
    dir->init();
    dir->assign(&guest::at<char>(0x49d728), 4);             // "lang"
    MsString::concat_move(t1.get(), dir.get(), 0x49ccfcu);  // + "\\"
    MsString::concat_move(t2.get(), t1.get(), name);
    MsString::concat_move(path.get(), t2.get(), 0x49d720u);  // + ".lng"
    bool opened = file_open(f.get(), path.get(), FM_IN);
    path->destroy();
    t2->destroy();
    t1->destroy();
    dir->destroy();
    if (!opened) {
        for (int n = 0; n < LANG_COUNT; n++) set_default(n);
    } else {
        int size = file_size(f.get());
        uint32_t buf = guest::call_cdecl(0x496c14, static_cast<uint32_t>(size + 1));  // malloc
        uint32_t len = static_cast<uint32_t>(file_read(f.get(), &guest::at<char>(buf), static_cast<uint32_t>(size)));
        file_close(f.get());
        for (int n = 0; n < LANG_COUNT; n++)
            if (!lang_find_entry(&guest::at<char>(buf), len, static_cast<uint32_t>(n), &g_lang[n])) set_default(n);
        guest::call_cdecl(0x496bd7, buf);  // free
    }
    file_destruct(f.get());
}
