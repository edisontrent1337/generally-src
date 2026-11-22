// Loading the car models (0x485950): every cars\*.car file (u32 record size 0x540, u32 count, the
// records) is appended to g_cars. Without any, the built-in model "General" is made and saved as
// cars\general.car.
#include <cstring>

#include "game/driver.h"
#include "game/file.h"
#include "game/fmath.h"
#include "game/ghost.h"
#include "game/race_state.h"
#include "bridge/imports.h"

namespace {
uint32_t malloc_models(uint32_t n) {  // operator new[] with the overflow check
    const uint64_t bytes = static_cast<uint64_t>(n) * sizeof(CarModel);
    return guest::call_cdecl(0x496c14, bytes > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(bytes));
}
void free_guest(uint32_t p) { guest::call_cdecl(0x496bd7, p); }

// Appends the models of one file; false if it is missing or not a model file.
bool load_file(const MsString *name) {
    guest::Local<GameFile> file;
    GameFile *f = file_construct(file.get());
    guest::Local<MsString> dir, t1, t2, path;
    dir->init();
    dir->assign(&guest::at<char>(0x49dab0), 4);  // "cars"
    MsString::concat_move(t1.get(), dir.get(), 0x49ccfcu);  // + "\\"
    MsString::concat_move(t2.get(), t1.get(), name);
    MsString::concat_move(path.get(), t2.get(), 0x49dabcu);  // + ".car"
    const bool opened = file_open(f, path.get(), FM_IN | FM_BINARY);
    path->destroy();
    t2->destroy();
    t1->destroy();
    dir->destroy();
    if (opened) {
        guest::Local<uint32_t> size, count;
        file_read(f, size.get(), 4);
        file_read(f, count.get(), 4);
        if (*size == sizeof(CarModel) && *count != 0) {
            const uint32_t old = g_cars.a;
            const uint32_t n = static_cast<uint32_t>(g_car_count) + 1 + *count;
            const uint32_t block = malloc_models(n);
            if (block)
                for (uint32_t i = 0; i < n; i++) car_model_construct(&guest::at<uint8_t>(block + i * sizeof(CarModel)));
            g_cars.a = block;
            if (old) guest::call_cdecl(0x498168, block, old, static_cast<uint32_t>(g_car_count) * *size);  // memcpy
            file_read(f, &guest::at<uint8_t>(block + static_cast<uint32_t>(g_car_count) * sizeof(CarModel)), *count * *size);
            file_close(f);
            if (old) free_guest(old);
            g_car_count = g_car_count + static_cast<int32_t>(*count);
            file_destruct(f);
            return true;
        }
    }
    file_close(f);
    file_destruct(f);
    return false;
}

// The built-in model "General": a wedge of ten vertices and sixteen triangles.
void build_general(uint8_t *m) {
    auto u32 = [m](int off) -> uint32_t & { return *reinterpret_cast<uint32_t *>(m + off); };
    auto vec = [m](int off) -> Vec3 & { return *reinterpret_cast<Vec3 *>(m + off); };
    std::strcpy(reinterpret_cast<char *>(m), "General");
    // handling, grip, engine and drag values, as the bit patterns of their floats
    static constexpr struct { uint16_t off; uint32_t bits; } kValues[] = {
        {0x10, 0x3fc00000}, {0x14, 0x3f19999a}, {0x18, 0x3fe66666}, {0x2c, 0x3f666666}, {0x64, 0x3dcccccd},
        {0x30, 0x40033333}, {0x68, 0x3dcccccd}, {0x34, 0x40100000}, {0x6c, 0x3dcccccd}, {0x38, 0x3f99999a},
        {0x70, 0x3e99999a}, {0x3c, 0x3fcccccd}, {0x74, 0x3e99999a}, {0x40, 0x3fcccccd}, {0x78, 0x3e4ccccd},
        {0x44, 0x3fe66666}, {0x7c, 0x3e4ccccd}, {0x48, 0x3fcccccd}, {0x80, 0x3fb33333}, {0x4c, 0x3fe66666},
        {0x84, 0x3f333333}, {0x50, 0x3f666666}, {0x88, 0x3fb33333}, {0x54, 0x3f800000}, {0x8c, 0x3e4ccccd},
        {0x58, 0x3feccccd}, {0x90, 0x3dcccccd}, {0x5c, 0x40033333}, {0x94, 0x3dcccccd}, {0x60, 0x3f000000},
        {0x98, 0x3e99999a}, {0x9c, 0x40800000}, {0xa0, 0x42700000}, {0xa4, 0x442f0000}, {0xa8, 0x42531c72},
        {0xac, 0x3f000000}, {0xb0, 0x43fa0000}, {0xb4, 0x3f866666}, {0xb8, 0x406ccccd},
        {0x1c, 0x3f000000}, {0x20, 0x3f000000}, {0x24, 0x3f000000}, {0x28, 0x3f000000},
    };
    for (const auto &v : kValues) u32(v.off) = v.bits;

    // vertices (+0x180, count +0xbc)
    static constexpr float kVertex[10][3] = {
        {0.0f, 0.2f, 2.0f}, {0.0f, 0.5f, 0.6f},   {0.4f, 0.0f, 0.8f},  {-0.4f, 0.0f, 0.8f},  {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f}, {0.4f, 0.5f, -0.3f}, {-0.4f, 0.5f, -0.3f}, {0.0f, 0.2f, -1.5f}, {0.0f, 0.8f, -0.6f},
    };
    m[0xbc] = 10;
    for (int i = 0; i < 10; i++) vec(0x180 + 12 * i) = Vec3(kVertex[i][0], kVertex[i][1], kVertex[i][2]);
    // triangles: corner indices (+0xc1, count of indices +0xbe) and colours (+0x139)
    static constexpr uint8_t kTriangle[16][4] = {  // a, b, c, colour
        {0, 1, 2, 0}, {0, 3, 1, 0}, {4, 6, 8, 0}, {5, 8, 7, 0}, {6, 7, 9, 0}, {3, 0, 2, 4}, {4, 8, 5, 4}, {3, 2, 4, 4},
        {5, 3, 4, 4}, {7, 6, 1, 4}, {9, 8, 6, 0}, {7, 8, 9, 0}, {6, 2, 1, 0}, {1, 3, 7, 0}, {6, 4, 2, 0}, {3, 5, 7, 0},
    };
    m[0xbe] = 48;
    for (int t = 0; t < 16; t++) {
        for (int k = 0; k < 3; k++) m[0xc1 + 3 * t + k] = kTriangle[t][k];
        m[0x139 + t] = kTriangle[t][3];
    }
    // vertex normals (+0x2e8): the sum of the cross products of the triangles at the vertex, normalised
    for (int v = 0; v < m[0xbc]; v++) {
        Vec3 &n = vec(0x2e8 + 12 * v);
        n = Vec3(0.0f, 0.0f, 0.0f);
        for (int t = 0; 3 * t < m[0xbe]; t++) {
            const uint8_t *tri = m + 0xc1 + 3 * t;
            if (tri[0] != v && tri[1] != v && tri[2] != v) continue;
            const Vec3 &a = vec(0x180 + 12 * tri[0]), &b = vec(0x180 + 12 * tri[1]), &c = vec(0x180 + 12 * tri[2]);
            const float ey = c.y - a.y, ez = c.z - a.z, ex = c.x - a.x;
            const float fz = b.z - a.z, fy = b.y - a.y, fx = b.x - a.x;
            const float nz = n.z + (fy * ex - fx * ey);
            n.x = (fz * ey - fy * ez) + n.x;
            n.y = n.y + (fx * ez - fz * ex);
            n.z = nz;
        }
        const float len = guest_sqrtf(n.y * n.y + n.x * n.x + n.z * n.z);
        if (len <= 1e-4f) n = Vec3(0.0f, 1.0f, 0.0f);
        else n = Vec3(n.x / len, n.y / len, n.z / len);
    }
    // collision points (+0x450, count +0xbd) and their triangles (+0x161, count of indices +0xbf)
    static constexpr float kPoint[5][3] = {{0.0f, 0.2f, 2.0f}, {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.2f, -1.5f}, {0.0f, 1.0f, -0.3f}};
    m[0xbd] = 5;
    for (int i = 0; i < 5; i++) vec(0x450 + 12 * i) = Vec3(kPoint[i][0], kPoint[i][1], kPoint[i][2]);
    static constexpr uint8_t kPointIndex[12] = {0, 1, 4, 0, 2, 4, 3, 1, 4, 3, 2, 4};
    m[0xbf] = 12;
    std::memcpy(m + 0x161, kPointIndex, sizeof kPointIndex);
    // wheels (+0x504) and the fifth point
    vec(0x504) = Vec3(1.0f, -0.2f, 1.0f);
    vec(0x510) = Vec3(-1.0f, -0.2f, 1.0f);
    vec(0x51c) = Vec3(1.0f, -0.2f, -1.0f);
    vec(0x528) = Vec3(-1.0f, -0.2f, -1.0f);
    vec(0x534) = Vec3(0.0f, 0.6f, -0.05f);
    m[0xc0] = 0;
}

void save_general() {
    guest::Local<GameFile> file;
    GameFile *f = file_construct(file.get());
    guest::Local<MsString> dir, t1, path;
    dir->init();
    dir->assign(&guest::at<char>(0x49dab0), 4);  // "cars"
    MsString::concat_move(t1.get(), dir.get(), 0x49ccfcu);  // + "\\"
    MsString::concat_move(path.get(), t1.get(), 0x49daccu);  // + "general.car"
    const bool opened = file_open(f, path.get(), FM_OUT | FM_TRUNC | FM_BINARY);
    path->destroy();
    t1->destroy();
    dir->destroy();
    if (opened) {
        guest::Local<uint32_t> size;
        *size = sizeof(CarModel);
        file_write(f, size.get(), 4);
        file_write(f, &g_car_count, 4);
        car_models_write(f, reinterpret_cast<uint8_t *>(g_cars.get()), g_car_count);
    }
    file_close(f);
    file_destruct(f);
}
}  // namespace

