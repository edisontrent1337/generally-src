// The audio device: SDL audio opened at 44.1 kHz 16-bit stereo; the mixer callback (0x46f080) mixes the
// channels.
#include "game/sound.h"

#include <cstring>

static uint32_t crt_malloc(uint32_t n) { return guest::call_import(IMP_malloc, n); }
static void crt_free(uint32_t p) { guest::call_import(IMP_free, p); }

bool audio_open() {
    if (g_audio) return true;
    uint32_t want = crt_malloc(sizeof(AudioSpec));
    uint32_t have = crt_malloc(sizeof(AudioSpec));
    AudioSpec &w = guest::at<AudioSpec>(want);
    w.freq = 44100;
    w.channels = 2;
    w.format = 0x8010;  // AUDIO_S16LSB
    w.samples = 1024;
    w.callback = 0x46f080;
    w.userdata = 0;
    if (static_cast<int>(guest::call_import(IMP_SDL_OpenAudio, want, have)) < 0) {
        crt_free(want);
        crt_free(have);
        return false;
    }
    crt_free(want);
    g_audio = ptr32<AudioSpec>(&guest::at<AudioSpec>(have));
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        g_channel_buf[i] = ptr32<int16_t>(&guest::at<int16_t>(
            guest::call_cdecl(0x496c14, static_cast<uint32_t>(g_audio->samples) * g_audio->channels * 2)));
        g_channels[i].sound = nullptr;
        g_channels[i].loop = 0;
    }
    guest::call_import(IMP_SDL_PauseAudio, 0u);
    return true;
}

void audio_close() {
    if (!g_audio) return;
    guest::call_import(IMP_SDL_PauseAudio, 1u);
    guest::call_import(IMP_SDL_CloseAudio);
    crt_free(g_audio.a);
    g_audio = nullptr;
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (g_channel_buf[i]) {
            guest::call_cdecl(0x496bd7, g_channel_buf[i].a);  // free
            g_channel_buf[i] = nullptr;
        }
        g_channels[i].sound = nullptr;
        g_channels[i].loop = 0;
    }
}

// A new sample holding a copy of the PCM data, with one SoundRef at normal speed and full volume.
SoundRef *sound_create(const void *pcm, uint32_t bytes) {
    SoundRef *r = &guest::at<SoundRef>(guest::call_cdecl(0x496bdc, 0xcu));  // operator new
    r->sample = nullptr;
    r->rate = 0;
    r->volume = 0;
    Sample *s = &guest::at<Sample>(guest::call_cdecl(0x496bdc, 0xcu));
    s->data = nullptr;
    s->length = 0;
    r->sample = ptr32<Sample>(s);
    s->length = bytes >> 1;
    uint64_t size = static_cast<uint64_t>(s->length) * 2;
    s->data = ptr32<int16_t>(&guest::at<int16_t>(
        guest::call_cdecl(0x496c14, size > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(size))));
    s->refs = 1;
    guest::call_cdecl(0x498168, s->data.a, guest::addr(pcm), s->length * 2);  // memcpy
    r->rate = 1.0f;
    r->volume = 128;
    return r;
}

// Deletes a SoundRef; the sample goes with its last reference.
void sound_ref_free(SoundRef *r) {
    if (r->sample) {
        Sample *s = r->sample.get();
        s->refs--;
        if (s->refs == 0) {
            if (s->data) {
                guest::call_cdecl(0x496bd7, s->data.a);  // free
                s->data = nullptr;
            }
            guest::call_cdecl(0x496c1d, r->sample.a);  // operator delete
            r->sample = nullptr;
        }
    }
    guest::call_cdecl(0x496c1d, guest::addr(r));
}

// Stops every channel playing s.
void sound_stop(SoundRef *s) {
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        if (g_channels[i].sound.a == guest::addr(s)) {
            g_channels[i].pos = 0;
            g_channels[i].sound = nullptr;
        }
    }
}

// SDL_AudioCVT (SDL 1.2)
struct AudioCvt {
    int32_t needed;
    uint16_t src_format, dst_format;
    double rate_incr;
    uint32_t buf;
    int32_t len, len_cvt, len_mult;
    double len_ratio;
    uint32_t filters[10];
    int32_t filter_index;
    uint32_t pad;
};
static_assert(sizeof(AudioCvt) == 0x58);

