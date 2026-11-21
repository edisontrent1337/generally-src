#include "game/file.h"

// calls of MSVCP140 members (thiscall, `this` in ECX)
static uint32_t imp(uint32_t slot) { return guest::at<uint32_t>(slot); }
static void tellg(GameFile *f, StreamPos *p) { guest::call_regs(imp(IMP_istream_tellg), f->istream(), 0, guest::addr(p)); }
static void tellp(GameFile *f, StreamPos *p) { guest::call_regs(imp(IMP_ostream_tellp), f->ostream(), 0, guest::addr(p)); }
static void clear(GameFile *f) { guest::call_regs(imp(IMP_ios_clear), f->ios(), 0, 0u, 0u); }
static void seekg(GameFile *f, int64_t off, int way) {
    guest::call_regs(imp(IMP_istream_seekg), f->istream(), 0, static_cast<uint32_t>(off),
                     static_cast<uint32_t>(off >> 32), way);
}
static void seekp(GameFile *f, int64_t off, int way) {
    guest::call_regs(imp(IMP_ostream_seekp), f->ostream(), 0, static_cast<uint32_t>(off),
                     static_cast<uint32_t>(off >> 32), way);
}
// seekg / seekp(fpos), the position passed by value (6 dwords)
static void seek_pos(uint32_t fn, uint32_t self, const StreamPos *p) {
    const uint32_t *d = reinterpret_cast<const uint32_t *>(p);
    guest::call_regs(imp(fn), self, 0, d[0], d[1], d[2], d[3], d[4], d[5]);
}
static int32_t pos32(const StreamPos &p) { return static_cast<int32_t>(p.fpos) + static_cast<int32_t>(p.off); }

static void reset_state(GameFile *f) {
    f->keep = f->unknown_e4 = f->keep_extra = f->unknown_ec = 0;
    f->unknown_f0 = 0;
}

GameFile *file_construct(GameFile *f) {
    guest::call_regs(0x4028c0, guest::addr(f), 0, guest::addr(f));  // std::fstream()
    f->path.init();
    reset_state(f);
    f->mode = 0;
    f->truncate = 0;
    return f;
}

void file_destruct(GameFile *f) {
    file_close(f);
    f->path.destroy();
    // ~fstream, inlined: vftable and vtordisp of the virtual base, then the members' destructors
    uint32_t self = guest::addr(f);
    int32_t vbase = guest::at<int32_t>(guest::at<uint32_t>(self) + 4);
    guest::at<uint32_t>(self + vbase) = 0x49b568;  // std::basic_fstream<char>::`vftable'
    guest::at<int32_t>(self + vbase - 4) = vbase - 0x78;
    guest::call_regs(0x403140, self + 0x18, 0);              // ~basic_filebuf
    guest::call_regs(imp(0x49b140), self + 0x20, 0);         // ~basic_iostream
    guest::call_regs(imp(0x49b108), self + 0x78, 0);         // ~basic_ios
}

bool file_open(GameFile *f, const MsString *path, uint32_t mode) {
    file_close(f);
    // basic_filebuf::open(name, mode, prot); prot is whatever ECX held: the security cookie left by
    // file_close's epilogue
    uint32_t ok = guest::call_regs(0x403750, guest::addr(f->stream) + 0x18, 0, guest::addr(path->c_str()), mode,
                                   guest::at<uint32_t>(0x4a400c));
    if (!ok) guest::call_regs(imp(IMP_ios_setstate), f->ios(), 0, 2u, 0u);
    else clear(f);
    if (f->ios_state() != 0) return false;
    if (&f->path != path) f->path.assign(*path);
    f->mode = mode;
    reset_state(f);
    f->truncate = 0;
    return true;
}

bool file_close(GameFile *f) {
    if (!f->is_open()) return false;
    if (!f->truncate || (f->mode & (FM_IN | FM_OUT)) != (FM_IN | FM_OUT)) {
        guest::call_regs(0x402860, guest::addr(f), 0);  // fstream::close
        f->path.clear();
        f->mode = 0;
        f->truncate = 0;
        reset_state(f);
        return true;
    }
    uint32_t n = f->keep_extra + f->keep;
    f->truncate = 0;
    reset_state(f);
    guest::Local<MsString> path;
    path->init();
    path->assign(f->path);
    file_seek(f, 0, 0);
    uint32_t buf = guest::call_cdecl(0x496c14, n);  // operator new
    file_read(f, &guest::at<uint8_t>(buf), n);
    file_close(f);
    file_open(f, path.get(), FM_OUT | FM_TRUNC | FM_BINARY);
    file_write(f, &guest::at<uint8_t>(buf), n);
    guest::call_cdecl(0x496bd7, buf);  // free
    bool r = file_close(f);
    guest::call_regs(0x4027a0, guest::addr(path.get()), 0);  // ~basic_string
    return r;
}

