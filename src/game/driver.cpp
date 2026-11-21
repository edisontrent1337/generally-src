// Driver files (see driver.h).
#include "game/driver.h"

#include "game/file.h"

// "drivers\<name>.drv", built the way the original does: three temporaries, all freed after the file is
// opened (the path first, then the temporaries in reverse order; the heap sees the same order).
struct DriverPath {
    guest::Local<MsString> dir, with_sep, with_name, path;
    explicit DriverPath(const MsString *name) {
        dir->init();
        dir->assign(&guest::at<char>(0x49d7c4), 7);                     // "drivers"
        MsString::concat_move(with_sep.get(), dir.get(), 0x49ccfcu);     // + backslash
        MsString::concat_move(with_name.get(), with_sep.get(), name);
        MsString::concat_move(path.get(), with_name.get(), 0x49dad8u);   // + ".drv"
    }
    void destroy() {
        path->destroy();
        with_name->destroy();
        with_sep->destroy();
        dir->destroy();
    }
};

// Writes a driver to drivers/<name>.drv (the folder is created if missing): the format version 92, then
// the record.
void driver_save(Driver *d, const MsString *name) {
    enter_game_dir();
    guest::call_import(IMP__mkdir, 0x49d7c4u);  // "drivers"
    guest::Local<GameFile> f;
    file_construct(f.get());
    DriverPath p(name);
    bool ok = file_open(f.get(), p.path.get(), FM_OUT | FM_TRUNC | FM_BINARY);
    p.destroy();
    if (ok) {
        guest::Local<uint32_t> version;
        *version = 92;
        file_write(f.get(), version.get(), 4);
        driver_write(f.get(), d);
    }
    file_close(f.get());
    file_destruct(f.get());
}

// Reads drivers/<name>.drv into d if it exists and has format version 92; a
// car index beyond the installed cars becomes 0. The file is closed only when the version matched.
void driver_load(Driver *d, const MsString *name) {
    if (!d) return;
    enter_game_dir();
    guest::Local<GameFile> f;
    file_construct(f.get());
    DriverPath p(name);
    bool ok = file_open(f.get(), p.path.get(), FM_IN | FM_BINARY);
    p.destroy();
    if (ok) {
        guest::Local<uint32_t> version;
        *version = 0;
        file_read(f.get(), version.get(), 4);
        if (*version == 92) {
            driver_read(f.get(), d);
            file_close(f.get());
            if (d->car >= g_car_count) d->car = 0;
        }
    }
    file_destruct(f.get());
}

// The record as stored in a .drv file after the version: the statistics, the car and its colours, the
// keys, then each joystick binding (its last byte written as 0). Every field is its own write.
static constexpr struct { uint8_t off, size; } kHead[] = {
    {0x00, 4}, {0x04, 0x18}, {0x1c, 4}, {0x20, 4}, {0x24, 0x18}, {0x3c, 4}, {0x40, 4}, {0x44, 4},
    {0x48, 4}, {0x7c, 4}, {0x80, 4}, {0x84, 4}, {0x88, 4},
};

// Returns the bytes written.
int driver_write(GameFile *f, const Driver *d) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(d);
    guest::Local<uint32_t> zero;
    int total = 0;
    for (auto fl : kHead) total += file_write(f, p + fl.off, fl.size);
    for (int i = 0; i < CTRL_COUNT; i++) {
        const JoyBinding *b = &d->joy[i];
        total += file_write(f, &b->joy, 4);
        total += file_write(f, &b->index, 4);
        total += file_write(f, &b->unknown8, 1);
        total += file_write(f, &b->kind, 1);
        total += file_write(f, &b->negative, 1);
        total += file_write(f, zero.get(), 1);
    }
    return total;
}

// Reads a record written by driver_write; the pad byte of each binding is skipped (counting 1 if the
// stream is still good) and left as it was.
int driver_read(GameFile *f, Driver *d) {
    uint8_t *p = reinterpret_cast<uint8_t *>(d);
    int total = 0;
    for (auto fl : kHead) total += file_read(f, p + fl.off, fl.size);
    for (int i = 0; i < CTRL_COUNT; i++) {
        JoyBinding *b = &d->joy[i];
        total += file_read(f, &b->joy, 4);
        total += file_read(f, &b->index, 4);
        total += file_read(f, &b->unknown8, 1);
        total += file_read(f, &b->kind, 1);
        total += file_read(f, &b->negative, 1);
        total += file_skip(f, 1);
    }
    return total;
}

// Deletes drivers/<name>.drv (relative to the current folder).
void driver_delete(const MsString *name) {
    DriverPath p(name);
    guest::call_import(IMP__unlink, guest::addr(p.path->c_str()));
    p.destroy();
}
