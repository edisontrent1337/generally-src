// Loading a track file: tracks\<name>.trk (0x43d390).
//
// File layout. A file starting with u16 version, u16 "GR" has a header; older files (version 0x100) start
// directly with the trailer offset.
//   u32 offset just past the lap length (the trailer), u8 track size, u8 water level (units of 0.05),
//   i16 camera tilt, i16 camera rotation (degrees); from 0x105 on: u8 zoom (percent), u8 paint the
//   finish line
//   tiles: run count (u16 before 0x103, else u32), runs u16 (length << 4 | kind), see apply_tile_run
//   heights: u16 byte count, the 64 x 64 grid
//   objects: count (u8 before 0x102, else u16), records (u8 type, u16 x, u16 z, u16 angle)
//   pit boxes: u8 count, records (u16 x, u16 z, u16 angle)
//   checkpoints: u8 count, records (u16 x0, z0, x1, z1); from 0x120 on: pit lane gates, alike
//   racing line: count (u8 before 0x102, else u16), points (u16 x, z); pit line: u8 count, points
//   trailer: lap length (u16 before 0x102, else u32), the hot-lap records, the ghost lap; before it,
//   optionally, the author (30 bytes) and description (500 bytes), marked by 0xffff just before the length
// Positions are in half tiles, angles in 1/10000 radians. The checksum over the file (up to the trailer
// offset) seeds the records' checksum.
#include "game/file.h"
#include "game/fmath.h"
#include "game/ghost.h"
#include "game/ini.h"
#include "game/menu.h"
#include "game/objects.h"
#include "game/race_state.h"
#include "game/render.h"
#include "game/selected_tracks.h"
#include "game/track.h"
#include "game/track_info.h"
#include "bridge/imports.h"

