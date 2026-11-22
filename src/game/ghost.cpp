#include "game/ghost.h"

#include <cstring>

#include "game/fmath.h"

#include "game/file.h"
#include "game/menu.h"
#include "game/selected_tracks.h"

static ptr32<GhostFrame> alloc_buffer() {
    // operator new (0x496c14), then the frames' (inlined) constructors zero them
    uint32_t p = guest::call_cdecl(0x496c14, static_cast<uint32_t>((kGhostFrames + 1) * sizeof(GhostFrame)));
    if (!p) return nullptr;
    GhostFrame *f = &guest::at<GhostFrame>(p);
    for (int i = 0; i < kGhostFrames + 1; i++) f[i] = GhostFrame{};
    return ptr32<GhostFrame>(f);
}

static void alloc_buffers() {
    if (!g_ghost_buf_a) g_ghost_buf_a = alloc_buffer();
    if (!g_ghost_buf_b) g_ghost_buf_b = alloc_buffer();
}

void ghost_init() {
    alloc_buffers();
    g_ghost_rec = g_ghost_buf_a;
    g_ghost_rec_len = 0;
    g_ghost_best_len = 0;
    g_ghost_max = kGhostFrames;
    g_ghost_best = nullptr;
}

void ghost_record(const Vec3 *a, const Vec3 *b, const Vec3 *c, const Vec3 *d) {
    if (!g_ghost_rec || g_ghost_rec_len >= kGhostFrames) return;
    GhostFrame &f = g_ghost_rec.get()[g_ghost_rec_len];
    f.v[0] = *a;
    f.v[1] = *b;
    f.v[2] = *c;
    f.v[3] = *d;
    g_ghost_rec_len = g_ghost_rec_len + 1;
}

// Pose of the best lap at `frame`; returns frame, or 0 if there is none.
int ghost_get(int frame, Vec3 *a, Vec3 *b, Vec3 *c, Vec3 *d) {
    if (g_ghost_rec_len > kGhostFrames || !g_ghost_best || frame < 0 || frame >= g_ghost_best_len) return 0;
    const GhostFrame &f = g_ghost_best.get()[frame];
    *a = f.v[0];
    *b = f.v[1];
    *c = f.v[2];
    *d = f.v[3];
    return frame;
}

// End of a lap: a complete recording faster than the best becomes the best; recording restarts.
void ghost_lap_done() {
    if (g_ghost_rec_len <= kGhostFrames && (g_ghost_best_len == 0 || g_ghost_rec_len < g_ghost_best_len)) {
        g_ghost_best_len = g_ghost_rec_len;
        g_ghost_best = g_ghost_rec;
        g_ghost_rec = g_ghost_rec == g_ghost_buf_a ? g_ghost_buf_b : g_ghost_buf_a;
    }
    lap_record_add();
    g_ghost_rec_len = 0;
}

// Enters the lap just recorded into the records if it beats the last one.
void lap_record_add() {
    uint16_t t = g_ghost_rec_len;
    if (t >= g_record_time[kRecords - 1]) return;
    int slot = kRecords - 1;
    for (int j = slot - 1; g_ghost_rec_len < g_record_time[j]; j--) {
        if (j <= 0) break;
        g_record_time[j + 1] = g_record_time[j];
        std::memcpy(g_record_name[slot], g_record_name[slot - 1], 13);
        std::memcpy(g_record_car[slot], g_record_car[slot - 1], 0x540);
        g_record_date[slot] = g_record_date[slot - 1];
        slot--;
    }
    g_record_time[slot] = g_ghost_rec_len;
    std::memcpy(g_record_car[slot], g_race_car, 0x540);
    std::memcpy(g_record_name[slot], g_player_name[0], 13);
    guest::Local<SystemTime> now;
    guest::call_import(IMP_GetSystemTime, now.get());
    g_record_date[slot] = *now;
}

// operator new[] of n packed frames (the size computation saturates on overflow)
static PackedGhostFrame *alloc_packed(uint32_t n) {
    uint64_t size = static_cast<uint64_t>(n) * sizeof(PackedGhostFrame);
    uint32_t p = guest::call_cdecl(0x496c14, size > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(size));
    return &guest::at<PackedGhostFrame>(p);
}

