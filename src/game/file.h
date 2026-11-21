// The game's file class: a std::fstream with its path and open mode, plus "truncate on close": a file
// open for reading and writing can be marked to be cut at a position, which close() does by reading the
// part to keep, reopening the file truncated and writing that part back.
#pragma once
#include "bridge/msvc.h"

// std::fpos<_Mbstatet> (MSVC 2015): the stream position is off + fpos
struct StreamPos {
    int64_t off;
    int64_t fpos;
    uint8_t state[8];
};
static_assert(sizeof(StreamPos) == 24);

GLOBAL(ptr32<char>, g_game_dir, 0x4a4d40);  // the game's folder (working directory for data files)

inline void enter_game_dir() {
    if (g_game_dir) guest::call_cdecl(guest::at<uint32_t>(IMP__chdir), g_game_dir);
}

enum FileMode : uint32_t {
    FM_IN = 1, FM_OUT = 2, FM_TRUNC = 0x10, FM_BINARY = 0x20,  // std::ios_base::openmode bits
};

struct GameFile {
    uint8_t stream[0xc0];  // std::fstream: istream +0, ostream +0x10, filebuf +0x18 (FILE* at +0x64)
    MsString path;         // +0xc0
    uint32_t mode;         // +0xd8, FileMode
    uint8_t truncate;      // +0xdc: cut the file at `keep` bytes when closing
    uint8_t pad[3];
    uint32_t keep;         // +0xe0
    uint32_t unknown_e4;
    uint32_t keep_extra;   // +0xe8, added to keep (always 0 here)
    uint32_t unknown_ec;
    uint64_t unknown_f0;

    bool is_open() const { return guest::at<uint32_t>(guest::addr(stream) + 0x64) != 0; }
    uint32_t istream() { return guest::addr(stream); }
    uint32_t ostream() { return guest::addr(stream) + 0x10; }
    uint32_t ios() { return guest::addr(stream) + guest::at<int32_t>(guest::at<uint32_t>(guest::addr(stream)) + 4); }
    uint32_t ios_state() { return guest::at<uint32_t>(ios() + 0xc); }
};
static_assert(sizeof(GameFile) == 0xf8);

GameFile *file_construct(GameFile *f);
void file_destruct(GameFile *f);
bool file_open(GameFile *f, const MsString *path, uint32_t mode);
bool file_close(GameFile *f);
int file_read(GameFile *f, void *buf, uint32_t n);
int file_write(GameFile *f, const void *buf, uint32_t n);
bool file_seek(GameFile *f, int off, int whence);
int file_read_i16x2(GameFile *f, int16_t *v, uint32_t n);
int file_read_i16x3(GameFile *f, int16_t *v, uint32_t n);
int file_read_i16x4(GameFile *f, int16_t *v, uint32_t n);
int file_read_tagged_i16x3(GameFile *f, uint8_t *rec, uint32_t n);
bool file_truncate_here(GameFile *f);
int file_tell(GameFile *f);
int file_size(GameFile *f);
int file_skip(GameFile *f, int n);
bool delete_folder(const MsString *path);
void list_directory(const MsString *dir, const MsString *ext, MsVector<MsString> *out, uint32_t mode,
                    uint32_t max_len);
