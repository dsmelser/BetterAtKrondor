/*
 * The port's sound output: sources mixed on the platform's clock (see audio.h).
 *
 * Each source has an SDL audio stream from its own format to the mix's. Pulling mixed frames
 * pulls each source's stream, whose callback renders the source's frames, carrying out the
 * events whose moment has come between them. Frame f of a source falls at epoch + f / rate on
 * the platform's clock, so an event stamped t is carried out before the source's frame
 * (t + AUDIO_LATENCY_NS - epoch) * rate. The mix is pulled by the device's thread, or by the
 * platform's polls as the clock moves (audio_advance), or by nobody.
 *
 * The device's frames and the platform's clock drift apart slowly (their crystals differ):
 * each pull compares the mix's time with the clock and, once they are more than DRIFT_NS
 * further apart than at the first pull, moves the epoch back into step.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "audio.h"
#include "platform.h"

#define MIX_CHUNK 1024      /* frames mixed at once */
#define RENDER_CHUNK 1024   /* a source's frames rendered at once */
#define ADVANCE_FRAMES 256  /* audio_advance waits until this many are due */
#define DRIFT_NS 20000000ll /* the device's drift tolerated before the epoch moves */

typedef enum AudioMode {
    MODE_NONE,   /* events are carried out as they come; nothing renders */
    MODE_DUMP,   /* audio_advance renders as the clock moves, into the WAV file if any */
    MODE_DEVICE, /* the device's thread renders */
} AudioMode;

struct AudioSource {
    AudioSourceDesc desc;
    SDL_AudioStream *stream;
    int frameBytes;
    uint64_t pos;       /* frames rendered since the epoch */
    AudioEvent *events; /* a ring of those not carried out yet, in order */
    int head, count, cap;
    unsigned char *buf;
};

static AudioMode s_mode;
static AudioSource s_sources[AUDIO_MAX_SOURCES];
static int s_sourceCount;
static SDL_Mutex *s_lock; /* the event rings */
static SDL_AudioStream *s_device;
static uint64_t s_epochNs; /* frame 0's moment on the platform's clock */
static uint64_t s_mixFrames;
static bool s_driftKnown;
static int64_t s_driftRefNs;
static float s_volume, s_music, s_effects;

static SDL_IOStream *s_wav;
static uint32_t s_wavBytes;
static uint64_t s_wavHash; /* FNV-1a over the frames written, whatever the writes' sizes */
static uint32_t s_wavDone; /* the last WAV file's, folded */
static SDL_IOStream *s_log;
static bool s_logOpened;

/* ---- the WAV file: 16-bit stereo at the mix's rate --------------------------------------- */

static void put32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void wav_header(unsigned char h[44], uint32_t dataBytes) {
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + dataBytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put32(h + 20, 1 | 2u << 16); /* PCM, two channels */
    put32(h + 24, AUDIO_RATE);
    put32(h + 28, AUDIO_RATE * 4); /* bytes a second */
    put32(h + 32, 4 | 16u << 16);  /* bytes a frame, bits a sample */
    memcpy(h + 36, "data", 4);
    put32(h + 40, dataBytes);
}

static bool wav_open(const char *path) {
    unsigned char h[44];
    s_wav = SDL_IOFromFile(path, "wb");
    if (s_wav == NULL) {
        fprintf(stderr, "BAK_DUMP_AUDIO: cannot write %s\n", path);
        return false;
    }
    s_wavBytes = 0;
    s_wavHash = 0xcbf29ce484222325ull;
    wav_header(h, 0);
    SDL_WriteIO(s_wav, h, sizeof h);
    return true;
}

static void wav_write(const float *mix, int frames) {
    int16_t pcm[MIX_CHUNK * 2];
    for (int i = 0; i < frames * 2; i++) {
        float v = mix[i] * 32768.0f;
        v = v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v;
        pcm[i] = (int16_t)(v < 0.0f ? v - 0.5f : v + 0.5f);
    }
    SDL_WriteIO(s_wav, pcm, (size_t)frames * 4);
    for (int i = 0; i < frames; i++)
        s_wavHash = (s_wavHash ^ ((uint32_t)(uint16_t)pcm[2 * i] |
                                  (uint32_t)(uint16_t)pcm[2 * i + 1] << 16)) *
                    0x100000001b3ull;
    s_wavBytes += (uint32_t)frames * 4;
}