int ghost_read_packed(GameFile *f, PackedGhostFrame *p, int n) {
    int total = 0;
    for (; n != 0; n--, p++) {
        for (int i = 0; i < 3; i++) total += file_read(f, &p->pos[i], 4);
        for (int i = 0; i < 6; i++) total += file_read(f, &p->dir[i], 1);
        total += file_read(f, &p->pad, 2);
    }
    return total;
}

int ghost_write_packed(GameFile *f, const PackedGhostFrame *p, int n) {
    int total = 0;
    for (; n != 0; n--, p++) {
        for (int i = 0; i < 3; i++) total += file_write(f, &p->pos[i], 4);
        for (int i = 0; i < 6; i++) total += file_write(f, &p->dir[i], 1);
        total += file_write(f, &p->pad, 2);
    }
    return total;
}

// Packs every second frame of frames[0..n) (and the last one) into out; returns the number packed.
static int pack_frames(const GhostFrame *frames, const uint16_t &n, PackedGhostFrame *out) {
    int count = 0;
    for (uint16_t i = 0; i <= n; i += 2, count++, out++) {
        const GhostFrame &g = frames[i < n ? i : static_cast<uint16_t>(n - 1)];
        const float *v = reinterpret_cast<const float *>(&g);
        for (int k = 0; k < 3; k++) out->pos[k] = guest::to_int(v[k] * 150.0f + 0.5f);
        for (int k = 0; k < 3; k++) out->dir[k] = static_cast<int8_t>(guest::to_int(v[3 + k] * 120.0f + 0.5f));
        for (int k = 0; k < 3; k++) out->dir[3 + k] = static_cast<int8_t>(guest::to_int(v[9 + k] * 120.0f + 0.5f));
        out->pad = 0;
    }
    return count;
}

static float *fl(GhostFrame &g, int i) { return reinterpret_cast<float *>(&g) + i; }

// Inverse of pack_frames: normalizes the direction vectors, recomputes v[2] and interpolates the frames
// in between.
static void unpack_frames(GhostFrame *frames, uint16_t n, const PackedGhostFrame *packed) {
    uint16_t r = 0;
    for (uint16_t i = 0; i <= n; i += 2, r++) {
        int idx = i < n ? i : static_cast<uint16_t>(n - 1);
        GhostFrame &g = frames[idx];
        const PackedGhostFrame &p = packed[r];
        for (int k = 0; k < 3; k++) *fl(g, k) = static_cast<float>(p.pos[k]) / 150.0f;
        for (int k = 0; k < 3; k++) *fl(g, 3 + k) = static_cast<float>(p.dir[k]) / 120.0f;
        for (int k = 0; k < 3; k++) *fl(g, 9 + k) = static_cast<float>(p.dir[3 + k]) / 120.0f;
        Vec3 &a = g.v[1], &b = g.v[3];
        float len = guest_sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
        a.x = a.x / len;
        a.z = a.z / len;
        a.y = a.y / len;
        len = guest_sqrtf(b.x * b.x + b.y * b.y + b.z * b.z);
        b.x = b.x / len;
        b.z = b.z / len;
        b.y = b.y / len;
        g.v[2].x = b.y * a.z - b.z * a.y;  // v[2] = v[3] × v[1]
        g.v[2].y = b.z * a.x - b.x * a.z;
        g.v[2].z = b.x * a.y - b.y * a.x;
        if (i > 1 && i < n) {  // the frame in between: the average of its neighbours
            GhostFrame &mid = frames[idx - 1], &prev = frames[idx - 2];
            for (int k = 0; k < 12; k++) *fl(mid, k) = (*fl(g, k) + *fl(prev, k)) * 0.5f;
        }
    }
}

// Writes header n and `count` packed frames; returns their bytes, 0 on failure (the buffer is then not freed).
static int write_packed_frames(GameFile *f, uint16_t *n, PackedGhostFrame *packed, int count) {
    int bytes = count * static_cast<int>(sizeof(PackedGhostFrame));
    if (file_write(f, n, 2) != 2) return 0;
    if (ghost_write_packed(f, packed, count) != bytes) {
        file_seek(f, -4, 1);
        bytes = 0;
    }
    guest::call_cdecl(0x496bd7, guest::addr(packed));  // free
    return bytes;
}

