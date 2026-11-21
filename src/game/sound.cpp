#include "game/sound.h"

#include <cstring>

static SoundRef *ref(uint32_t handle) {
    if (handle == 0 || handle > static_cast<uint32_t>(g_sounds.size())) return nullptr;
    return g_sounds[static_cast<int>(handle) - 1].get();
}

// Stores a SoundRef in the first free slot (or appends one) and returns its handle.
uint32_t sound_add(SoundRef *s) {
    MsVector<ptr32<SoundRef>> &v = g_sounds;
    for (uint32_t i = 0; i < static_cast<uint32_t>(v.size()); i++) {
        if (!v[static_cast<int>(i)]) {
            v[static_cast<int>(i)] = s;
            return i + 1;
        }
    }
    // push_back: grow through the original's vector code (0x48fbc0) when full
    if (v.last.a == v.end.a) guest::call_stdcall(0x48fbc0, 0u);
    if (v.last) *v.last = s;
    v.last.a += 4;
    return static_cast<uint32_t>(v.size());
}

// Frees all SoundRefs, and samples nobody uses any more.
void sounds_free() {
    for (uint32_t i = 0; i < static_cast<uint32_t>(g_sounds.size()); i++) {
        SoundRef *s = g_sounds[static_cast<int>(i)].get();
        if (!s) continue;
        if (Sample *smp = s->sample.get()) {
            if (--smp->refs == 0) {
                if (smp->data) {
                    guest::call_cdecl(0x496bd7, smp->data);  // free
                    smp->data = nullptr;
                }
                guest::op_delete(smp, sizeof(Sample));
                s->sample = nullptr;
            }
        }
        guest::op_delete(s, sizeof(SoundRef));
        g_sounds[static_cast<int>(i)] = nullptr;
    }
    g_sounds.last = g_sounds.first;
}

// Loads a WAV file and returns its handle, 0 on failure.
uint32_t sound_load(const MsString *path) {
    guest::Local<MsString> name;
    name->init();
    const char *p = path->c_str();
    name->assign(p, static_cast<uint32_t>(std::strlen(p)));
    SoundRef *s = sound_load_wav(name.get());
    name->destroy();
    return s ? sound_add(s) : 0;
}

// A new SoundRef on the same sample (own volume); returns its handle.
uint32_t sound_share(uint32_t handle) {
    SoundRef *s = ref(handle);
    if (!s) return 0;
    SoundRef *n = static_cast<SoundRef *>(guest::op_new(sizeof(SoundRef)));
    n->sample = nullptr;
    n->rate = 0;  // (as in the original)
    n->volume = 0;
    n->sample = s->sample;
    s->sample->refs++;
    return sound_add(n);
}

// Volume in hundredths of a decibel (DirectSound style, 0 = full, negative = quieter) -> 0..128.
void sound_set_volume(uint32_t handle, int millibel) {
    sound_set_attenuation(handle, static_cast<float>(millibel) / -2000.0f);
}

// Volume 128 / 10^e (e = 0: full volume).
void sound_set_attenuation(uint32_t handle, float e) {
    SoundRef *s = ref(handle);
    if (!s) return;
    guest::set_xmm(0, 10.0f);
    guest::set_xmm(1, e);
    guest::call_regs(0x48f860, 0, 0);  // std::pow(float, float)
    float f = guest::xmm0();
    s->volume = guest::to_int(128.0 / static_cast<double>(f) + 0.5);
}

// Starts the sound on the first free channel (none free: not played).
void sound_play(uint32_t handle) {
    SoundRef *s = ref(handle);
    if (!s) return;
    for (Channel &ch : g_channels) {
        if (ch.sound) continue;
        ch.sound = s;
        ch.loop = 0;
        ch.pos = 0;
        return;
    }
}

// Starts the sound looping on the first free channel (inlined in the original).
void sound_play_loop(uint32_t handle) {
    SoundRef *s = ref(handle);
    if (!s) return;
    for (Channel &ch : g_channels) {
        if (ch.sound) continue;
        ch.sound = s;
        ch.loop = 1;
        ch.pos = 0;
        return;
    }
}

// Stops the sound (inlined in the original).
void sound_stop_handle(uint32_t handle) {
    if (SoundRef *s = ref(handle)) sound_stop(s);
}

MsVector<ptr32<SoundRef>> *sounds() { return &g_sounds; }

// vector<SoundRef*>::_Reallocate: moves the handles to new storage for capacity n.
void sounds_reallocate(uint32_t n) {
    MsVector<ptr32<SoundRef>> &v = g_sounds;
    uint32_t mem = guest::call_regs(0x477b80, n, 0);  // allocator::allocate(n)
    guest::call_import(IMP_memmove, mem, v.first.a, (v.last.a - v.first.a) & ~3u);
    uint32_t count = (v.last.a - v.first.a) >> 2;
    if (v.first) guest::call_stdcall(0x4775b0, v.first, (v.end.a - v.first.a) >> 2);  // deallocate
    v.first.a = mem;
    v.end.a = mem + n * 4;
    v.last.a = mem + count * 4;
}
