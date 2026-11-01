/* The sound's mix: events land on the frame of their moment, the timer's on their tick's, the
 * settings' volumes scale the sources, and the dump is a WAV file of the mix. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/audio.h"
#include "../../platform/platform.h"
#include "bak_port.h"

#define T0 1000000000ull
#define MS 1000000ull
#define PIT_HZ 1193182ull

/* A source that plays a constant level, which its events set (the event's a), and notes the
 * frame each event lands on. */
typedef struct TestSource {
    int16_t level;
    uint64_t rendered;
    uint64_t appliedAt[16];
    int applied;
} TestSource;

static void test_render(void *user, void *out, int frames) {
    TestSource *t = user;
    int16_t *o = out;
    for (int i = 0; i < frames; i++)
        o[2 * i] = o[2 * i + 1] = t->level;
    t->rendered += (uint64_t)frames;
}

static void test_apply(void *user, const AudioEvent *e) {
    TestSource *t = user;
    t->level = (int16_t)e->a;
    if (t->applied < 16)
        t->appliedAt[t->applied++] = t->rendered;
}

static AudioSource *add_test_source(TestSource *t, int rate, AudioVolume volume) {
    AudioSourceDesc d = {"test", SDL_AUDIO_S16, 2, rate, volume, 1.0f, test_render, test_apply, t};
    memset(t, 0, sizeof *t);
    return audio_source_add(&d);
}

/* The frame of a 48 kHz source an event of moment ns lands on, the epoch being T0. */
static uint64_t frame_of(uint64_t ns) {
    return (ns + AUDIO_LATENCY_NS - T0) * AUDIO_RATE / 1000000000ull;
}

void unit_audio_events_land_on_their_frame(void) {
    static float mix[4800 * 2];
    TestSource t;
    clock_use_virtual(T0);
    AudioSource *s = add_test_source(&t, AUDIO_RATE, AUDIO_VOLUME_EFFECTS);
    UNIT_CHECK(s != NULL);
    audio_open_dump(NULL, T0);
    audio_post_at(s, T0 + 10 * MS, 1000, 0, NULL);
    audio_post_at(s, T0 + 10 * MS + 21 * 1000, 2000, 0, NULL); /* a frame later */
    audio_mix(mix, 4800);
    UNIT_CHECK(t.applied == 2);
    UNIT_CHECK(t.appliedAt[0] == frame_of(T0 + 10 * MS));
    UNIT_CHECK(t.appliedAt[0] == 2880); /* 60 ms in */
    UNIT_CHECK(t.appliedAt[1] == 2881);
    UNIT_CHECK(mix[2 * 2879] == 0.0f && mix[2 * 2879 + 1] == 0.0f);
    UNIT_CHECK(mix[2 * 2880] == 1000.0f / 32768.0f && mix[2 * 2880 + 1] == 1000.0f / 32768.0f);
    UNIT_CHECK(mix[2 * 2881] == 2000.0f / 32768.0f && mix[2 * 4799] == 2000.0f / 32768.0f);
    /* An event whose moment has passed by the time it comes lands at once. */
    audio_post_at(s, T0, 3000, 0, NULL);
    audio_mix(mix, 10);
    UNIT_CHECK(t.applied == 3 && t.appliedAt[2] == 4800);
    UNIT_CHECK(mix[0] == 3000.0f / 32768.0f);
    audio_close();
}

static AudioSource *s_tickSource;
static uint32_t s_tickCount;

static void tick_posts(void) {
    audio_post(s_tickSource, ++s_tickCount * 100, 0, NULL);
}

/* The platform runs late timer ticks in a burst; what they post keeps each tick's moment. */
void unit_audio_timer_events_take_their_ticks_time(void) {
    static float mix[9600 * 2];
    TestSource t;
    BakIsr *saved = bak_get_vector(0x08);
    clock_use_virtual(T0);
    s_tickSource = add_test_source(&t, AUDIO_RATE, AUDIO_VOLUME_EFFECTS);
    audio_open_dump(NULL, T0);
    bak_set_vector(0x08, tick_posts);
    machine_port_out8(0x43, 0x36); /* channel 0: the game's 236.7 Hz */
    machine_port_out8(0x40, 5041 & 0xff);
    machine_port_out8(0x40, 5041 >> 8);
    uint64_t start = clock_now_ns();
    uint64_t tick = 5041ull * 1000000000ull / PIT_HZ;
    s_tickCount = 0;
    clock_spend(5 * tick + 1000); /* five ticks late */
    bak_poll();
    UNIT_CHECK(s_tickCount == 5);
    UNIT_CHECK(machine_event_ns() == clock_now_ns()); /* outside the interrupt: the present */
    audio_mix(mix, 9600);
    UNIT_CHECK(t.applied == 5);
    for (int k = 0; k < 5 && k < t.applied; k++)
        UNIT_CHECK(t.appliedAt[k] == frame_of(start + (uint64_t)(k + 1) * tick));
    bak_set_vector(0x08, saved);
    audio_close();
}