// Loads a WAV file and converts it to the device format; null if the device is closed or the file
// cannot be read or converted.
SoundRef *sound_load_wav(const MsString *path) {
    if (!g_audio) return nullptr;
    struct Frame {  // the original's locals, in its stack order
        AudioCvt cvt;
        AudioSpec spec;
        uint32_t len, buf;
    };
    guest::Local<Frame> f;
    uint32_t rw = guest::call_import(IMP_SDL_RWFromFile, guest::addr(path->c_str()), 0x49cdd4u /* "rb" */);
    if (!guest::call_import(IMP_SDL_LoadWAV_RW, rw, 1u, guest::addr(&f->spec), guest::addr(&f->buf),
                            guest::addr(&f->len)))
        return nullptr;
    // format and channels are pushed as whole dwords from the spec
    const uint8_t *spec = reinterpret_cast<const uint8_t *>(&f->spec);
    uint32_t format_dw, channels_dw;
    std::memcpy(&format_dw, spec + 4, 4);
    std::memcpy(&channels_dw, spec + 6, 4);
    if (static_cast<int>(guest::call_import(IMP_SDL_BuildAudioCVT, guest::addr(&f->cvt), format_dw, channels_dw,
                                            static_cast<uint32_t>(f->spec.freq), static_cast<uint32_t>(g_audio->format),
                                            static_cast<uint32_t>(g_audio->channels),
                                            static_cast<uint32_t>(g_audio->freq))) == -1) {
        guest::call_import(IMP_SDL_FreeWAV, f->buf);
        return nullptr;
    }
    f->cvt.buf = crt_malloc(static_cast<uint32_t>(f->cvt.len_mult) * f->len);
    f->cvt.len = static_cast<int32_t>(f->len);
    guest::call_cdecl(0x498168, f->cvt.buf, f->buf, f->len);  // memcpy
    guest::call_import(IMP_SDL_FreeWAV, f->buf);
    guest::call_import(IMP_SDL_ConvertAudio, guest::addr(&f->cvt));
    SoundRef *r = sound_create(&guest::at<uint8_t>(f->cvt.buf), static_cast<uint32_t>(f->cvt.len_cvt));
    crt_free(f->cvt.buf);
    return r;
}

// Soft clipping of a sum of channels into 16 bits.
static int soft_clip(int v) {
    int r = v < 0 ? 0x40000000 / (0x8000 - v) - 0x8000 : 0x8000 - 0x40000000 / (v + 0x8000);
    if (r > 0x7fff) return 0x7fff;
    if (r < -0x8000) return -0x8000;
    return r;
}

// The SDL audio callback: renders each playing channel into the next mix buffer (the first playing
// channel into buffer 0, and so on), then mixes those buffers into the stream; silence if none plays.
void audio_mix(uint32_t userdata, int16_t *stream, uint32_t len) {
    uint32_t values = len >> 1;  // 16-bit values
    int used = 0;
    for (int i = 0; i < CHANNEL_COUNT; i++) {
        Channel &ch = g_channels[i];
        if (!ch.sound) continue;
        SoundRef *s = ch.sound.get();
        Sample *sample = s->sample.get();
        const int16_t *data = sample->data.get();
        int16_t *out = g_channel_buf[used++].get(), *end = out + values;
        while (out < end) {
            out[0] = static_cast<int16_t>(data[guest::to_int(ch.pos) * 2] * s->volume >> 7);
            out[1] = static_cast<int16_t>(data[guest::to_int(ch.pos) * 2 + 1] * s->volume >> 7);
            out += 2;
            float p = s->rate + ch.pos;
            ch.pos = p;
            float length = static_cast<float>(static_cast<double>(sample->length));
            if (!(p * 2.0f >= length)) continue;
            if (ch.loop) {
                ch.pos = p - length * 0.5f;
                continue;
            }
            // end of the sample: silence for the rest of the buffer, free the channel
            ch.pos = 0;
            uint32_t rest = out > end ? 0 : static_cast<uint32_t>(reinterpret_cast<uint8_t *>(end) -
                                                                  reinterpret_cast<uint8_t *>(out) + 1) >> 1;
            for (uint32_t k = 0; k < rest; k++) out[k] = 0;
            ch.sound = nullptr;
            break;
        }
    }
    if (used == 0) {
        guest::call_cdecl(0x497d10, guest::addr(stream), 0u, len);  // memset
        return;
    }
    for (uint32_t k = 0; k < values; k += 2) {
        int left = 0, right = 0;
        for (int i = 0; i < used; i++) {
            left += g_channel_buf[i].get()[k];
            right += g_channel_buf[i].get()[k + 1];
        }
        int l = soft_clip(left);
        stream[k + 1] = static_cast<int16_t>(soft_clip(right));
        stream[k] = static_cast<int16_t>(l);
    }
}