static void wav_close(void) {
    unsigned char h[44];
    wav_header(h, s_wavBytes);
    SDL_SeekIO(s_wav, 0, SDL_IO_SEEK_SET);
    SDL_WriteIO(s_wav, h, sizeof h);
    SDL_CloseIO(s_wav);
    s_wav = NULL;
    s_wavDone = (uint32_t)(s_wavHash ^ s_wavHash >> 32);
    audio_log("wav: %u frames, hash %08x", (unsigned)(s_wavBytes / 4), (unsigned)s_wavDone);
}

uint32_t audio_wav_hash(void) {
    return s_wavDone;
}

/* ---- the log ------------------------------------------------------------------------------ */

void audio_log(const char *fmt, ...) {
    char line[256];
    va_list ap;
    if (!s_logOpened) {
        const char *path = SDL_getenv("BAK_AUDIO_LOG");
        s_logOpened = true;
        if (path != NULL && (s_log = SDL_IOFromFile(path, "w")) == NULL)
            fprintf(stderr, "BAK_AUDIO_LOG: cannot write %s\n", path);
    }
    if (s_log == NULL)
        return;
    va_start(ap, fmt);
    SDL_vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    uint64_t ns = machine_event_ns();
    SDL_IOprintf(s_log, "%llu.%03llu %s\n", (unsigned long long)(ns / 1000000),
                 (unsigned long long)(ns / 1000 % 1000), line);
}

/* ---- sources --------------------------------------------------------------------------- */

static uint64_t frame_due(const AudioSource *s, uint64_t ns) {
    uint64_t at = ns + AUDIO_LATENCY_NS;
    return at > s_epochNs ? (at - s_epochNs) * (uint64_t)s->desc.rate / 1000000000ull : 0;
}

/* The stream wants more of the source's frames: render them, the events between them. */
static void SDLCALL source_get(void *user, SDL_AudioStream *stream, int additional, int total) {
    AudioSource *s = user;
    int frames = additional / s->frameBytes;
    (void)total;
    while (frames > 0) {
        uint64_t next = UINT64_MAX;
        SDL_LockMutex(s_lock);
        while (s->count > 0) {
            AudioEvent e = s->events[s->head];
            uint64_t at = frame_due(s, e.ns);
            if (at > s->pos) {
                next = at;
                break;
            }
            s->head = (s->head + 1) % s->cap;
            s->count--;
            SDL_UnlockMutex(s_lock);
            s->desc.apply(s->desc.user, &e);
            SDL_LockMutex(s_lock);
        }
        SDL_UnlockMutex(s_lock);
        int n = frames < RENDER_CHUNK ? frames : RENDER_CHUNK;
        if (next - s->pos < (uint64_t)n)
            n = (int)(next - s->pos);
        s->desc.render(s->desc.user, s->buf, n);
        SDL_PutAudioStreamData(stream, s->buf, n * s->frameBytes);
        s->pos += (uint64_t)n;
        frames -= n;
    }
}