void unit_audio_volumes_scale_the_mix(void) {
    static float mix[16 * 2];
    TestSource music, effects;
    char why[200];
    clock_use_virtual(T0);
    UNIT_CHECK(settings_override("audio.volume=50", why, sizeof why));
    UNIT_CHECK(settings_override("audio.effects_volume=50", why, sizeof why));
    AudioSource *m = add_test_source(&music, AUDIO_RATE, AUDIO_VOLUME_MUSIC);
    AudioSource *e = add_test_source(&effects, AUDIO_RATE, AUDIO_VOLUME_EFFECTS);
    audio_open_dump(NULL, T0);
    audio_post_at(m, 0, 16384, 0, NULL); /* long past: at once */
    audio_mix(mix, 16);
    UNIT_CHECK(mix[30] == 0.25f); /* half of 16384: the volume */
    audio_post_at(m, 0, 0, 0, NULL);
    audio_post_at(e, 0, 16384, 0, NULL);
    audio_mix(mix, 16);
    UNIT_CHECK(mix[30] == 0.125f); /* and half of that: the effects' */
    audio_close();
    settings_override("audio.volume=100", why, sizeof why);
    settings_override("audio.effects_volume=100", why, sizeof why);
}

/* A source at another rate is converted: its event still lands at its moment, in its own
 * frames, and the mix follows within half a millisecond (the converter's delay). */
void unit_audio_sources_at_other_rates_keep_time(void) {
    static float mix[4800 * 2];
    TestSource t;
    clock_use_virtual(T0);
    AudioSource *s = add_test_source(&t, 22050, AUDIO_VOLUME_EFFECTS);
    audio_open_dump(NULL, T0);
    audio_post_at(s, T0 + 10 * MS, 8000, 0, NULL);
    audio_mix(mix, 4800);
    UNIT_CHECK(t.applied == 1 && t.appliedAt[0] == 1323); /* 60 ms of 22050 Hz */
    int first = -1;
    for (int i = 0; i < 4800 && first < 0; i++)
        if (mix[2 * i] > 4000.0f / 32768.0f) /* half way up the converter's step */
            first = i;
    UNIT_CHECK(first >= 2880 && first <= 2880 + 24); /* SDL's resampler looks a few frames ahead */
    audio_close();
}

/* Event i of the queue test: level i + 1 on the frame i after 100 ms. */
static void post_frame(AudioSource *s, uint32_t i) {
    audio_post_at(s, T0 + 100 * MS + (i * 1000000000ull + AUDIO_RATE - 1) / AUDIO_RATE, i + 1, 0,
                  NULL);
}

/* With no sound open, events are carried out as they come; once it is, a source added after
 * the mix began starts at its present moment, and its events, however many, keep their order. */
void unit_audio_sources_join_and_queue(void) {
    static float mix[9600 * 2];
    TestSource early, late;
    clock_use_virtual(T0);
    AudioSource *e = add_test_source(&early, AUDIO_RATE, AUDIO_VOLUME_EFFECTS);
    audio_post_at(e, T0 + 500 * MS, 777, 0, NULL);
    UNIT_CHECK(early.applied == 1 && early.level == 777 && early.rendered == 0);
    audio_open_dump(NULL, T0);
    audio_mix(mix, 4800); /* 100 ms: frames 0-4799 */
    AudioSource *l = add_test_source(&late, AUDIO_RATE, AUDIO_VOLUME_EFFECTS);
    for (uint32_t i = 0; i < 200; i++)
        post_frame(l, i); /* frame 7200 + i: 100 ms on, and the latency */
    audio_mix(mix, 2500); /* up to frame 7300: the first 100 are carried out, the ring turns */
    UNIT_CHECK(late.level == 100);
    UNIT_CHECK(late.appliedAt[0] == frame_of(T0 + 100 * MS) - 4800); /* in its own frames */
    UNIT_CHECK(late.appliedAt[15] == late.appliedAt[0] + 15);
    for (uint32_t i = 200; i < 400; i++)
        post_frame(l, i); /* more than the ring holds: it grows */
    audio_mix(mix, 9600); /* from frame 7300 */
    UNIT_CHECK(late.level == 400);
    bool inOrder = true;
    for (int i = 100; i < 400; i++)
        inOrder &= mix[2 * (i - 100)] == (777.0f + (float)(i + 1)) / 32768.0f;
    UNIT_CHECK(inOrder);
    audio_close();
}