void car_models_load() {
    enter_game_dir();
    guest::call_import(IMP__mkdir, 0x49dab0u);  // "cars"
    guest::Local<MsVector<MsString>> names;
    names->first = names->last = names->end = nullptr;
    {
        guest::Local<MsString> ext, dir;
        ext->init();
        ext->assign(&guest::at<char>(0x49dab8), 3);  // "car"
        dir->init();
        dir->assign(&guest::at<char>(0x49dab0), 4);  // "cars"
        list_directory(dir.get(), ext.get(), names.get(), 1, 0);
        dir->destroy();
        ext->destroy();
    }
    int loaded = 0;
    const uint32_t count = static_cast<uint32_t>(names->size());
    for (uint32_t i = 0; i < count; i++)
        if (load_file(&(*names)[static_cast<int>(i)])) loaded++;
    if (loaded == 0) {
        g_car_count = 1;
        if (g_cars) {
            free_guest(g_cars.a);
            g_cars = nullptr;
        }
        const uint32_t block = malloc_models(static_cast<uint32_t>(g_car_count));
        if (block)
            for (int32_t i = 0; i < g_car_count; i++) car_model_construct(&guest::at<uint8_t>(block + static_cast<uint32_t>(i) * sizeof(CarModel)));
        g_cars.a = block;
        build_general(&guest::at<uint8_t>(block));
        save_general();
    }
    if (names->first) {
        guest::call_regs(0x4050e0, names->first.a, names->last.a, 0u, 0u);  // destroy the strings
        guest::call_stdcall(0x404c00, names->first, (names->end.a - names->first.a) / static_cast<uint32_t>(sizeof(MsString)));
    }
}