AudioSource *audio_source_add(const AudioSourceDesc *desc) {
    if (s_sourceCount == AUDIO_MAX_SOURCES)
        return NULL;
    if (s_lock == NULL)
        s_lock = SDL_CreateMutex();
    AudioSource *s = &s_sources[s_sourceCount];
    SDL_AudioSpec src = {(SDL_AudioFormat)desc->format, desc->channels, desc->rate};
    SDL_AudioSpec dst = {SDL_AUDIO_F32, 2, AUDIO_RATE};
    memset(s, 0, sizeof *s);
    s->desc = *desc;
    s->frameBytes = SDL_AUDIO_FRAMESIZE(src);
    s->stream = SDL_CreateAudioStream(&src, &dst);
    s->buf = SDL_malloc((size_t)RENDER_CHUNK * (size_t)s->frameBytes);
    s->cap = 256;
    s->events = SDL_malloc((size_t)s->cap * sizeof *s->events);
    if (s->stream == NULL || s->buf == NULL || s->events == NULL) {
        fprintf(stderr, "audio: no source \"%s\": %s\n", desc->name, SDL_GetError());
        SDL_DestroyAudioStream(s->stream);
        SDL_free(s->buf);
        SDL_free(s->events);
        return NULL;
    }
    SDL_SetAudioStreamGetCallback(s->stream, source_get, s);
    /* The device's thread mixes while it holds its stream's lock. A source added late starts
     * at the mix's present moment. */
    if (s_device != NULL)
        SDL_LockAudioStream(s_device);
    s->pos = s_mixFrames * (uint64_t)desc->rate / AUDIO_RATE;
    s_sourceCount++;
    if (s_device != NULL)
        SDL_UnlockAudioStream(s_device);
    return s;
}

void audio_post_at(AudioSource *s, uint64_t ns, uint32_t a, uint32_t b, void *p) {
    AudioEvent e = {ns, a, b, p};
    if (s == NULL)
        return;
    if (s_mode == MODE_NONE) {
        s->desc.apply(s->desc.user, &e);
        return;
    }
    SDL_LockMutex(s_lock);
    if (s->count == s->cap) {
        AudioEvent *grown = SDL_malloc((size_t)s->cap * 2 * sizeof *grown);
        if (grown == NULL) {
            SDL_UnlockMutex(s_lock);
            return; /* the event is lost, the game goes on */
        }
        for (int i = 0; i < s->count; i++)
            grown[i] = s->events[(s->head + i) % s->cap];
        SDL_free(s->events);
        s->events = grown;
        s->head = 0;
        s->cap *= 2;
    }
    s->events[(s->head + s->count) % s->cap] = e;
    s->count++;
    SDL_UnlockMutex(s_lock);
}

void audio_post(AudioSource *s, uint32_t a, uint32_t b, void *p) {
    audio_post_at(s, machine_event_ns(), a, b, p);
}

/* ---- the mix ----------------------------------------------------------------------------- */

void audio_mix(float *out, int frames) {
    static float in[MIX_CHUNK * 2];
    while (frames > 0) {
        int n = frames < MIX_CHUNK ? frames : MIX_CHUNK;
        memset(out, 0, (size_t)n * 2 * sizeof *out);
        for (int i = 0; i < s_sourceCount; i++) {
            AudioSource *s = &s_sources[i];
            float gain = s_volume * s->desc.gain *
                         (s->desc.volume == AUDIO_VOLUME_MUSIC ? s_music : s_effects);
            int got = SDL_GetAudioStreamData(s->stream, in, n * 2 * (int)sizeof *in);
            for (int k = 0; k < (got > 0 ? got / (int)sizeof *in : 0); k++)
                out[k] += in[k] * gain;
        }
        s_mixFrames += (uint64_t)n;
        out += n * 2;
        frames -= n;
    }
}

static int64_t mix_time_ns(void) {
    return (int64_t)(s_epochNs + s_mixFrames * 1000000000ull / AUDIO_RATE);
}

int64_t audio_epoch_correction(int64_t driftNs, int64_t refNs) {
    int64_t off = driftNs - refNs;
    return off > DRIFT_NS || off < -DRIFT_NS ? -off : 0;
}

/* The device wants more: mix it, keeping the epoch in step with the platform's clock. */
static void SDLCALL device_get(void *user, SDL_AudioStream *stream, int additional, int total) {
    float mix[MIX_CHUNK * 2];
    (void)user;
    (void)total;
    int64_t drift = mix_time_ns() - (int64_t)clock_now_ns();
    if (!s_driftKnown) {
        s_driftKnown = true;
        s_driftRefNs = drift;
    }
    s_epochNs = (uint64_t)((int64_t)s_epochNs + audio_epoch_correction(drift, s_driftRefNs));
    for (int frames = additional / 8; frames > 0;) {
        int n = frames < MIX_CHUNK ? frames : MIX_CHUNK;
        audio_mix(mix, n);
        if (s_wav != NULL)
            wav_write(mix, n);
        SDL_PutAudioStreamData(stream, mix, n * 8);
        frames -= n;
    }
}