// Reads n bytes; returns how far the position moved.
int file_read(GameFile *f, void *buf, uint32_t n) {
    if (!f->is_open()) return 0;
    guest::Local<StreamPos> before, after;
    tellg(f, before.get());
    guest::call_regs(imp(IMP_istream_read), f->istream(), 0, guest::addr(buf), n, 0u);
    clear(f);
    if (f->mode & FM_OUT) {  // keep the put position with the get position
        tellg(f, after.get());
        seek_pos(IMP_ostream_seekp_pos, f->ostream(), after.get());
        clear(f);
    }
    tellg(f, after.get());
    return pos32(*after) - pos32(*before);
}

int file_write(GameFile *f, const void *buf, uint32_t n) {
    if (!f->is_open()) return 0;
    guest::Local<StreamPos> before, after;
    tellp(f, before.get());
    guest::call_regs(imp(IMP_ostream_write), f->ostream(), 0, guest::addr(buf), n, 0u);
    clear(f);
    if (f->mode & FM_IN) {
        tellp(f, after.get());
        seek_pos(IMP_istream_seekg_pos, f->istream(), after.get());
        clear(f);
    }
    tellp(f, after.get());
    return pos32(*after) - pos32(*before);
}

bool file_seek(GameFile *f, int off, int whence) {
    if (!f->is_open()) return false;
    if (f->mode & FM_IN) seekg(f, off, whence);
    if (f->mode & FM_OUT) seekp(f, off, whence);
    return (f->ios_state() & 6) == 0;
}

// Marks the file (open for reading and writing) to be cut at the current position when closed.
bool file_truncate_here(GameFile *f) {
    if (!f->is_open() || !(f->mode & FM_OUT) || !(f->mode & FM_IN)) return false;
    uint32_t pos = static_cast<uint32_t>(file_tell(f));
    f->truncate = 1;
    reset_state(f);
    f->keep = pos;
    return true;
}

int file_tell(GameFile *f) {
    guest::Local<StreamPos> p;
    if (f->mode & FM_IN) tellg(f, p.get());
    else if (f->mode & FM_OUT) tellp(f, p.get());
    else return 0;
    return pos32(*p);
}

int file_size(GameFile *f) {
    if (!f->is_open()) return 0;
    guest::Local<StreamPos> here, end;
    tellg(f, here.get());
    seekg(f, 0, 2);
    tellg(f, end.get());
    int size = pos32(*end);
    seekg(f, here->off + here->fpos, 0);
    return size;
}

// Arrays of records of int16 values, read one value at a time; the result is the number of bytes read.
static int read_i16s(GameFile *f, int16_t *v, uint32_t n, int per_record) {
    int total = 0;
    for (; n; n--)
        for (int k = 0; k < per_record; k++) total += file_read(f, v++, 2);
    return total;
}
int file_read_i16x2(GameFile *f, int16_t *v, uint32_t n) { return read_i16s(f, v, n, 2); }
int file_read_i16x3(GameFile *f, int16_t *v, uint32_t n) { return read_i16s(f, v, n, 3); }
int file_read_i16x4(GameFile *f, int16_t *v, uint32_t n) { return read_i16s(f, v, n, 4); }

// Records of 8 bytes: a byte, a padding byte (skipped in the file without being read: the stream
// positions move by 1) and three int16 values.
int file_read_tagged_i16x3(GameFile *f, uint8_t *rec, uint32_t n) {
    int total = 0;
    for (; n; n--, rec += 8) {
        total += file_read(f, rec, 1);
        if (f->is_open()) {
            if (f->mode & FM_IN) seekg(f, 1, 1);  // ios::cur
            if (f->mode & FM_OUT) seekp(f, 1, 1);
        }
        total += file_read(f, rec + 2, 2);
        total += file_read(f, rec + 4, 2);
        total += file_read(f, rec + 6, 2);
    }
    return total;
}

// Skips n bytes (the stream positions move by n without reading); 1 if the stream is still good, 0 if
// it failed or the file is not open.
int file_skip(GameFile *f, int n) {
    if (!f->is_open()) return 0;
    if (f->mode & FM_IN) seekg(f, n, 1);  // ios::cur
    if (f->mode & FM_OUT) seekp(f, n, 1);
    return (f->ios_state() & 6) == 0;  // neither failbit nor badbit
}

// WIN32_FIND_DATAA, the fields used
struct FindData {
    uint32_t attributes;
    uint8_t unknown04[0x28];
    char name[260];
    char short_name[14];
    uint8_t pad[2];
};
static_assert(sizeof(FindData) == 0x140);
constexpr uint32_t FILE_ATTRIBUTE_DIRECTORY = 0x10;