// Reads a frame count (> max: failure) and its packed frames; returns the buffer, null on failure.
static PackedGhostFrame *read_packed_frames(GameFile *f, uint16_t *n, int *bytes, bool allow_empty) {
    guest::Local<uint32_t> header;
    *header = 0;
    if (file_read(f, header.get(), 2) != 2) return nullptr;
    *n = static_cast<uint16_t>(*header);
    if (*n > kGhostFrames || (!allow_empty && *n == 0)) return nullptr;
    PackedGhostFrame *packed = alloc_packed(*n);
    int records = (*n & 1) + (*n >> 1);
    *bytes = records * static_cast<int>(sizeof(PackedGhostFrame));
    if (ghost_read_packed(f, packed, records) != *bytes) return nullptr;  // (the buffer is not freed)
    return packed;
}

// Writes the best lap's ghost (frame count, then every second frame packed); returns the bytes of
// frames written, 0 on failure. The file is then marked to be truncated at the current position.
int ghost_save(GameFile *f) {
    uint16_t n = g_ghost_best_len;
    if (g_ghost_max <= n || !g_ghost_best || static_cast<uint16_t>(n - 1) > kGhostFrames - 2 || !f->is_open()) return 0;
    PackedGhostFrame *packed = alloc_packed(n);
    int count = pack_frames(g_ghost_best.get(), g_ghost_best_len, packed);
    if (file_write(f, &g_ghost_best_len, 2) != 2) return 0;
    int bytes = count * static_cast<int>(sizeof(PackedGhostFrame));
    if (ghost_write_packed(f, packed, count) != bytes) {
        file_seek(f, -4, 1);
        bytes = 0;
    }
    guest::call_cdecl(0x496bd7, guest::addr(packed));
    file_truncate_here(f);
    return bytes;
}

// Reads a ghost written by ghost_save into the buffer not being recorded into and makes it the best lap.
// Returns the bytes of packed frames read, 0 on failure.
int ghost_load(GameFile *f) {
    g_ghost_max = kGhostFrames;
    alloc_buffers();
    g_ghost_best_len = 0;
    g_ghost_best = nullptr;
    if (!f->is_open() || !g_ghost_rec || !g_ghost_buf_a || !g_ghost_buf_b) return 0;
    GhostFrame *frames = (g_ghost_rec == g_ghost_buf_b ? g_ghost_buf_a : g_ghost_buf_b).get();
    uint16_t n;
    int bytes;
    PackedGhostFrame *packed = read_packed_frames(f, &n, &bytes, true);
    if (!packed) return 0;
    g_ghost_best_len = n;
    g_ghost_max = n;
    g_ghost_best = ptr32<GhostFrame>(frames);
    unpack_frames(frames, n, packed);
    guest::call_cdecl(0x496bd7, guest::addr(packed));
    return bytes;
}

// The lap being recorded, saved with a replay (same format; an even frame count stores one frame less).
int replay_save(GameFile *f) {
    if (!g_ghost_rec || g_ghost_rec_len > kGhostFrames || !f->is_open()) return 0;
    if (g_ghost_rec_len == 0) {
        file_write(f, &g_ghost_rec_len, 2);
        return 0;
    }
    PackedGhostFrame *packed = alloc_packed(g_ghost_rec_len);
    int count = pack_frames(g_ghost_rec.get(), g_ghost_rec_len, packed);
    if (g_ghost_rec_len % 2 == 0) count--;
    return write_packed_frames(f, &g_ghost_rec_len, packed, count);
}

int replay_load(GameFile *f) {
    if (!f->is_open()) return 0;
    alloc_buffers();
    g_ghost_rec = g_ghost_best == g_ghost_buf_a ? g_ghost_buf_b : g_ghost_buf_a;
    g_ghost_rec_len = 0;
    uint16_t n;
    int bytes;
    PackedGhostFrame *packed = read_packed_frames(f, &n, &bytes, false);
    if (!packed) return 0;
    g_ghost_rec_len = n;
    unpack_frames(g_ghost_rec.get(), n, packed);
    guest::call_cdecl(0x496bd7, guest::addr(packed));
    return bytes;
}

// The dates of the 7 records in the track's record file: every field as its own 2-byte read or write.
// Returns the bytes transferred.
int record_dates_read(GameFile *f) {
    int total = 0;
    for (int i = 0; i < kRecords; i++) {
        uint16_t *v = &g_record_date[i].year;
        for (int k = 0; k < 8; k++) total += file_read(f, v + k, 2);
    }
    return total;
}

int record_dates_write(GameFile *f) {
    int total = 0;
    for (int i = 0; i < kRecords; i++) {
        const uint16_t *v = &g_record_date[i].year;
        for (int k = 0; k < 8; k++) total += file_write(f, v + k, 2);
    }
    return total;
}