namespace {

uint32_t malloc_guest(uint64_t n) {  // 0x496c14; a size that overflowed asks for 0xffffffff bytes
    return guest::call_cdecl(0x496c14, n > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(n));
}
void free_guest(uint32_t p) { guest::call_cdecl(0x496bd7, p); }
void free_global(uint32_t &p) {
    if (p) {
        free_guest(p);
        p = 0;
    }
}

float half_tiles(int v) { return static_cast<float>(v) * g_tile_size * 0.5f; }

// rand() * -range / 32768, truncated: 0 down to -(range - 1) for a whole range
int neg_random(float range) {
    const int r = static_cast<int>(guest::call_import(IMP_rand));
    return guest::to_int(static_cast<float>(r) * -3.0517578125e-05f * range);
}

uint8_t &tile_at(int i) { return guest::at<uint8_t>(g_tile_map + static_cast<uint32_t>(i)); }

// A random tile up to range - 1 tiles before (x, z), shifted back by `back`, clamped to the map.
uint8_t &tile_near(int x, int z, float range, int back) {
    int tx = (x - neg_random(range)) - back;
    int tz = (z - neg_random(range)) - back;
    if (tx < 0) tx = 0; else if (tx > 0x1ff) tx = 0x1ff;
    if (tz < 0) tz = 0; else if (tz > 0x1ff) tz = 0x1ff;
    return tile_at(tz * 0x200 + tx);
}

// Paints tile i (the map starts as type 0x10 everywhere). Types >= 0x28 are worn tiles; most kinds keep a
// tile worn, as the worn type of what they paint.
void apply_tile_run(int i, int kind) {
    uint8_t &t = tile_at(i);
    const int x = i % 0x200, z = i / 0x200;
    switch (kind) {
    case 0: t = t < 0x28 ? 2 : 0x29; break;
    case 1: t = t < 0x28 ? 1 : 0x29; break;
    case 2:  // three random tiles nearby: types 8..10 where still 0x10, worn ones become 0x2a
        for (int k = 0; k < 3; k++) {
            uint8_t &n = tile_near(x, z, 5.0f, 2);
            if (n >= 0x28) n = 0x2a;
            else if (n == 0x10) n = static_cast<uint8_t>(8 - neg_random(2.5f));
        }
        break;
    case 4:
        if (t < 0x28) t = static_cast<uint8_t>(12 - neg_random(2.0f));
        else t = 0x2a;
        break;
    case 5: {  // wears a random tile nearby
        uint8_t &n = tile_near(x, z, 13.0f, 6);
        n = worn_tile(n);
        break;
    }
    case 6: {  // types 0x14..0x16 here and on a random tile nearby (unless that is 0x18..0x1b)
        uint8_t &n = tile_near(x, z, 7.0f, 3);
        if ((n & 0xfc) != 0x18) n = static_cast<uint8_t>(0x14 - neg_random(2.5f));
        if (t < 0x28) t = static_cast<uint8_t>(0x14 - neg_random(2.5f));
        else t = 0x2c;
        break;
    }
    case 7: t = static_cast<uint8_t>(0x18 - neg_random(2.0f)); break;
    case 8: t = static_cast<uint8_t>(0x1a - neg_random(2.0f)); break;
    case 9: t = t < 0x28 ? 0x1c : 0x2d; break;
    case 10: t = t < 0x28 ? 0x20 : 0x2e; break;
    case 11: t = t < 0x28 ? 0x24 : 0x2f; break;
    case 12: t = t < 0x28 ? 0 : 0x28; break;
    default: break;
    }
}

// The tile runs' buffer has room for one more run when loading a race (spare = 1), not for the preview.
void read_tiles(GameFile *f, uint16_t version, int spare) {
    guest::Local<int32_t> runs;
    file_read(f, runs.get(), version < 0x103 ? 2 : 4);
    const uint32_t buf = malloc_guest(static_cast<uint64_t>(static_cast<uint32_t>(*runs + spare)) * 2);
    file_read(f, guest::host(buf), static_cast<uint32_t>(*runs) * 2);
    guest::call_cdecl(0x497d10, g_tile_map, 0x10, 0x40000);  // memset
    const uint16_t *run = &guest::at<uint16_t>(buf);
    int i = 0;
    for (int r = 0; r < *runs; r++)
        for (int k = 0; k < run[r] >> 4; k++) apply_tile_run(i++, run[r] & 0xf);
    if (buf) free_guest(buf);
}

// Objects; the menu preview sets only type, position and heading (and shows type '$' as '%').
void read_objects(GameFile *f, uint16_t version, bool preview) {
    if (version < 0x102) {
        guest::Local<uint8_t> n;
        file_read(f, n.get(), 1);
        g_object_count = *n;
    } else {
        file_read(f, &g_object_count, 2);
    }
    const uint32_t recs = malloc_guest(static_cast<uint64_t>(g_object_count) * 8);
    file_read_tagged_i16x3(f, &guest::at<uint8_t>(recs), g_object_count);
    free_global(g_objects.a);
    const int n = g_object_count;
    const uint32_t block = malloc_guest(static_cast<uint64_t>(n) * sizeof(TrackObject));
    if (block) {
        for (int k = 0; k < n; k++) {
            TrackObject &o = guest::at<TrackObject>(block + static_cast<uint32_t>(k) * sizeof(TrackObject));
            o.pos = Vec3();
            o.heading = Vec3();
            o.vel = Vec3();
        }
    }
    g_objects.a = block;
    for (int k = 0; k < g_object_count; k++) {
        const uint16_t *r = &guest::at<uint16_t>(recs + static_cast<uint32_t>(k) * 8);
        TrackObject &o = g_objects.get()[k];
        o.type = static_cast<uint8_t>(r[0]);
        o.pos = Vec3(half_tiles(r[1]), 0.0f, half_tiles(r[2]));
        const float a = static_cast<float>(r[3]) / 10000.0f;
        const float s = guest_sinf(a);
        const float c = guest_cosf(a);
        o.heading = Vec3(c, 0.0f, -s);
        if (preview) {
            if (o.type == '$') o.type = '%';
            continue;
        }
        o.vel = Vec3();
        *reinterpret_cast<uint16_t *>(o.unknown1c) = 0;
        o.mass = 0.001f;
        o.radius = 0.8f;
    }
    if (recs) free_guest(recs);
}

void pit_box_init(PitBox &b, int k, const uint16_t *r) {
    b.centre = Vec3(half_tiles(r[0]), 0.0f, half_tiles(r[1]));
    b.centre.y = terrain_height(b.centre);
    const float a = static_cast<float>(r[2]) / 10000.0f;
    const float s = guest_sinf(a);
    const float c = guest_cosf(a);
    b.state = 6;
    b.heading = Vec3(c, 0.0f, -s);
    pit_crew_home(b);
    for (int j = 0; j < 8; j++) {
        b.crew_timer[j] = 0;
        b.crew_pos[j] = b.crew_target[j];
        b.crew_vel[j] = Vec3();
        b.crew_pos[j].y = terrain_height(b.crew_pos[j]) + 0.7f;
    }
    if (g_pit_shared) b.car = static_cast<uint8_t>(k * 8 - 0x40);
}

void read_pits(GameFile *f) {
    file_read(f, &g_pit_count, 1);
    const uint32_t recs = malloc_guest(static_cast<uint64_t>(g_pit_count) * 6);
    file_read_i16x3(f, &guest::at<int16_t>(recs), g_pit_count);
    free_global(g_pits.a);
    const int n = g_pit_count;
    const uint32_t block = malloc_guest(static_cast<uint64_t>(n) * sizeof(PitBox));
    if (block)
        for (int k = 0; k < n; k++) pit_box_construct(&guest::at<PitBox>(block + static_cast<uint32_t>(k) * sizeof(PitBox)));
    g_pits.a = block;
    for (int k = 0; k < g_pit_count; k++)
        pit_box_init(g_pits.get()[k], k, &guest::at<uint16_t>(recs + static_cast<uint32_t>(k) * 6));
    if (recs) free_guest(recs);
    if (g_pit_count > 6) g_pit_count = 6;
}

// A line (x0, z0, x1, z1 in half tiles) as start, unit direction, length.
void segment_from(Segment &s, const uint16_t *r) {
    s.x = half_tiles(r[0]);
    s.z = half_tiles(r[1]);
    s.dx = half_tiles(r[2] - r[0]);
    s.dz = half_tiles(r[3] - r[1]);
    s.length = guest_sqrtf(s.dz * s.dz + s.dx * s.dx);
    s.dx = s.dx / s.length;
    s.dz = s.dz / s.length;
}

// Checkpoints and gates: u8 count, then lines (start, end) stored as start, unit direction, length.
void read_segments(GameFile *f, uint8_t &count, uint32_t &segments) {
    file_read(f, &count, 1);
    const uint32_t recs = malloc_guest(static_cast<uint64_t>(count) * 8);
    file_read_i16x4(f, &guest::at<int16_t>(recs), count);
    free_global(segments);
    segments = malloc_guest(static_cast<uint64_t>(count) * sizeof(Segment));
    for (int k = 0; k < count; k++) {
        const uint16_t *r = &guest::at<uint16_t>(recs + static_cast<uint32_t>(k) * 8);
        segment_from(guest::at<Segment>(segments + static_cast<uint32_t>(k) * sizeof(Segment)), r);
    }
    if (recs) free_guest(recs);
}

// The finish line (the last checkpoint) as tile type 0x1c on the plain tiles (< 8) it crosses: seven
// lines 0.5 tiles apart, centred on it.
void paint_finish_line(const Segment &s) {
    float off = -1.5f;
    do {
        const float length = s.length / g_tile_size;
        float x = off * s.dz + s.x / g_tile_size;
        float z = s.z / g_tile_size - off * s.dx;
        for (float t = 0.0f; t < length; t = t + 0.5f) {
            if (0.0f <= x && x < 512.0f && 0.0f <= z && z < 512.0f) {
                uint8_t &tile = tile_at(guest::to_int(x) + guest::to_int(z) * 0x200);
                if (tile < 8) tile = 0x1c;
            }
            x = x + s.dx * 0.5f;
            z = z + s.dz * 0.5f;
        }
        off = off + 0.5f;
    } while (off < 1.6f);
}

// The racing line and the pit line: points on the ground.
void read_line(GameFile *f, uint32_t count, uint32_t &points) {
    const uint32_t recs = malloc_guest(static_cast<uint64_t>(count) * 4);
    file_read_i16x2(f, &guest::at<int16_t>(recs), count);
    free_global(points);
    const uint32_t block = malloc_guest(static_cast<uint64_t>(count) * sizeof(Vec3));
    if (block)
        for (uint32_t k = 0; k < count; k++) guest::at<Vec3>(block + k * sizeof(Vec3)) = Vec3();
    points = block;
    for (uint32_t k = 0; k < count; k++) {
        const uint16_t *r = &guest::at<uint16_t>(recs + k * 4);
        Vec3 &p = guest::at<Vec3>(points + k * sizeof(Vec3));
        p.x = half_tiles(r[0]);
        p.z = half_tiles(r[1]);
        p.y = terrain_height(p);
    }
    if (recs) free_guest(recs);
}

// The racing-line points nearest to the pit line extended by one step at either end.
void find_pit_ends() {
    const Vec3 *line = &guest::at<Vec3>(g_race_line);
    const Vec3 *pit = &guest::at<Vec3>(g_pit_line);
    const int n = g_pit_line_count;
    const float far = g_track_size * g_track_size;
    g_pit_entry = 0;
    float best = far;
    const float ez = pit[0].z * 2.0f - pit[1].z;
    for (int i = 0; i < g_race_line_count; i++) {
        const float dx = line[i].x - (pit[0].x * 2.0f - pit[1].x);
        const float dy = line[i].y - (pit[0].y * 2.0f - pit[1].y);
        const float d = dx * dx + dy * dy + (line[i].z - ez) * (line[i].z - ez);
        if (d < best) {
            g_pit_entry = static_cast<uint8_t>(i);
            best = d;
        }
    }
    g_pit_rejoin = 0;
    best = far;
    for (int i = 0; i < g_race_line_count; i++) {
        const float dx = line[i].x - (pit[n - 1].x * 2.0f - pit[n - 2].x);
        const float dy = line[i].y - (pit[n - 1].y * 2.0f - pit[n - 2].y);
        const float dz = line[i].z - (pit[n - 1].z * 2.0f - pit[n - 2].z);
        const float d = dx * dx + dy * dy + dz * dz;
        if (d < best) {
            g_pit_rejoin = static_cast<uint8_t>(i);
            best = d;
        }
    }
}

void read_trailer(GameFile *f, uint16_t version, uint32_t trailer, bool ghost) {
    const uint32_t length_size = version < 0x102 ? 2 : 4;
    file_seek(f, static_cast<int>(trailer - length_size), 0);
    g_track_length = 0;
    file_read(f, &g_track_length, length_size);
    file_read(f, g_record_time, 0xe);
    file_read(f, g_record_name, 0x5b);
    car_models_read(f, g_record_car[0], kRecords);
    record_dates_read(f);
    if (ghost && g_player_count == 1) ghost_load(f);
}

void checksum(GameFile *f, uint32_t size) {
    g_checksum_seed[3] = 1;
    g_checksum_seed[2] = 1;
    g_checksum_seed[1] = 1;
    g_checksum_seed[0] = 1;
    file_seek(f, 0, 0);
    const uint32_t buf = malloc_guest(size + 1);
    const uint32_t got = static_cast<uint32_t>(file_read(f, guest::host(buf), size));
    if (got == size && size != 0) {
        const uint8_t *bytes = &guest::at<uint8_t>(buf);
        for (uint32_t i = 0; i < size; i++) g_checksum_seed[i & 3] += (bytes[i] + 1u) * (i + 1);
    }
    free_guest(buf);
}

// The race camera's axes from its tilt and rotation (degrees). Returns cos / sin of the tilt.
void camera_axes(int16_t tilt, int16_t rotation, float &cos_tilt, float &sin_tilt) {
    const double t = static_cast<double>(tilt) * 0.01745329252;
    cos_tilt = static_cast<float>(guest_cos(t));
    sin_tilt = static_cast<float>(guest_sin(t));
    const double r = static_cast<double>(rotation) * 0.01745329252;
    const float cr = static_cast<float>(guest_cos(r));
    const float sr = static_cast<float>(guest_sin(r));
    g_view_right = Vec3(cr, 0.0f, sr);
    g_view_down = Vec3(sr * sin_tilt, -cos_tilt, -(cr * sin_tilt));
    g_view_forward = Vec3(-(sr * cos_tilt), -sin_tilt, cr * cos_tilt);
}

// Places the camera eye so that the track's corners (2 above the ground) fit the screen: for each pair of
// corners the distance at which they fill the width and the height, the eye at the nearest of these, then
// pulled in by the zoom (in a race, zooms below 1 are corrected for the screen's aspect; the menu
// preview fits the height a little more loosely).
void camera_place(float cos_tilt, float sin_tilt, bool race) {
    const uint8_t *h = g_height_map.get();
    const float size = g_track_size;
    const Vec3 &R = g_view_right, &D = g_view_down, &F = g_view_forward;
    const float hy[4] = {
        static_cast<float>(h[0]) * 0.05f + 2.0f, static_cast<float>(h[0x3f]) * 0.05f + 2.0f,
        static_cast<float>(h[0xfc0]) * 0.05f + 2.0f, static_cast<float>(h[0xfff]) * 0.05f + 2.0f,
    };
    const float cx[4] = {0.0f, size, 0.0f, size};
    const float cz[4] = {0.0f, 0.0f, size, size};
    struct { float r, d, f; } p[4];  // right, down, forward
    for (int k = 0; k < 4; k++) {
        p[k].r = R.y * hy[k] + R.x * cx[k] + R.z * cz[k];
        p[k].d = D.y * hy[k] + D.x * cx[k] + D.z * cz[k];
        p[k].f = F.y * hy[k] + F.x * cx[k] + F.z * cz[k];
    }
    const float aspect = g_screen_aspect;
    const float k = race ? (aspect * 11.0f) / 12.0f : aspect;
    float near_r = 0.0f, near_d = 0.0f, eye_r = 0.0f, eye_d = 0.0f;
    for (int i = 0; i < 3; i++) {
        for (int j = i + 1; j < 4; j++) {
            const auto &a = p[i], &b = p[j];
            const float m = (a.f + b.f) * 0.5f;
            if (b.r <= a.r) {
                const float t = (b.r + m) - a.r;
                if (t < near_r) {
                    eye_r = (b.f - t) * 0.5f + b.r;
                    near_r = t;
                }
            } else {
                const float t = (m + a.r) - b.r;
                if (t < near_r) {
                    eye_r = (a.f - t) * 0.5f + a.r;
                    near_r = t;
                }
            }
            if (b.d <= a.d) {
                const float t = (b.d - a.d) / k + m;
                if (t < near_d) {
                    eye_d = (b.f - t) * aspect * 0.5f + b.d;
                    near_d = t;
                }
            } else {
                const float t = (a.d - b.d) / k + m;
                if (t < near_d) {
                    eye_d = (a.f - t) * aspect * 0.5f + a.d;
                    near_d = t;
                }
            }
        }
    }
    float dist = near_r;
    if (near_d < dist) dist = near_d;
    if (race && g_view_zoom < 1.0f) {
        if (0.75f < aspect)
            g_view_zoom = g_view_zoom + ((aspect * 4.0f) / 3.0f - 1.0f) * (1.0f - g_view_zoom) * sin_tilt;
        if (aspect < 0.75f)
            g_view_zoom = g_view_zoom + ((1.0f - (aspect * 4.0f) / 3.0f) * (1.0f - g_view_zoom) * cos_tilt) / (1.0f - cos_tilt);
        if (1.0f < g_view_zoom) g_view_zoom = 1.0f;
    }
    const float zoom = g_view_zoom;
    g_view_eye.x = (R.x * eye_r + D.x * eye_d) + F.x * dist * zoom;
    g_view_eye.y = (R.y * eye_r + D.y * eye_d) + F.y * dist * zoom;
    g_view_eye.z = (R.z * eye_r + D.z * eye_d) + F.z * dist * zoom;
}

struct Header {
    uint16_t version;
    uint32_t trailer;
    int16_t tilt, rotation;
    uint8_t paint_finish;
};

// The header up to the zoom; false (the file closed) for a version newer than 0x124.
bool read_header(GameFile *f, Header &h) {
    guest::Local<uint16_t> version, magic;
    guest::Local<uint8_t> paint_finish;
    *paint_finish = 1;
    file_read(f, version.get(), 2);
    file_read(f, magic.get(), 2);
    if (*magic == 0x5247) {  // "GR"
        if (*version > 0x124) {
            file_close(f);
            return false;
        }
    } else {
        *version = 0x100;
        file_seek(f, 0, 0);
    }
    h.version = *version;
    guest::Local<uint32_t> trailer;
    file_read(f, trailer.get(), 4);
    guest::Local<uint8_t> byte;
    file_read(f, byte.get(), 1);
    g_track_size = static_cast<float>(*byte);
    g_height_cell = g_track_size / 63.0f;
    g_tile_size = g_track_size * 0.001953125f;
    file_read(f, byte.get(), 1);
    g_water_level = static_cast<float>(*byte) * 0.05f;
    guest::Local<int16_t> tilt, rotation;
    file_read(f, tilt.get(), 2);
    file_read(f, rotation.get(), 2);
    g_view_zoom = 1.05f;
    if (h.version >= 0x105) {
        file_read(f, byte.get(), 1);
        g_view_zoom = static_cast<float>(*byte) * 0.01f;
        file_read(f, paint_finish.get(), 1);
    }
    h.trailer = *trailer;
    h.tilt = *tilt;
    h.rotation = *rotation;
    h.paint_finish = *paint_finish;
    return true;
}

void read_heights(GameFile *f) {
    guest::Local<uint16_t> bytes;
    file_read(f, bytes.get(), 2);
    file_read(f, g_height_map.get(), *bytes);
}

// Reads an opened track file; false (the file closed) for a version newer than 0x124.
bool read_track(GameFile *f) {
    Header h;
    if (!read_header(f, h)) return false;
    const uint16_t ver = h.version;
    if (g_cfg_zoom > 0) g_view_zoom = static_cast<float>(g_cfg_zoom) * 0.01f;
    if (g_cfg_view_angle != 0x7fff) h.tilt = static_cast<int16_t>(g_cfg_view_angle);
    if (g_cfg_rotation != 0x7fff) h.rotation = static_cast<int16_t>(g_cfg_rotation);
    float cos_tilt, sin_tilt;
    camera_axes(h.tilt, h.rotation, cos_tilt, sin_tilt);

    read_tiles(f, ver, 1);
    read_heights(f);
    read_objects(f, ver, false);
    read_pits(f);
    read_segments(f, g_checkpoint_count, g_checkpoints);
    if (h.paint_finish)
        paint_finish_line(guest::at<Segment>(g_checkpoints + static_cast<uint32_t>(g_checkpoint_count - 1) * sizeof(Segment)));
    g_gate_count = 0;
    if (ver >= 0x120) read_segments(f, g_gate_count, g_gates);
    if (ver < 0x102) {
        guest::Local<uint8_t> byte;
        file_read(f, byte.get(), 1);
        g_race_line_count = *byte;
    } else {
        file_read(f, &g_race_line_count, 2);
    }
    read_line(f, g_race_line_count, g_race_line);
    file_read(f, &g_pit_line_count, 1);
    read_line(f, g_pit_line_count, g_pit_line);
    find_pit_ends();
    read_trailer(f, ver, h.trailer, true);
    checksum(f, h.trailer);
    file_close(f);
    camera_place(cos_tilt, sin_tilt, true);
    return true;
}

// The menu preview's reading of an opened file: no gr.ini overrides, the finish line always painted,
// pit boxes and checkpoints skipped (only the finish line is read), the author and description, no ghost.
void read_preview(GameFile *f) {
    Header h;
    if (!read_header(f, h)) return;
    const uint16_t ver = h.version;
    float cos_tilt, sin_tilt;
    camera_axes(h.tilt, h.rotation, cos_tilt, sin_tilt);
    read_tiles(f, ver, 0);
    read_heights(f);
    read_objects(f, ver, true);
    file_read(f, &g_pit_count, 1);
    file_seek(f, g_pit_count * 6, 1);
    file_read(f, &g_checkpoint_count, 1);
    file_seek(f, g_checkpoint_count * 8 - 8, 1);
    guest::Local<uint16_t[4]> line;
    file_read_i16x4(f, reinterpret_cast<int16_t *>(*line), 1);
    Segment finish;
    segment_from(finish, *line);
    paint_finish_line(finish);
    if (ver >= 0x120) {
        file_read(f, &g_gate_count, 1);
        file_seek(f, g_gate_count * 8 - 8, 1);
        guest::Local<uint16_t[4]> gate;
        file_read_i16x4(f, reinterpret_cast<int16_t *>(*gate), 1);
    }
    g_track_author[0] = 0;
    g_track_description[0] = 0;
    file_seek(f, static_cast<int>(h.trailer - (ver < 0x102 ? 4 : 6)), 0);
    guest::Local<uint16_t> marker;
    file_read(f, marker.get(), 2);
    if (*marker == 0xffff) {
        file_seek(f, static_cast<int>(h.trailer - (ver < 0x102 ? 0x216 : 0x218)), 0);
        file_read(f, g_track_author, 0x1e);
        g_track_author[0x1e] = 0;
        file_read(f, g_track_description, 0x1f4);
        g_track_description[0x1f4] = 0;
    }
    read_trailer(f, ver, h.trailer, false);
    file_close(f);
    camera_place(cos_tilt, sin_tilt, false);
}

}  // namespace