/* The device's frames and the clock drift apart: the epoch follows once they are 20 ms
 * further apart than at the first pull, back to where they were then. */
void unit_audio_epoch_follows_the_device_drift(void) {
    UNIT_CHECK(audio_epoch_correction(-3 * MS, -3 * MS) == 0);
    UNIT_CHECK(audio_epoch_correction(16 * MS, -3 * MS) == 0);
    UNIT_CHECK(audio_epoch_correction(17 * MS, -3 * MS) == 0); /* 20 ms exactly: tolerated */
    UNIT_CHECK(audio_epoch_correction(17 * MS + 1, -3 * MS) == -(int64_t)(20 * MS + 1));
    UNIT_CHECK(audio_epoch_correction(-23 * MS, -3 * MS) == 0);
    UNIT_CHECK(audio_epoch_correction(-23 * MS - 1, -3 * MS) == (int64_t)(20 * MS + 1));
}

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* On the virtual clock the mix is rendered as the clock moves, into a WAV file. */
void unit_audio_dump_follows_the_clock(void) {
    TestSource t;
    unsigned char h[48];
    const char *dir = unit_temp_dir("audio-dump");
    char path[512];
    SDL_snprintf(path, sizeof path, "%s/mix.wav", dir);
    char why[200];
    clock_use_virtual(T0);
    settings_override("audio.volume=50", why, sizeof why); /* 617.5 in the WAV's scale */
    AudioSource *s = add_test_source(&t, AUDIO_RATE, AUDIO_VOLUME_EFFECTS);
    UNIT_CHECK(audio_open_dump(path, T0));
    settings_override("audio.volume=100", why, sizeof why);
    audio_post_at(s, 0, 1235, 0, NULL);
    audio_advance(T0 + 2 * MS); /* fewer frames than worth rendering yet */
    UNIT_CHECK(t.rendered == 0);
    audio_advance(T0 + 100 * MS);
    UNIT_CHECK(t.rendered == 4800);
    audio_close();
    size_t size = 0;
    unsigned char *wav = SDL_LoadFile(path, &size);
    UNIT_CHECK(wav != NULL);
    if (wav != NULL) {
        UNIT_CHECK(size == 44 + 4800 * 4);
        memcpy(h, wav, sizeof h);
        SDL_free(wav);
        UNIT_CHECK(memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVEfmt ", 8) == 0);
        UNIT_CHECK(rd32(h + 4) == 36 + 4800 * 4 && rd32(h + 40) == 4800 * 4);
        UNIT_CHECK(rd32(h + 24) == AUDIO_RATE && (h[22] | h[23] << 8) == 2);
        UNIT_CHECK((int16_t)(h[44] | h[45] << 8) == 618); /* the first frame's left, rounded */
    }
    /* The same sound rendered as the clock moves in other steps: the same hash. */
    uint32_t hash = audio_wav_hash();
    clock_use_virtual(T0);
    settings_override("audio.volume=50", why, sizeof why);
    s = add_test_source(&t, AUDIO_RATE, AUDIO_VOLUME_EFFECTS);
    UNIT_CHECK(audio_open_dump(path, T0));
    settings_override("audio.volume=100", why, sizeof why);
    audio_post_at(s, 0, 1235, 0, NULL);
    audio_advance(T0 + 7 * MS);
    audio_advance(T0 + 61 * MS);
    audio_advance(T0 + 100 * MS);
    UNIT_CHECK(t.rendered == 4800);
    audio_close();
    UNIT_CHECK(audio_wav_hash() == hash);
    unit_remove_tree(dir);
}