// Car model records as stored in track files (lap records) and saves: field by field, with two pad
// fields (4 and 1 zero bytes); returns the bytes written.
static constexpr struct { int16_t off, size; } kModelFields[] = {  // off -1: padding
    {0x0, 0xc}, {-1, 4}, {0x10, 0xc}, {0x1c, 0x10}, {0x2c, 0x38}, {0x64, 0x38},
    {0x9c, 4}, {0xa0, 4}, {0xa4, 4}, {0xa8, 4}, {0xac, 4}, {0xb0, 4}, {0xb4, 4}, {0xb8, 4},
    {0xbc, 1}, {0xbd, 1}, {0xbe, 1}, {0xbf, 1}, {0xc0, 1}, {0xc1, 0x78}, {0x139, 0x28}, {0x161, 0x1e},
    {-1, 1},
};
static constexpr struct { int16_t off, count; } kModelVectors[] = {{0x180, 30}, {0x2e8, 30}, {0x450, 15}, {0x504, 5}};

int car_models_write(GameFile *f, const uint8_t *models, int count) {
    const auto &kFields = kModelFields;
    const auto &kVectors = kModelVectors;
    guest::Local<uint32_t> zero;
    *zero = 0;
    int total = 0;
    for (; count != 0; count--, models += 0x540) {
        for (auto fl : kFields) total += file_write(f, fl.off < 0 ? static_cast<const void *>(zero.get()) : models + fl.off, fl.size);
        for (auto v : kVectors)  // arrays of 3 floats
            for (int i = 0; i < v.count * 3; i++) total += file_write(f, models + v.off + 4 * i, 4);
    }
    return total;
}

// Reads car model records written by car_models_write: padding is skipped (counting 1 for each skip
// that leaves the stream good) and byte +0xc, after the name, is cleared. Returns the bytes read plus
// those counts.
int car_models_read(GameFile *f, uint8_t *models, int count) {
    int total = 0;
    for (; count != 0; count--, models += 0x540) {
        for (auto fl : kModelFields) {
            if (fl.off >= 0) {
                total += file_read(f, models + fl.off, fl.size);
            } else {
                total += file_skip(f, fl.size);
                if (fl.size == 4) models[0xc] = 0;
            }
        }
        for (auto v : kModelVectors)
            for (int i = 0; i < v.count * 3; i++) total += file_read(f, models + v.off + 4 * i, 4);
    }
    return total;
}

// Appends (or renews) the checksum trailer of a track file: a 4-byte marker and 16 bytes of checksum over
// the bytes from `start` to the trailer. A file whose 20th byte from the end does not hold the marker gets
// a new marker at its end first. (The marker is 0x4752 xor whether the seek before it succeeded.) A file
// open for reading and writing is then marked to be cut after the trailer.
void track_checksum_write(GameFile *f, int32_t start) {
    bool ok = file_seek(f, -20, 2);
    guest::Local<uint32_t> marker;
    *marker = 0;
    if (file_read(f, marker.get(), 4) != 4 || *marker != (static_cast<uint32_t>(ok) ^ 0x4752)) {
        bool at_end = file_seek(f, 0, 2);
        *marker = static_cast<uint32_t>(at_end) ^ 0x4752;
        file_write(f, marker.get(), 4);
    }
    uint32_t end = static_cast<uint32_t>(file_tell(f));
    file_seek(f, start, 0);
    uint32_t n = end - static_cast<uint32_t>(start);
    uint32_t buf = guest::call_cdecl(0x496c14, n + 1);  // malloc
    file_read(f, &guest::at<uint8_t>(buf), n);
    guest::Local<uint32_t[4]> sum;
    for (int i = 0; i < 4; i++) (*sum)[i] = g_checksum_seed[i];
    const uint8_t *bytes = &guest::at<uint8_t>(buf);
    for (uint32_t i = static_cast<uint32_t>(start); i < end; i++)
        (*sum)[i & 3] += (bytes[i - static_cast<uint32_t>(start)] + 1u) * (i + 1);
    guest::call_cdecl(0x496bd7, buf);  // free
    file_seek(f, static_cast<int>(end), 0);
    file_write(f, sum.get(), 16);
    if (f->is_open() && (f->mode & 3) == 3) {
        uint32_t keep = static_cast<uint32_t>(file_tell(f));
        f->truncate = 1;
        f->keep = keep;
        f->unknown_e4 = 0;
        f->keep_extra = 0;
        f->unknown_ec = 0;
        f->unknown_f0 = 0;
    }
}

