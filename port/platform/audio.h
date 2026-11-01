/*
 * audio.h - the port's sound output (audio.c): sources mixed on the platform's clock.
 *
 * A source (the CD's music, the synthesizer, the digital samples) renders its own format at
 * its own rate, and an SDL audio stream converts that to the mix's. What the game tells a
 * source (a track played, a register written) is an event stamped with the moment it happened
 * on the platform's clock; inside the timer interrupt that is the moment the tick was due, so
 * the bursts in which the platform runs late ticks do not jitter. A source carries out each
 * event between the frames of its moment, AUDIO_LATENCY_NS later.
 *
 * The mix goes to the sound device, rendered on SDL's audio thread as the device asks for it;
 * or, on the virtual clock, rendered as the clock moves into the WAV file BAK_DUMP_AUDIO names,
 * so a scripted run sounds the same every time. With neither, events are carried out as they
 * come and nothing is rendered. Nothing the game reads depends on the rendering.
 */
#ifndef BAK_AUDIO_H
#define BAK_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#define AUDIO_RATE 48000             /* the mix: frames a second, two channels of floats */
#define AUDIO_LATENCY_NS 50000000ull /* from an event's moment to its sound */
#define AUDIO_MAX_SOURCES 8

/* What a source is told, at a moment of the platform's clock. The fields mean what the source
 * makes of them; p passes it something made on the game's side (it then owns it). */
typedef struct AudioEvent {
    uint64_t ns;
    uint32_t a, b;
    void *p;
} AudioEvent;

/* Which setting scales a source, besides audio.volume. */
typedef enum AudioVolume {
    AUDIO_VOLUME_MUSIC,
    AUDIO_VOLUME_EFFECTS
} AudioVolume;

typedef struct AudioSourceDesc {
    const char *name;
    int format; /* an SDL_AudioFormat: what render writes */
    int channels;
    int rate;
    AudioVolume volume;
    float gain; /* the source's level in the mix, before the settings' volumes */
    /* Write frames frames of sound into out (on the audio thread, or the game's when no
     * device plays). */
    void (*render)(void *user, void *out, int frames);
    /* Carry out an event (on the same thread as render, between frames). */
    void (*apply)(void *user, const AudioEvent *e);
    void *user;
} AudioSourceDesc;

typedef struct AudioSource AudioSource;

/* Start the sound: the device on the host's clock, the BAK_DUMP_AUDIO file on the virtual one.
 * Sources are added before. Returns whether anything renders. */
bool audio_open(void);
/* Render into dumpPath's WAV (or only render, for NULL) as audio_advance moves, frame 0 being
 * the clock's epochNs: the virtual clock's way, and the unit tests'. */
bool audio_open_dump(const char *dumpPath, uint64_t epochNs);
/* Stop the device, finish the WAV file, drop the sources. */
void audio_close(void);

AudioSource *audio_source_add(const AudioSourceDesc *desc);
/* An event for the source at the present moment: in the timer interrupt, its tick's. */
void audio_post(AudioSource *s, uint32_t a, uint32_t b, void *p);
void audio_post_at(AudioSource *s, uint64_t ns, uint32_t a, uint32_t b, void *p);

/* A line for BAK_AUDIO_LOG's file, which replays compare: something a source was told, at the
 * present moment (ms). When the WAV file is finished, its length and hash end the log. */
void audio_log(const char *fmt, ...);

/* Render what is due up to nowNs when rendering follows the clock (audio_open_dump); called
 * as the platform polls. */
void audio_advance(uint64_t nowNs);
/* Mix frames frames of every source into out (two floats a frame): what the device's thread
 * and audio_advance do; the unit tests call it directly. */
void audio_mix(float *out, int frames);
/* The device's frames drift from the platform's clock: at a pull whose mix time is driftNs
 * ahead of the clock, refNs having been the first pull's, how far the epoch moves (0 while
 * they keep within 20 ms of the first pull's). */
int64_t audio_epoch_correction(int64_t driftNs, int64_t refNs);

#endif