// Lists the entries of folder dir into out, sorted case-insensitively (less_nocase).
// mode: 1 files, 2 folders (bit set). ext: if non-empty only "*.ext" entries are listed, and their names
// lose the extension. max_len (if not 0): a name longer than this (without the extension) is replaced by
// its 8.3 short name. "." and ".." are never listed.
void list_directory(const MsString *dir, const MsString *ext, MsVector<MsString> *out, uint32_t mode,
                    uint32_t max_len) {
    bool has_ext = ext->size != 0;
    guest::Local<MsString> pattern, query;
    if (has_ext) {
        MsString::concat(pattern.get(), 0x49b57cu, ext);  // "\*." + ext
    } else {
        pattern->init();
        pattern->assign(&guest::at<char>(0x49b578), 2);  // "\*"
    }
    guest::call_regs(0x404d80, guest::addr(query.get()), guest::addr(dir), guest::addr(pattern.get()));  // dir + pattern
    pattern->destroy();
    uint32_t ext_len = ext->size ? ext->size + 1 : 0;  // with the dot

    guest::Local<FindData> fd;
    uint32_t h = guest::call_import(IMP_FindFirstFileA, guest::addr(query->c_str()), guest::addr(fd.get()));
    if (h != 0xffffffffu) {
        do {
            bool is_dir = fd->attributes & FILE_ATTRIBUTE_DIRECTORY;
            if (is_dir ? !(mode & 2) : !(mode & 1)) continue;
            guest::Local<MsString> name;
            name->init();
            name->assign(fd->name, static_cast<uint32_t>(std::strlen(fd->name)));
            uint32_t n = name->size;
            bool dot = std::memcmp(name->c_str(), ".", n < 1 ? n : 1) == 0 && n == 1;
            bool dotdot = std::memcmp(name->c_str(), "..", n < 2 ? n : 2) == 0 && n == 2;
            if (dot || dotdot) {
                name->destroy();
                continue;
            }
            if (max_len != 0 && max_len < n - ext_len)  // unsigned, as in the original
                name->assign(fd->short_name, static_cast<uint32_t>(std::strlen(fd->short_name)));
            if (name->size <= ext_len) {
                name->destroy();
                continue;
            }
            if (ext_len != 0) {
                guest::Local<MsString> dot_ext, tail;
                MsString::concat(dot_ext.get(), 0x49b580u, ext);  // "." + ext
                name->substr(tail.get(), name->size - ext_len, 0xffffffffu);
                int differs = static_cast<int>(
                    guest::call_import(IMP__stricmp, guest::addr(tail->c_str()), guest::addr(dot_ext->c_str())));
                tail->destroy();
                dot_ext->destroy();
                if (differs) {
                    guest::call_thiscall(0x4027a0, name.get());  // ~string
                    continue;
                }
                guest::call_thiscall(0x403970, name.get(), name->size - ext_len);  // erase(pos)
            }
            guest::call_thiscall(0x404510, out, name.get());  // push_back
            name->destroy();
        } while (guest::call_import(IMP_FindNextFileA, h, guest::addr(fd.get())));
        // std::sort(first, last, less_nocase)
        guest::call_regs(0x404fa0, out->first.a, out->last.a, static_cast<uint32_t>(out->size()), 0x403eb0u);
        guest::call_import(IMP_FindClose, h);
    }
    query->destroy();
}

// SHFILEOPSTRUCTA (byte-packed on 32-bit Windows)
#pragma pack(push, 1)
struct ShFileOp {
    uint32_t hwnd, func;
    uint32_t from, to;  // double-0-terminated path lists
    uint16_t flags;
    uint32_t aborted, mappings, title;
};
#pragma pack(pop)
static_assert(sizeof(ShFileOp) == 0x1e);

// Deletes a folder with its contents (SHFileOperation FO_DELETE, silent, no confirmation, no error UI).
bool delete_folder(const MsString *path) {
    uint32_t n = path->size;
    uint32_t from = guest::call_cdecl(0x496c14, n + 2);  // malloc
    guest::call_import(IMP_strncpy, from, guest::addr(path->c_str()), n);
    char *p = &guest::at<char>(from);
    p[n] = 0;
    p[n + 1] = 0;
    guest::Local<ShFileOp> op;
    op->hwnd = 0;
    op->func = 3;  // FO_DELETE
    op->from = from;
    op->to = 0;
    op->flags = 0x614;  // FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR
    bool ok = guest::call_import(IMP_SHFileOperationA, guest::addr(op.get())) == 0;
    guest::call_cdecl(0x496bd7, from);  // free
    return ok;
}