// The record block of a track file: finds it (the int after the "GR" header; files without the header
// have it at the start), writes times, names, cars and dates there; returns its offset.
static int32_t records_write(GameFile *f) {
    guest::Local<int32_t> v;
    *v = 0;
    file_read(f, v.get(), 2);
    file_read(f, v.get(), 2);
    if (static_cast<int16_t>(*v) != 0x5247) file_seek(f, 0, 0);  // "GR"
    *v = 0;
    file_read(f, v.get(), 4);
    file_seek(f, *v, 0);
    file_write(f, g_record_time, sizeof g_record_time);
    file_write(f, g_record_name, sizeof g_record_name);
    car_models_write(f, g_record_car[0], kRecords);
    record_dates_write(f);
    return *v;
}

// Writes the track's lap records (times, names, cars, dates) into tracks\\<name>.trk, at the offset the
// file keeps for them (the int after the "GR" header; files without the header have it at the start), and
// cuts the file there.
void track_records_write(const MsString *name) {
    enter_game_dir();
    guest::Local<GameFile> f;
    file_construct(f.get());
    guest::Local<MsString> dir, t1, t2, path;
    dir->init();
    dir->assign(&guest::at<char>(0x49ccf4), 6);                 // "tracks"
    MsString::concat_move(t1.get(), dir.get(), 0x49ccfcu);       // + "\\"
    MsString::concat_move(t2.get(), t1.get(), name);
    MsString::concat_move(path.get(), t2.get(), 0x49cd00u);     // + ".trk"
    bool opened = file_open(f.get(), path.get(), FM_IN | FM_OUT | FM_BINARY);
    path->destroy();
    t2->destroy();
    t1->destroy();
    dir->destroy();
    if (opened) {
        records_write(f.get());
        file_truncate_here(f.get());
        file_close(f.get());
    }
    file_destruct(f.get());
}

// After a race: writes the lap records (and with one player the ghost) into the file of the track raced,
// tracks<path>.trk (selected tracks are named "\\name" or "\\folder\\name"), then renews the checksum
// from the record block on.
void track_records_save() {
    enter_game_dir();
    guest::Local<GameFile> f;
    file_construct(f.get());
    guest::Local<MsString> dir, t1, path;
    dir->init();
    dir->assign(&guest::at<char>(0x49ccf4), 6);  // "tracks"
    int i = g_current_track;
    uint32_t name = 0;
    if (i >= 0 && i < g_selected_tracks.size()) name = guest::addr(g_selected_tracks[i].c_str());
    MsString::concat_move(t1.get(), dir.get(), name);
    MsString::concat_move(path.get(), t1.get(), 0x49cd00u);  // + ".trk"
    bool opened = file_open(f.get(), path.get(), FM_IN | FM_OUT | FM_BINARY);
    path->destroy();
    t1->destroy();
    dir->destroy();
    if (opened) {
        int32_t at = records_write(f.get());
        if (g_player_count == 1) ghost_save(f.get());
        track_checksum_write(f.get(), at);
        file_close(f.get());
    }
    file_destruct(f.get());
}