void audio_advance(uint64_t nowNs) {
    float mix[MIX_CHUNK * 2];
    if (s_mode != MODE_DUMP || nowNs <= s_epochNs)
        return;
    uint64_t due = (nowNs - s_epochNs) * AUDIO_RATE / 1000000000ull;
    if (due < s_mixFrames + ADVANCE_FRAMES)
        return;
    while (s_mixFrames < due) {
        int n = due - s_mixFrames < MIX_CHUNK ? (int)(due - s_mixFrames) : MIX_CHUNK;
        audio_mix(mix, n);
        if (s_wav != NULL)
            wav_write(mix, n);
    }
}

/* ---- the sound card the game drives -------------------------------------------------------- */

int16_t bak_sound_driver(int16_t configured) {
    /* AUDRESIN.H's SoundDriverId: ADL 2, M32 3, SBP 4 (ADL with the digital ASB), NONE 8. */
    static const int16_t ids[] = {[DEVICE_SB] = 4, [DEVICE_ADLIB] = 2, [DEVICE_MT32] = 3,
                                  [DEVICE_NONE] = 8};
    int device = settings_get_choice(SETTING_AUDIO_DEVICE);
    return device == DEVICE_GAME ? configured : ids[device];
}

/* ---- opening and closing ---------------------------------------------------------------- */

static void read_volumes(void) {
    int v;
    s_volume = settings_get_int(SETTING_AUDIO_VOLUME, &v) ? (float)v / 100.0f : 1.0f;
    s_music = settings_get_int(SETTING_AUDIO_MUSIC_VOLUME, &v) ? (float)v / 100.0f : 1.0f;
    s_effects = settings_get_int(SETTING_AUDIO_EFFECTS_VOLUME, &v) ? (float)v / 100.0f : 1.0f;
}

bool audio_open_dump(const char *dumpPath, uint64_t epochNs) {
    read_volumes();
    s_epochNs = epochNs;
    s_mixFrames = 0;
    for (int i = 0; i < s_sourceCount; i++)
        s_sources[i].pos = 0;
    if (dumpPath != NULL && !wav_open(dumpPath))
        return false;
    s_mode = MODE_DUMP;
    return true;
}

bool audio_open(void) {
    const char *dump = SDL_getenv("BAK_DUMP_AUDIO");
    if (clock_is_virtual())
        return dump != NULL && audio_open_dump(dump, clock_now_ns());
    read_volumes();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "no sound: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, AUDIO_RATE};
    s_epochNs = clock_now_ns();
    s_mixFrames = 0;
    s_driftKnown = false;
    s_mode = MODE_DEVICE; /* before the device's thread can pull */
    s_device =
        SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, device_get, NULL);
    if (s_device == NULL) {
        s_mode = MODE_NONE;
        fprintf(stderr, "no sound device: %s\n", SDL_GetError());
        return false;
    }
    if (dump != NULL)
        wav_open(dump);
    if (SDL_getenv("BAK_TRACE") != NULL)
        fprintf(stderr, "trace: sound on \"%s\"\n",
                SDL_GetAudioDeviceName(SDL_GetAudioStreamDevice(s_device)));
    SDL_ResumeAudioStreamDevice(s_device);
    return true;
}

void audio_close(void) {
    if (s_device != NULL) {
        SDL_DestroyAudioStream(s_device); /* stops the device's thread first */
        s_device = NULL;
    }
    if (s_wav != NULL)
        wav_close();
    if (s_log != NULL) {
        SDL_CloseIO(s_log);
        s_log = NULL;
    }
    for (int i = 0; i < s_sourceCount; i++) {
        SDL_DestroyAudioStream(s_sources[i].stream);
        SDL_free(s_sources[i].buf);
        SDL_free(s_sources[i].events);
    }
    s_sourceCount = 0;
    s_mode = MODE_NONE;
}
