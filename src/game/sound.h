// Sound samples and the channels that play them (the mixer itself lives in the SDL audio callback).
//
// A loaded sample (Sample) can be shared: each user holds a SoundRef with its own volume, and the game
// refers to SoundRefs by handle = index + 1 into g_sounds (0 = none).
#pragma once
#include "bridge/msvc.h"

struct Sample {
    ptr32<int16_t> data;  // PCM in the device format (16-bit stereo, 44.1 kHz)
    uint32_t length;      // in 16-bit values
    int32_t refs;         // SoundRefs using this sample
};

struct SoundRef {
    ptr32<Sample> sample;
    float rate;      // playback speed, 1 = normal
    int32_t volume;  // 0..128
};

GLOBAL(MsVector<ptr32<SoundRef>>, g_sounds, 0x4aafec);
MsVector<ptr32<SoundRef>> *sounds();
// The sound a handle (1-based index into g_sounds) refers to, or null.
inline SoundRef *sound_ref(uint32_t h) {
    if (!h || h > static_cast<uint32_t>(g_sounds.size())) return nullptr;
    return g_sounds[static_cast<int>(h) - 1].get();
}

// A playing sound.
struct Channel {
    ptr32<SoundRef> sound;  // null = free
    float pos;              // in stereo frames
    int32_t loop;
};
constexpr int CHANNEL_COUNT = 16;
GLOBAL(Channel[CHANNEL_COUNT], g_channels, 0x4ab068);
GLOBAL(ptr32<int16_t>[CHANNEL_COUNT], g_channel_buf, 0x4ab028);  // per-channel mix buffers

// SDL_AudioSpec
struct AudioSpec {
    int32_t freq;
    uint16_t format;
    uint8_t channels, silence;
    uint16_t samples, padding;
    uint32_t size;
    uint32_t callback, userdata;
};
static_assert(sizeof(AudioSpec) == 0x18);
GLOBAL(ptr32<AudioSpec>, g_audio, 0x4a4c2c);  // the device's format, null while closed

bool audio_open();
void audio_close();
SoundRef *sound_create(const void *pcm, uint32_t bytes);
SoundRef *sound_load_wav(const MsString *path);
void audio_mix(uint32_t userdata, int16_t *stream, uint32_t len);
void sound_ref_free(SoundRef *s);
void sound_stop(SoundRef *s);

uint32_t sound_add(SoundRef *s);
void sounds_free();
void sounds_reallocate(uint32_t n);
uint32_t sound_load(const MsString *path);
uint32_t sound_share(uint32_t handle);
void sound_set_volume(uint32_t handle, int millibel);
void sound_set_attenuation(uint32_t handle, float e);
void sound_play(uint32_t handle);
void sound_play_loop(uint32_t handle);
void sound_stop_handle(uint32_t handle);