// The checksum of a track file, tracks<name>.trk: the bytes after the size field (4 bytes in files from
// version 0x102 on, 2 before; old files without the "GR" header count as version 0x100), summed into
// four words from 1 each as track_checksum_write does. Returns the number of bytes summed, 0 if the file
// cannot be read or is newer than 0x124 (the sum then stays zero).
uint32_t track_file_checksum(const MsString *name, uint32_t sum[4]) {
    if (!sum) return 0;
    for (int i = 0; i < 4; i++) sum[i] = 0;
    enter_game_dir();
    guest::Local<GameFile> f;
    file_construct(f.get());
    guest::Local<MsString> t1, path;
    MsString::concat(t1.get(), 0x49ccf4u /* "tracks" */, name);
    MsString::concat_move(path.get(), t1.get(), 0x49cd00u);  // + ".trk"
    bool opened = file_open(f.get(), path.get(), FM_IN | FM_BINARY);
    path->destroy();
    t1->destroy();
    uint32_t n = 0;
    if (opened) {
        guest::Local<uint16_t[2]> head;  // version, "GR"
        file_read(f.get(), &(*head)[0], 2);
        file_read(f.get(), &(*head)[1], 2);
        bool readable = true;
        if ((*head)[1] != 0x5247) {
            guest::at<uint32_t>(guest::addr(&(*head)[0])) = 0x100;
            file_seek(f.get(), 0, 0);
        } else if ((*head)[0] > 0x124) {
            readable = false;
        }
        if (readable) {
            guest::Local<uint32_t> size;
            *size = 0;
            file_read(f.get(), size.get(), 4);
            *size = (*head)[0] < 0x102 ? *size - 2 : *size - 4;
            file_seek(f.get(), 0, 0);
            uint32_t buf = guest::call_cdecl(0x496c14, *size + 1);  // malloc
            if (static_cast<uint32_t>(file_read(f.get(), &guest::at<uint8_t>(buf), *size)) == *size) {
                file_close(f.get());
                for (int i = 0; i < 4; i++) sum[i] = 1;
                const uint8_t *bytes = &guest::at<uint8_t>(buf);
                for (uint32_t i = 0; i < *size; i++) sum[i & 3] += (bytes[i] + 1u) * (i + 1);
                guest::call_cdecl(0x496bd7, buf);  // free
                n = *size;
                file_destruct(f.get());
                return n;
            }
        }
        file_close(f.get());
    }
    file_destruct(f.get());
    return n;
}

// Whether the track file `name` (a guest C string) has the checksum `expected`.
bool track_checksum_matches(uint32_t name, const uint32_t expected[4]) {
    if (!name || !expected) return false;
    guest::Local<MsString> s;
    s->construct(name);
    guest::Local<uint32_t[4]> sum;
    bool read = track_file_checksum(s.get(), *sum) != 0;
    s->destroy();
    if (!read) return false;
    return std::memcmp(*sum, expected, 16) == 0;
}

// Finds the track a saved game refers to: name is "\\<folder>\\<track>" (or "\\<track>"). If the file of
// <track> in tracks\\ itself has the checksum (or none is given), *out = "\\<track>"; otherwise each
// subfolder of tracks\\ is tried and the first "\\<folder>\\<track>" with the checksum is taken.
// 1 when found, 0 if not.
uint32_t track_find_by_checksum(const char *name, const uint32_t sum[4], MsString *out) {
    enter_game_dir();
    const char *base = reinterpret_cast<const char *>(
        &guest::at<char>(guest::call_cdecl(guest::at<uint32_t>(IMP_strrchr), guest::addr(name), 0x5cu)));
    if (track_checksum_matches(guest::addr(base), sum)) {
        out->assign(base, static_cast<uint32_t>(std::strlen(base)));
        return 1;
    }
    guest::Local<MsVector<MsString>> folders;
    folders->first = nullptr;
    folders->last = nullptr;
    folders->end = nullptr;
    {
        guest::Local<MsString> ext, dir;
        ext->init();
        ext->assign(&guest::at<char>(0x49b574), 0);  // ""
        dir->init();
        dir->assign(&guest::at<char>(0x49ccf4), 6);  // "tracks"
        list_directory(dir.get(), ext.get(), folders.get(), 2, 0x14);
        dir->destroy();
        ext->destroy();
    }
    uint32_t found = 0;
    int n = folders->size();
    for (int i = 0; i < n; i++) {
        guest::Local<MsString> t, cand;
        MsString::concat(t.get(), 0x49ccfcu, &(*folders)[i]);  // "\\" + folder
        MsString::concat_move(cand.get(), t.get(), guest::addr(base));
        t->destroy();
        if (sum) {
            guest::Local<MsString> path;
            path->construct(guest::addr(cand->c_str()));
            guest::Local<uint32_t[4]> got;
            bool ok = track_file_checksum(path.get(), *got) != 0;
            path->destroy();
            if (ok && std::memcmp(*got, sum, 16) == 0) {
                if (out != cand.get()) out->assign(*cand);
                cand->destroy();
                found = 1;
                break;
            }
        }
        cand->destroy();
    }
    if (folders->first) {
        guest::call_regs(0x4050e0, folders->first.a, folders->last.a, 0u, 0u);  // destroy the strings
        guest::call_stdcall(0x404c00, folders->first, (folders->end.a - folders->first.a) / static_cast<uint32_t>(sizeof(MsString)));
    }
    return found;
}