int track_load() {
    enter_game_dir();
    guest::Local<GameFile> file;
    GameFile *f = file_construct(file.get());
    guest::Local<MsString> dir, name_path, path;
    dir->init();
    dir->assign(&guest::at<char>(0x49ccf4), 6);  // "tracks"
    uint32_t name = 0;
    if (g_current_track >= 0 && g_current_track < g_selected_tracks.size())
        name = guest::addr(g_selected_tracks[g_current_track].c_str());
    MsString::concat_move(name_path.get(), dir.get(), name);
    MsString::concat_move(path.get(), name_path.get(), 0x49cd00u);  // + ".trk"
    const bool opened = file_open(f, path.get(), FM_IN | FM_BINARY);
    path->destroy();
    name_path->destroy();
    dir->destroy();
    const int result = opened && read_track(f) ? 0 : -1;
    file_destruct(f);
    return result;
}

void track_preview_load(const MsString *name) {
    enter_game_dir();
    guest::Local<MsString> dir, t1, t2, path;
    dir->init();
    dir->assign(&guest::at<char>(0x49ccf4), 6);  // "tracks"
    MsString::concat_move(t1.get(), dir.get(), 0x49ccfcu);  // + "\\"
    MsString::concat_move(t2.get(), t1.get(), name);
    MsString::concat_move(path.get(), t2.get(), 0x49cd00u);  // + ".trk"
    t2->destroy();
    t1->destroy();
    dir->destroy();
    guest::Local<GameFile> file;
    GameFile *f = file_construct(file.get());
    if (file_open(f, path.get(), FM_IN | FM_BINARY)) read_preview(f);
    file_destruct(f);
    path->destroy();
}
