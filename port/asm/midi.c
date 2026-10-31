/*
 * MIDI.ASM in C: the sequencer behind the game's music and sound effects.
 *
 * A track (an AudioTrackHandle record, 17Ah bytes) plays a script of up to 16 streams of
 * MIDI-like events, each on a MIDI channel. The playing tracks sit in a registry of 16,
 * highest priority first. The timer runs midi_task_scheduler_tick every fourth tick
 * (59.2 Hz): fades step, every stream of every track advances one tick, and its events
 * become calls into the synth driver (dude.h). The tracks together may want more voices
 * than the synthesizer has, so an arbiter (recompute_voices) gives each channel of each
 * track a voice by priority and cost, and re-keys the voices whose owner changed.
 *
 * The driver is the SX.OVL chunk the game loaded; natively the port's C code for it is
 * called with the chunk and the registers the original passes (DudeRegs). Registers a
 * driver does not read are passed as 0.
 *
 * Record fields are reached by byte offset, as in the original: the per-channel arrays
 * hold 15 entries, and channel 15 reaches into the next array. Interrupts cannot arrive
 * inside these routines natively (the timer runs only where the game polls), so the
 * original's CLI/POPF brackets have no counterpart.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../drivers/dude.h"
#include "midiseq.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/AUDIO/DRIVER/MIDI.H"
#include "SRC/AUDIO/DRIVER/SNDDRV.H"
#include "SRC/AUDIO/RES/AUDRESIN.H"
#include "bak_game_end.h"

typedef uint8_t u8;

/* Track record fields. Per stream (0-15): */
#define T_SCRIPT 0x008      /* far pointer to the far pointer to the script */
#define T_POS 0x00c         /* u16: next byte, from the stream's start; 0: the stream ended */
#define T_LOOP_POS 0x02c    /* u16: T_POS at the loop point */
#define T_WAIT 0x04c        /* u16: ticks to the next event; 8000h up: a long wait */
#define T_LOOP_WAIT 0x06c   /* u16 */
#define T_CHANNEL 0x08c     /* u8: the stream's MIDI channel; FFh ends the list, FEh skipped */
#define T_STATUS 0x09c      /* u8: running status */
#define T_LOOP_STATUS 0x0ac /* u8 */
/* Per MIDI channel: */
#define T_BEND 0x0bc       /* u16: pitch bend, 14 bits; bit 15: sustain pedal down */
#define T_VOICE 0x0da      /* u8: low nibble the cost of a voice, high nibble its priority */
#define T_MODULATION 0x0e9 /* u8: controller 1 */
#define T_PAN 0x0f8        /* u8: controller 10 */
#define T_VOLUME 0x107     /* u8: controller 7 */
#define T_PROGRAM 0x116    /* u8 */
#define T_KEY 0x125        /* u8: the note on; FFh none */
#define T_FLAGS 0x134      /* u8: 1 the voice numbered as the channel; 2 a fixed voice */
#define T_MUTE 0x143       /* u8: nonzero: no voice; bit 0 controller 4Eh, high nibble a count */
/* Per track: */
#define T_MARKS 0x152      /* u16: controller 60h events played */
#define T_CLOCK 0x154      /* u16: ticks played */
#define T_LOOP_CLOCK 0x156 /* u16 */
#define T_STATE 0x158      /* u8: FFh stopped, FEh a fade ended, else the last C meta event */
#define T_KIND 0x159       /* u8: 1 sound effect, 2 music; 0 stopped */
#define T_CUE 0x15a        /* u8: compared by controller 52h; nonzero: loop at the end */
#define T_KEEP_PRIORITY 0x15b
#define T_PRIORITY 0x15c
#define T_LOOP 0x15d        /* u8: nonzero: loop at the end */
#define T_MASTER 0x15e      /* u8: master volume */
#define T_PARAM 0x15f       /* u8: for the driver's slot 11; 7Fh: the global value */
#define T_FADE_TARGET 0x160 /* u8: bit 7: stop the track when the fade ends */
#define T_FADE_PERIOD 0x161
#define T_FADE_COUNT 0x162
#define T_FADE_STEP 0x163 /* u8: 0: no fade */
#define T_HOLD 0x164      /* u8: paused while nonzero */
#define T_DIGITAL 0x165   /* u8: 1 + the stream for the digital driver; bit 7: started */

#define NOT_FOUND 0xff

MidiData g_midiData = {
    .voiceOwner = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                   0xff, 0xff, 0xff},
    .voiceStage = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                   0xff, 0xff, 0xff},
    .snapStage = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                  0xff, 0xff, 0xff},
    .voiceChannel = {0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
                     0x0f, 0x0f, 0x0f},
    .voiceRefresh = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                     0xff, 0xff, 0xff},
    .g = {.voiceHigh = 0x0f, .eventVoice = 0xff},
};

#define M g_midiData

static DudeEntry *s_driver; /* the code of the chunk M.drvvec points at */

static u16 rd16(const unsigned char *p) {
    return (u16)(p[0] | p[1] << 8);
}

static void wr16(unsigned char *p, u16 v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

#define W(t, off) rd16((t) + (off))
#define SETW(t, off, v) wr16((t) + (off), (u16)(v))

/* A far pointer passed as offset and segment words holds a host pointer's bits. */
static void *far_ptr(u16 off, u16 seg) {
    return (void *)(uintptr_t)((u32)seg << 16 | off);
}

/* The script: +08h points at the far pointer to it. */
static unsigned char *script_of(const unsigned char *t) {
    unsigned char *at, *script;
    memcpy(&at, t + T_SCRIPT, sizeof at);
    memcpy(&script, at, sizeof script);
    return script;
}

/* CALL DWORD PTR [_cs_drvvec] with BP = slot. */
static DudeRegs drv(u16 slot, u16 ax, u16 cx) {
    DudeRegs r = {ax, cx, NULL};
    if (s_driver == NULL)
        bak_unimplemented("MIDI.ASM: a driver call with no driver installed", __FILE__, __LINE__);
    s_driver(M.drvvec, slot, &r);
    return r;
}

static void inc_pos(unsigned char *t, unsigned stream) {
    SETW(t, T_POS + 2 * stream, W(t, T_POS + 2 * stream) + 1);
}

/* midi_lookup_track_state (2307h): the track's registry offset (slot * 4), or FFh. */
static unsigned registry_offset(const unsigned char *t) {
    for (unsigned i = 0; i < 16; i++)
        if (M.registry[i] == t)
            return i * 4;
    return NOT_FOUND;
}

/* midi_scale_velocity (22EFh): a channel volume through the master volume. The high
 * byte of the product is doubled: bit 7 of the low byte is dropped. */
static u8 scale_volume(u8 volume, u8 master) {
    u16 product = (u16)((u8)(volume + 1) * (u8)(master + 1));
    u8 v = (u8)((product >> 8) << 1);
    return v != 0 ? (u8)(v - 1) : 0;
}

/* The stored bend (14 bits, bit 15 the pedal) as slot 10 takes it: CH = LSB, CL = MSB. */
static u16 bend_bytes(u16 bend) {
    u8 lo = (u8)bend, hi = (u8)(bend >> 8);
    u8 msb = (u8)((hi << 1) | (lo >> 7));
    return (u16)((lo & 0x7f) << 8 | (msb & 0x7f));
}

/* ---- registry ----------------------------------------------------------------------- */

/* midi_track_release (0EA5h): out of the registry (the rest move up), marked stopped; a
 * started digital stream is told to stop. */
static void track_release(unsigned char *t) {
    unsigned i;
    for (i = 0; i < 16 && M.registry[i] != t; i++)
        ;
    if (i == 16)
        return;
    memmove(&M.registry[i], &M.registry[i + 1], (15 - i) * sizeof M.registry[0]);
    M.registry[15] = NULL;
    t[T_STATE] = 0xff;
    t[T_KIND] = 0;
    if (t[T_DIGITAL] >= 0x80)
        snddrv_dispatch_cached(5, NULL);
}

/* midi_track_parse_program_table (2329h): the script starts with a table per device:
 * [F0h, id, 6 bytes], then rows [device, entries of 6 bytes: stream, -, offset word,
 * 2 bytes; FFh], FFh. The row for the driver's device becomes the script's first 20h
 * bytes: the 16 stream offsets, then the table's id (the priority), and FC FD FE to
 * mark it done. */
static void parse_track_table(unsigned char *t) {
    unsigned char *at, *script, *p;
    unsigned n = 0;
    memcpy(&at, t + T_SCRIPT, sizeof at);
    if (at == (unsigned char *)(uintptr_t)0xffffffffu)
        return;
    script = script_of(t);
    if (script[0x23] == 0xfe && script[0x22] == 0xfd && script[0x21] == 0xfc)
        return;
    memset(M.programs, 0, sizeof M.programs);
    M.g.tableId = 0xff;
    p = script;
    if (p[0] == 0xf0) {
        M.g.tableId = p[1];
        p += 8;
    }
    for (;;) {
        u8 device = p[0];
        if (device == M.g.deviceId) {
            p++;
            while (*p++ != 0xff) {
                if (n == 16)
                    bak_unimplemented("MIDI.ASM: more than 16 streams in a track table "
                                      "(the original overwrites its voice tables)",
                                      __FILE__, __LINE__);
                p++;
                M.programs[n++] = rd16(p);
                p += 4;
            }
            break;
        }
        if (device == 0xff)
            break;
        p++;
        while (*p++ != 0xff)
            p += 5;
    }
    for (unsigned i = 0; i < 16; i++)
        wr16(script + 2 * i, M.programs[i]);
    script[0x20] = M.g.tableId;
    script[0x21] = 0xfc;
    script[0x22] = 0xfd;
    script[0x23] = 0xfe;
}

/* ---- the voice arbiter -------------------------------------------------------------- */

/* Recompute's re-key of voice v for channel ch of track t: notes off, cost, program,
 * volume, pan, modulation, sustain, bend and the note on. */
static void rekey(unsigned v, unsigned char *t, unsigned ch) {
    u16 ax = (u16)v;
    DudeRegs r;
    drv(7, ax, 0x7b00);
    r = drv(7, ax, (u16)(0x4b00 | (t[T_VOICE + ch] & 0x0f)));
    drv(8, ax, (u16)((r.cx & 0xff00) | t[T_PROGRAM + ch]));
    M.voiceRefresh[v] = 0xff;
    drv(7, ax, (u16)(0x0700 | scale_volume(t[T_VOLUME + ch], t[T_MASTER])));
    drv(7, ax, (u16)(0x0a00 | t[T_PAN + ch]));
    drv(7, ax, (u16)(0x0100 | t[T_MODULATION + ch]));
    drv(7, ax, t[T_BEND + 2 * ch + 1] >= 0x80 ? 0x407f : 0x4000);
    drv(10, ax, bend_bytes(W(t, T_BEND + 2 * ch)));
    drv(7, ax, (u16)(0x4e00 | t[T_KEY + ch]));
}

static void free_stage(unsigned v) {
    M.voiceStage[v] = 0xff;
    M.voiceCost[v] = 0;
    M.voiceAge[v] = 0;
    M.voiceFlags[v] = 0;
}

static void swap_bytes(u8 *a, unsigned i, unsigned j) {
    u8 x = a[i];
    a[i] = a[j];
    a[j] = x;
}

static void swap_stage(unsigned v, unsigned w) {
    swap_bytes(M.voiceStage, v, w);
    swap_bytes(M.voiceAge, v, w);
    swap_bytes(M.voiceCost, v, w);
    swap_bytes(M.voiceFlags, v, w);
}

/* Take the voice with the highest age (the first of equals) back, refunding its cost:
 * its number, or FFh when every age is 0. */
static u8 steal(u8 *budget) {
    u8 best = 0, v = 0xff;
    for (unsigned i = 0; i < 16; i++)
        if (best < M.voiceAge[i]) {
            best = M.voiceAge[i];
            v = (u8)i;
        }
    if (v != 0xff) {
        *budget = (u8)(*budget + M.voiceCost[v]);
        free_stage(v);
    }
    return v;
}

enum {
    NEXT_STREAM,
    NEXT_TRACK,
    UNDO_TRACK
};

/* One stream of a track asks for a voice (0F54h, the loop at 11D1h). */
static int give_voice(unsigned char *t, unsigned slot, unsigned stream, u8 bias, u8 *budget) {
    u8 cl = t[T_CHANNEL + stream];
    if (cl == 0xff || cl == 0xfe || cl == 0x0f)
        return NEXT_STREAM;
    if ((t[T_FLAGS + cl] & 2) || t[T_MUTE + cl] != 0)
        return NEXT_STREAM;
    u8 key = (u8)(slot << 4 | cl);
    u8 cost = t[T_VOICE + cl] & 0x0f;
    u8 age = t[T_VOICE + cl] >> 4;
    if (age != 0)
        age = (u8)(0x10 - age + bias);
    u8 v;
    if ((t[T_FLAGS + cl] & 1) && M.voiceStage[cl] == 0xff) {
        v = cl;
    } else {
        v = 0xff;
        for (unsigned i = 0; i < 16; i++) {
            if (M.voiceStage[i] == 0xff) {
                if (i >= M.g.voiceLow && i <= M.g.voiceHigh)
                    v = (u8)i; /* the last free one */
            } else if (M.voiceStage[i] == key) {
                return NEXT_STREAM;
            }
        }
        if (v == 0xff) {
            if (age != 0)
                return NEXT_TRACK; /* keeps what the track got so far */
            if ((v = steal(budget)) == 0xff)
                return UNDO_TRACK;
        }
    }
    if (cost > *budget) {
        if (age != 0)
            return NEXT_STREAM;
        do {
            if ((v = steal(budget)) == 0xff)
                return UNDO_TRACK;
        } while (cost > *budget); /* takes the voice stolen last */
    }
    M.voiceStage[v] = key;
    M.voiceCost[v] = cost;
    *budget = (u8)(*budget - cost);
    M.voiceAge[v] = age;
    if (!(t[T_FLAGS + cl] & 1)) {
        M.voiceFlags[v] = 0;
        return NEXT_STREAM;
    }
    M.voiceFlags[v] = 1;
    if (v == cl)
        return NEXT_STREAM;
    /* The channel's own voice is held by another. */
    if (M.voiceFlags[cl] == 0) {
        swap_stage(v, cl);
        return NEXT_STREAM;
    }
    if (age != 0) {
        free_stage(v);
        *budget = (u8)(*budget + cost);
        return NEXT_STREAM;
    }
    if (M.voiceAge[cl] == 0)
        return UNDO_TRACK;
    *budget = (u8)(*budget + M.voiceCost[cl]);
    free_stage(v);
    M.voiceStage[cl] = key;
    M.voiceAge[cl] = age;
    M.voiceCost[cl] = cost;
    *budget = (u8)(*budget - cost);
    return NEXT_STREAM;
}

/* midi_recompute_active_voices (0F54h). Every channel of every track that is not held
 * asks for a voice, the registry's order deciding, within the budget; a track that does
 * not fit is undone. Voices keep their owner where they can (only the others are
 * re-keyed), new owners take free voices from the top of the range down, and voices
 * left without an owner are silenced. */
static void recompute_voices(void) {
    unsigned char *t;
    unsigned v;
    M.g.lock++;
    M.g.dirty = 0;
    memset(M.voiceOwner, 0xff, 16);
    memset(M.voiceCost, 0, 16);
    memset(M.voiceFlags, 0, 16);
    memset(M.voiceAge, 0, 16);
    memset(M.voiceStage, 0xff, 16);
    M.streams[0] = NULL;
    t = M.registry[0];
    if (t != NULL) {
        u8 param = t[T_PARAM] == 0x7f ? M.g.globalParam : t[T_PARAM];
        drv(11, 0, param);
        u8 budget = M.g.budget;
        for (unsigned slot = 0; slot < 16; slot++) {
            u8 bias = (u8)(slot * 0x10);
            t = M.registry[slot];
            if (t == NULL)
                break;
            if (t[T_HOLD] != 0)
                continue;
            if (t[T_DIGITAL] != 0) {
                if (M.streams[0] == NULL)
                    M.streams[0] = t;
                continue;
            }
            memcpy(M.snapStage, M.voiceStage, 16);
            memcpy(M.snapAge, M.voiceAge, 16);
            memcpy(M.snapCost, M.voiceCost, 16);
            memcpy(M.snapFlags, M.voiceFlags, 16);
            M.g.budgetSave = budget;
            for (unsigned stream = 0; stream < 16; stream++) {
                int next = give_voice(t, slot, stream, bias, &budget);
                if (next == NEXT_TRACK)
                    break;
                if (next == UNDO_TRACK) {
                    memcpy(M.voiceStage, M.snapStage, 16);
                    memcpy(M.voiceAge, M.snapAge, 16);
                    memcpy(M.voiceCost, M.snapCost, 16);
                    memcpy(M.voiceFlags, M.snapFlags, 16);
                    budget = M.g.budgetSave;
                    break;
                }
            }
        }

        /* Own voices take their number; other owners keep the voice they had. */
        for (v = 0; v < 16; v++) {
            u8 key = M.voiceStage[v];
            if (key == 0xff)
                continue;
            t = M.registry[key >> 4];
            u8 ch = key & 0x0f;
            if (M.voiceFlags[v] != 0) {
                M.voiceStage[v] = 0xff;
                M.voiceOwner[v] = key;
                if (M.voiceChannel[v] != ch || M.voiceTrack[v] != t)
                    rekey(v, t, ch);
                continue;
            }
            /* The voices from voiceLow to voiceHigh (past 31 no entry can match). */
            for (unsigned w = M.g.voiceLow; w < 32; w++) {
                if (M.voiceTrack[w] == t && M.voiceChannel[w] == ch) {
                    if (M.voiceFlags[w] == 0) {
                        M.voiceOwner[w] = M.voiceStage[v];
                        M.voiceStage[v] = 0xff;
                    }
                    break;
                }
                if (w == M.g.voiceHigh)
                    break;
            }
        }

        /* The rest take free voices from the top of the range down; their stage entries
         * stay as they are. */
        int w = (u8)(M.g.voiceHigh + 1);
        for (v = 0; v < 16; v++) {
            u8 key = M.voiceStage[v];
            if (key == 0xff)
                continue;
            do {
                if (--w < 0)
                    bak_unimplemented("MIDI.ASM: no free voice for a new owner (the original "
                                      "goes on into the tables before its voice table)",
                                      __FILE__, __LINE__);
            } while (M.voiceOwner[w] != 0xff);
            M.voiceOwner[w] = key;
            rekey((unsigned)w, M.registry[key >> 4], key & 0x0f);
        }
    } else {
        memset(M.voiceOwner, 0xff, 16);
    }

    for (int i = 15; i >= 0; i--)
        if (M.voiceChannel[i] != 0x0f && M.voiceOwner[i] == 0xff) {
            drv(7, (u16)i, 0x4000);
            drv(7, (u16)i, 0x7b00);
            drv(7, (u16)i, 0x4b00);
        }
    for (v = 0; v < 16; v++)
        M.voiceChannel[v] = M.voiceOwner[v] & 0x0f;
    for (v = 0; v < 16; v++)
        M.voiceTrack[v] = M.voiceOwner[v] == 0xff ? NULL : M.registry[M.voiceOwner[v] >> 4];
    M.g.lock--;
}

/* ---- volume ------------------------------------------------------------------------- */

/* midi_apply_vel_change_active (19D3h): a new master volume for track t (registry offset
 * si), sent to its voices now or queued in voiceRefresh (defer). */
static void set_master(unsigned char *t, unsigned si, u8 master, u8 defer) {
    M.g.deferVolume = defer;
    if (master == t[T_MASTER])
        return;
    t[T_MASTER] = master;
    if (si == NOT_FOUND)
        return;
    u8 key = (u8)(si << 2);
    for (unsigned v = 0; v < 16; v++) {
        u8 owner = M.voiceOwner[v];
        if (owner == 0xff || (owner & 0xf0) != key)
            continue;
        u8 vol = scale_volume(t[T_VOLUME + (owner & 0x0f)], t[T_MASTER]);
        if (M.g.deferVolume) {
            M.voiceRefresh[v] = vol;
        } else {
            M.voiceRefresh[v] = 0xff;
            drv(7, (u16)v, (u16)(0x0700 | vol));
        }
    }
    /* Fixed-voice channels whose voice is free. The index is CH:CL, and CH is 0 until a
     * driver call returns it (7 from the MT-32's controller 7); past 15 the original
     * looks into its own code at 2E2h + index, where none of the bytes these indexes
     * reach is FFh, so the channel is passed over (without reading past the record). */
    u8 hi = 0;
    for (unsigned stream = 0; stream < 16; stream++) {
        u8 cl = t[T_CHANNEL + stream];
        if (cl == 0xff)
            return;
        u16 c = (u16)(hi << 8 | cl);
        if (c >= 16 || !(t[T_FLAGS + c] & 2) || M.voiceOwner[c] != 0xff)
            continue;
        u8 vol = scale_volume(t[T_VOLUME + c], t[T_MASTER]);
        if (M.g.deferVolume) {
            M.voiceRefresh[c] = vol;
        } else {
            M.voiceRefresh[c] = 0xff;
            hi = (u8)(drv(7, c, (u16)(0x0700 | vol)).cx >> 8);
        }
    }
}

/* midi_task_tick_one_slot (1913h): every T_FADE_PERIOD + 1 ticks the master volume moves
 * T_FADE_STEP toward the target, sent through the queue; at the target the fade ends. */
static void fade_step(unsigned char *t, unsigned si) {
    if (t[T_FADE_COUNT] != 0) {
        t[T_FADE_COUNT]--;
        return;
    }
    t[T_FADE_COUNT] = t[T_FADE_PERIOD];
    u8 target = t[T_FADE_TARGET] & 0x7f, cur = t[T_MASTER], step = t[T_FADE_STEP];
    if (target != cur) {
        if (target > cur) {
            if ((u8)(target - cur) > step) {
                set_master(t, si, (u8)(cur + step), 1);
                return;
            }
        } else if ((u8)(cur - target) > step) {
            set_master(t, si, (u8)(cur - step), 1);
            return;
        }
        set_master(t, si, target, 1);
    }
    t[T_STATE] = 0xfe;
    t[T_FADE_STEP] = 0;
    if (t[T_FADE_TARGET] & 0x80) {
        track_release(t);
        M.g.dirty = 1;
    }
}

/* midi_task_scheduler_advance (1AB0h): up to two queued volumes a tick, round robin. */
static void send_queued_volumes(void) {
    unsigned sent = 0, v = M.g.refreshCursor;
    do {
        u8 vol = M.voiceRefresh[v];
        if (vol != 0xff) {
            M.voiceRefresh[v] = 0xff;
            drv(7, (u16)v, (u16)(0x0700 | vol));
            if (++sent == 2)
                break;
        }
        if (++v == 16)
            v = 0;
    } while (v != M.g.refreshCursor);
    M.g.refreshCursor = (u8)v;
}

/* ---- events ------------------------------------------------------------------------- */

/* The handlers read their data from the stream at bp, counting each byte in T_POS, and
 * return where they stopped; voice is the stream's voice (FFh none). */

static unsigned char *note_off(unsigned char *t, unsigned s, u8 voice, unsigned char *bp) {
    u8 note = *bp++, velocity;
    inc_pos(t, s);
    velocity = *bp++;
    inc_pos(t, s);
    unsigned ch = t[T_CHANNEL + s] & 0x0f;
    if (t[T_KEY + ch] == note)
        t[T_KEY + ch] = 0xff;
    if (voice != 0xff && !M.g.seeking)
        drv(4, voice & 0x0f, (u16)(note << 8 | velocity));
    return bp;
}

static unsigned char *note_on(unsigned char *t, unsigned s, u8 voice, unsigned char *bp) {
    u8 note = *bp++, velocity;
    inc_pos(t, s);
    velocity = *bp++;
    inc_pos(t, s);
    unsigned ch = t[T_CHANNEL + s] & 0x0f;
    if (velocity != 0) {
        t[T_KEY + ch] = note;
        if (voice != 0xff && !M.g.seeking)
            drv(5, voice & 0x0f, (u16)(note << 8 | velocity));
    } else {
        if (t[T_KEY + ch] == note)
            t[T_KEY + ch] = 0xff;
        if (voice != 0xff && !M.g.seeking)
            drv(4, voice & 0x0f, (u16)(note << 8));
    }
    return bp;
}

static unsigned char *poly_pressure(unsigned char *t, unsigned s, u8 voice, unsigned char *bp) {
    u8 note = *bp++, value;
    inc_pos(t, s);
    value = *bp++;
    inc_pos(t, s);
    if (voice != 0xff && !M.g.seeking)
        drv(6, voice, (u16)(note << 8 | value));
    return bp;
}

/* A fixed-voice stream gives way while the arbiter has given its voice to a channel. */
static int fixed_voice_taken(u8 voice) {
    return M.g.eventFixed && M.voiceOwner[voice & 0x0f] != 0xff;
}

static unsigned char *controller(unsigned char *t, unsigned s, u8 voice, unsigned char *bp) {
    u8 ctl = *bp++, value;
    inc_pos(t, s);
    value = *bp++;
    inc_pos(t, s);
    if (fixed_voice_taken(voice))
        return bp;
    unsigned ch = t[T_CHANNEL + s] & 0x0f;
    switch (ctl) {
    case 0x07:
        t[T_VOLUME + ch] = value;
        value = scale_volume(value, t[T_MASTER]);
        if (voice >= 0x20)
            return bp;
        M.voiceRefresh[voice] = 0xff;
        break;
    case 0x0a:
        t[T_PAN + ch] = value;
        break;
    case 0x01:
        t[T_MODULATION + ch] = value;
        break;
    case 0x40: {
        u16 bend = W(t, T_BEND + 2 * ch) | 0x8000;
        if (value == 0)
            bend &= 0x7fff;
        SETW(t, T_BEND + 2 * ch, bend);
        break;
    }
    case 0x4b: /* the voice's cost; the value is ORed in whole */
        t[T_VOICE + ch] = (u8)((t[T_VOICE + ch] & 0xf0) | value);
        M.g.dirty = 1;
        break;
    case 0x4e:
        t[T_MUTE + ch] = (u8)((t[T_MUTE + ch] & 0xf0) | (value != 0));
        M.g.dirty = 1;
        break;
    }
    if (voice != 0xff && !M.g.seeking)
        drv(7, voice & 0x0f, (u16)(ctl << 8 | value));
    return bp;
}

static unsigned char *program(unsigned char *t, unsigned s, u8 voice, unsigned char *bp) {
    u8 value = *bp++;
    inc_pos(t, s);
    if (fixed_voice_taken(voice))
        return bp;
    t[T_PROGRAM + (t[T_CHANNEL + s] & 0x0f)] = value;
    if (voice != 0xff && !M.g.seeking)
        drv(8, voice & 0x0f, value);
    return bp;
}

static unsigned char *channel_pressure(unsigned char *t, unsigned s, u8 voice, unsigned char *bp) {
    u8 value = *bp++;
    inc_pos(t, s);
    if (voice != 0xff && !M.g.seeking)
        drv(9, voice, value);
    return bp;
}

static unsigned char *pitch_bend(unsigned char *t, unsigned s, u8 voice, unsigned char *bp) {
    u8 lsb = *bp++, msb;
    inc_pos(t, s);
    msb = *bp++;
    inc_pos(t, s);
    if (fixed_voice_taken(voice))
        return bp;
    unsigned ch = t[T_CHANNEL + s] & 0x0f;
    u16 bend = (u16)((msb >> 1) << 8 | lsb | (msb & 1) << 7);
    if (W(t, T_BEND + 2 * ch) >= 0x8000)
        bend |= 0x8000;
    SETW(t, T_BEND + 2 * ch, bend);
    if (voice != 0xff && !M.g.seeking)
        drv(10, voice & 0x0f, (u16)(lsb << 8 | msb));
    return bp;
}

/* midi_consume_event_sysex_f0 (22B2h): F0h-FEh run to F7h; C0h and D0h have one data
 * byte, the others two. */
static unsigned char *skip_event(unsigned char *t, unsigned s, u8 ah, unsigned char *bp) {
    if (ah == 0xf0) {
        u8 c;
        do {
            c = *bp++;
            inc_pos(t, s);
        } while (c != 0xf7);
        return bp;
    }
    if (ah != 0xc0 && ah != 0xd0) {
        bp++;
        inc_pos(t, s);
    }
    bp++;
    inc_pos(t, s);
    return bp;
}

/* midi_dispatch_meta_or_control (21A2h): the events on channel 15. CFh 7Fh d: the loop
 * point (saved after its wait byte d, which is read again); CFh n: T_STATE = n. BFh 50h
 * v: the driver's slot 11; BFh 60h: count a mark; BFh 52h c: rewind every stream to its
 * end if T_CUE = c. Anything else is skipped. */
static unsigned char *meta_event(unsigned char *t, unsigned s, u8 ah, unsigned char *bp) {
    if (ah == 0xc0) {
        u8 ch = *bp++;
        inc_pos(t, s);
        if (ch == 0x7f) {
            u8 dl = *bp++;
            inc_pos(t, s);
            SETW(t, T_WAIT + 2 * s, dl == 0xf8 ? 0x80f0 : dl);
            t[T_STATUS + s] = 0xcf;
            for (unsigned i = 0; i < 16; i++) {
                SETW(t, T_LOOP_POS + 2 * i, W(t, T_POS + 2 * i));
                SETW(t, T_LOOP_WAIT + 2 * i, W(t, T_WAIT + 2 * i));
                t[T_LOOP_STATUS + i] = t[T_STATUS + i];
            }
            SETW(t, T_LOOP_CLOCK, W(t, T_CLOCK));
            SETW(t, T_POS + 2 * s, W(t, T_POS + 2 * s) - 1);
            bp--;
            SETW(t, T_WAIT + 2 * s, 0);
        } else if (!M.g.seeking) {
            t[T_STATE] = ch;
        }
        return bp;
    }
    if (ah == 0xb0) {
        u8 ch = *bp++, cl;
        inc_pos(t, s);
        cl = *bp++;
        inc_pos(t, s);
        if (ch == 0x50) {
            if (cl == 0x7f)
                cl = M.g.globalParam;
            t[T_PARAM] = cl;
            drv(11, 0, cl);
        } else if (ch == 0x60) {
            if (!M.g.seeking)
                SETW(t, T_MARKS, W(t, T_MARKS) + 1);
        } else if (ch == 0x52 && t[T_CUE] == cl) {
            for (unsigned i = 0; i < 16; i++)
                SETW(t, T_POS + 2 * i, 0);
        }
        return bp;
    }
    return skip_event(t, s, ah, bp);
}

/* midi_tick (1C72h): one tick of track t, whose registry offset the scheduler counts in
 * di (it runs ahead of the track's slot after a track stops in the same tick, as in the
 * original). A stream waits out T_WAIT, else plays events up to the next nonzero wait
 * byte (F8h: a long wait, read on at 8000h). When every stream has ended the track loops
 * (T_CUE or T_LOOP set) or stops. */
static void track_tick(unsigned char *t, unsigned di) {
    unsigned char *script;
    unsigned s;
    M.g.slotKey = (u8)(di << 2);
    SETW(t, T_CLOCK, W(t, T_CLOCK) + 1);
    script = script_of(t);
    M.cmdSlots[6] = (u16)(uintptr_t)script; /* the original keeps the script's offset here */
    for (s = 0; s < 16; s++) {
        u8 al = t[T_CHANNEL + s];
        if (al == 0xff)
            break;
        if (al == 0xfe)
            continue;
        M.g.eventVoice = 0xff;
        M.g.eventFixed = 0;
        if (t[T_FLAGS + al] & 2) {
            M.g.eventVoice = al;
            M.g.eventFixed = 1;
        } else {
            u8 key = (u8)((al & 0x0f) | M.g.slotKey);
            for (unsigned v = 0; v < 16; v++)
                if (M.voiceOwner[v] == key) {
                    M.g.eventVoice = (u8)v;
                    break;
                }
        }
        u16 pos = W(t, T_POS + 2 * s);
        if (pos == 0)
            continue;
        unsigned char *bp = script + (u16)(rd16(script + 2 * s) + pos);
        u16 wait = W(t, T_WAIT + 2 * s);
        if (wait != 0) {
            SETW(t, T_WAIT + 2 * s, --wait);
            if (wait == 0x8000) {
                u8 dl = *bp++;
                inc_pos(t, s);
                SETW(t, T_WAIT + 2 * s, dl == 0xf8 ? 0x80f0 : dl);
            }
            continue;
        }
        for (;;) {
            u8 dl = *bp++;
            inc_pos(t, s);
            if (dl >= 0x80) {
                t[T_STATUS + s] = dl;
            } else {
                dl = t[T_STATUS + s];
                bp--;
                SETW(t, T_POS + 2 * s, W(t, T_POS + 2 * s) - 1);
            }
            u8 ah = dl & 0xf0;
            if (dl == 0xfc) {
                SETW(t, T_POS + 2 * s, 0);
                break;
            }
            if ((dl & 0x0f) == 0x0f) {
                bp = meta_event(t, s, ah, bp);
                if (W(t, T_POS + 2 * s) == 0)
                    break;
            } else {
                u8 voice = M.g.eventVoice;
                switch (ah) {
                case 0x80:
                    bp = note_off(t, s, voice, bp);
                    break;
                case 0x90:
                    bp = note_on(t, s, voice, bp);
                    break;
                case 0xa0:
                    bp = poly_pressure(t, s, voice, bp);
                    break;
                case 0xb0:
                    bp = controller(t, s, voice, bp);
                    break;
                case 0xc0:
                    bp = program(t, s, voice, bp);
                    break;
                case 0xd0:
                    bp = channel_pressure(t, s, voice, bp);
                    break;
                case 0xe0:
                    bp = pitch_bend(t, s, voice, bp);
                    break;
                case 0xf0: /* skipped like a SysEx message */
                    bp = skip_event(t, s, ah, bp);
                    break;
                default: /* a running status below 80h (a loop without a loop point) */
                    SETW(t, T_POS + 2 * s, 0);
                    goto next_stream;
                }
            }
            dl = *bp++;
            inc_pos(t, s);
            if (dl == 0)
                continue;
            SETW(t, T_WAIT + 2 * s, dl == 0xf8 ? 0x80ef : dl - 1);
            break;
        }
    next_stream:;
    }

    for (s = 0; s < 16 && t[T_CHANNEL + s] != 0xff; s++)
        if (W(t, T_POS + 2 * s) != 0)
            return;
    if (t[T_CUE] == 0 && t[T_LOOP] == 0) {
        track_release(t);
        M.g.dirty = 1;
        return;
    }
    SETW(t, T_CLOCK, W(t, T_LOOP_CLOCK));
    for (s = 0; s < 16; s++) {
        SETW(t, T_POS + 2 * s, W(t, T_LOOP_POS + 2 * s));
        SETW(t, T_WAIT + 2 * s, W(t, T_LOOP_WAIT + 2 * s));
        t[T_STATUS + s] = t[T_LOOP_STATUS + s];
    }
}

/* ---- the digital driver's stream --------------------------------------------------- */

/* midi_track_verify_or_release (1B7Ch): a digital track that is no longer the stream
 * being played stops. */
static void check_stream(unsigned char *t) {
    for (unsigned i = 0; i < 16; i++)
        if (M.streams[i] == t)
            return;
    track_release(t);
    M.g.dirty = 1;
}

/* midi_track_tick_loop_16channel (1BA8h): the digital stream is started (command 3),
 * then polled (command 4): AH nonzero restarts the track's clock, AL nonzero ends it. */
static void pump_streams(void) {
    for (unsigned i = 0; i < 16; i++) {
        unsigned char *t = M.streams[i];
        if (t == NULL)
            break;
        SETW(t, T_CLOCK, W(t, T_CLOCK) + 1);
        unsigned char *script = script_of(t);
        u8 index = (u8)(((t[T_DIGITAL] & 0x0f) - 1) << 1);
        unsigned char *stream = script + rd16(script + index);
        if (t[T_DIGITAL] <= 0x10) {
            t[T_DIGITAL] |= 0x80;
            unsigned char *p = stream + 1;
            if (*p == 0xfe)
                p++;
            p++;
            /* The original reads these two bytes at the track's segment with the stream
             * data's offset (ES:[BX+15Eh] after BX moved to the data). */
            const unsigned char *odd = MK_FP(FP_SEG(t), (u16)(FP_OFF(p) + 0x15e));
#pragma pack(push, 1)
            struct {
                u16 masterAndLoop;
                u16 w0;
                unsigned char *data;
                u16 w2;
            } block = {(u16)(odd[0] | odd[-1] << 8), rd16(p), p + 8, rd16(p + 2)};
#pragma pack(pop)
            snddrv_dispatch_cached(3, &block);
        } else {
            u16 masterAndLoop = (u16)(t[T_MASTER] | t[T_LOOP] << 8);
            u16 r = snddrv_dispatch_cached(4, &masterAndLoop);
            if (r >> 8)
                SETW(t, T_CLOCK, 0);
            if (r & 0xff) {
                t[T_DIGITAL] = 0;
                track_release(t);
                M.g.dirty = 1;
            }
        }
    }
}

/* ---- commands ----------------------------------------------------------------------- */

/* midi_cmd0_program_change (061Ch): install the driver at chunk and ask it to describe
 * itself (slot 0); returns its AX. */
static u16 install(unsigned char *chunk) {
    DudeEntry *entry = dude_synth_entry(chunk);
    if (entry == NULL) {
        char name[64], what[128];
        dude_device_name(chunk, name, sizeof name);
        snprintf(what, sizeof what, "the SX.OVL sound driver \"%s\"", name);
        bak_unimplemented(what, __FILE__, __LINE__);
    }
    M.drvvec = chunk;
    s_driver = entry;
    DudeRegs r = drv(0, 0, 0);
    M.g.budget = (u8)r.cx;
    M.g.deviceId = (u8)(r.cx >> 8);
    u8 caps = (u8)(r.ax >> 12);
    if (g_bMusicDriverInstalled != 0)
        caps |= 1;
    M.g.caps = caps;
    return r.ax;
}

/* midi_cmd1_save_cx_303b4 (0653h): load the bank at bank (slot 1), keep its voice range,
 * then slot 11 with 0; returns slot 1's AX (FFFFh: failed). */
static u16 load_bank(const unsigned char *bank) {
    DudeRegs r = {(u16)(uintptr_t)bank, 0, bank};
    if (s_driver == NULL)
        bak_unimplemented("MIDI.ASM: a driver call with no driver installed", __FILE__, __LINE__);
    s_driver(M.drvvec, 1, &r);
    M.g.voiceLow = (u8)r.cx;
    M.g.voiceHigh = (u8)(r.cx >> 8);
    drv(11, r.ax, r.cx & 0xff00);
    return r.ax;
}

/* midi_cmd2_driver_dispatch_pair (0678h): master volume 15 (slot 12), then reset. */
static void reset_driver(void) {
    drv(12, 0, 0x0f);
    drv(2, 0, 0x0f);
}

/* midi_scheduler_pause_refcount (0697h). */
static void pause_all(u16 cx) {
    if (cx == 0)
        M.g.lock++;
    else if (M.g.lock != 0)
        M.g.lock--;
}

/* midi_track_init (07ADh): (re)start track t, as a sound effect (mode 0) or music.
 * Streams start as their headers say: [channel and flags, voice cost/priority, -, -,
 * program, -, -, -, volume, -, -, pan, first wait]; the flags give a fixed voice (10h,
 * events from offset 3), the voice numbered as the channel (20h) or a mute (40h).
 * Channel 15's header holds the track's slot 11 value. The track goes into the registry
 * before the first of lower or equal priority; with 16 of higher priority playing it
 * does not start. Returns what the original leaves in DI: the track's registry offset,
 * 40h when the registry was full. */
static unsigned track_init(unsigned char *t, u16 mode) {
    unsigned s;
    for (unsigned i = 0; i < 16; i++)
        if (M.registry[i] == t) {
            track_release(t);
            recompute_voices();
            break;
        }
    t[T_KIND] = mode != 0 ? 2 : 1;
    parse_track_table(t);
    for (s = 0; s < 15; s++) {
        SETW(t, T_POS + 2 * s, 0x0d);
        SETW(t, T_LOOP_POS + 2 * s, 3);
        SETW(t, T_WAIT + 2 * s, 0);
        SETW(t, T_LOOP_WAIT + 2 * s, 0);
        SETW(t, T_BEND + 2 * s, 0x2000);
        t[T_CHANNEL + s] = 0xff;
        t[T_STATUS + s] = 0;
        t[T_LOOP_STATUS + s] = 0;
        t[T_VOICE + s] = 0xff;
        t[T_MODULATION + s] = 0;
        t[T_PROGRAM + s] = 0xff;
        t[T_VOLUME + s] = 0xff;
        t[T_PAN + s] = 0xff;
        t[T_KEY + s] = 0xff;
        t[T_FLAGS + s] = 0;
        t[T_MUTE + s] = 0;
    }
    t[T_CHANNEL + 15] = 0xff;
    t[T_STATUS + 15] = 0;
    t[T_LOOP_STATUS + 15] = 0;
    t[T_DIGITAL] = 0;
    t[T_CUE] = 0;
    t[T_PARAM] = 0x7f;
    SETW(t, T_POS + 30, 0x0d);
    SETW(t, T_LOOP_POS + 30, 3);
    SETW(t, T_WAIT + 30, 0);
    SETW(t, T_LOOP_CLOCK, 0);

    unsigned char *script = script_of(t);
    if (script[0x20] != 0xff && t[T_KEEP_PRIORITY] == 0)
        t[T_PRIORITY] = script[0x20];
    for (s = 0; s < 16; s++) {
        u16 offset = rd16(script + 2 * s);
        if (offset == 0)
            break;
        unsigned char *h = script + offset;
        u8 dl = h[0];
        if (dl == 0xfe) {
            if (M.g.caps == 0) {
                SETW(t, T_POS + 2 * s, 0);
                SETW(t, T_LOOP_POS + 2 * s, 0);
                t[T_CHANNEL + s] = 0xfe;
                continue;
            }
            t[T_DIGITAL] = (u8)(s + 1);
            break;
        }
        t[T_CHANNEL + s] = dl;
        t[T_STATUS + s] = dl | 0xb0;
        SETW(t, T_WAIT + 2 * s, h[0x0c] == 0xf8 ? 0x80f0 : h[0x0c]);
        t[T_CHANNEL + s] &= 0x0f;
        unsigned c = dl & 0x0f;
        if (dl & 0x10) {
            SETW(t, T_POS + 2 * s, 3);
            SETW(t, T_WAIT + 2 * s, 0);
            t[T_FLAGS + c] |= 2;
            continue;
        }
        if (dl & 0x20)
            t[T_FLAGS + c] |= 1;
        if (dl & 0x40)
            t[T_MUTE + c] = 1;
        if (c == 15) {
            if (t[T_PARAM] == 0x7f) {
                t[T_PARAM] = h[8];
                continue;
            }
        } else {
            if (t[T_VOICE + c] == 0xff)
                t[T_VOICE + c] = h[1];
            if (t[T_PROGRAM + c] == 0xff)
                t[T_PROGRAM + c] = h[4];
            if (t[T_VOLUME + c] == 0xff)
                t[T_VOLUME + c] = h[8];
        }
        if (t[T_PAN + c] == 0xff)
            t[T_PAN + c] = h[0x0b];
    }
    if (t[T_KIND] == 2)
        for (s = 0; s < 15; s++)
            t[T_FLAGS + s] |= 1;

    unsigned at;
    for (at = 0; at < 16; at++) {
        if (M.registry[at] == NULL)
            break;
        if (M.registry[at][T_PRIORITY] <= t[T_PRIORITY]) {
            memmove(&M.registry[at + 1], &M.registry[at], (15 - at) * sizeof M.registry[0]);
            break;
        }
    }
    if (at == 16)
        return 0x40;
    M.registry[at] = t;
    if (M.g.seeking)
        return at * 4;
    SETW(t, T_MARKS, 0);
    SETW(t, T_CLOCK, 0);
    t[T_STATE] = 0;
    t[T_FADE_TARGET] = 0;
    t[T_FADE_PERIOD] = 0;
    t[T_FADE_COUNT] = 0;
    t[T_FADE_STEP] = 0;
    t[T_HOLD] = 0;
    recompute_voices();
    return at * 4;
}

/* midi_seek_with_volume_preserve (06BBh): restart track t and play it silently back to
 * the tick it had reached (looping as needed), keeping the high nibbles of channels
 * 0-14's T_MUTE. Only reached through midi_queue_command and its own thunk, neither of
 * which the game calls. midi_tick gets the DI midi_track_init leaves behind. */
static void seek(unsigned char *t) {
    for (int c = 14; c >= 0; c--)
        M.seekSave[c] = t[T_MUTE + c] & 0xf0;
    M.g.seeking = 1;
    unsigned di = track_init(t, (u8)(t[T_KIND] - 1));
    u16 target = W(t, T_CLOCK);
    SETW(t, T_CLOCK, 0);
    u8 loop = t[T_LOOP];
    t[T_LOOP] = 1;
    if (target != 0)
        for (;;) {
            u16 before = W(t, T_CLOCK);
            track_tick(t, di);
            u16 after = W(t, T_CLOCK);
            if (before == after)
                break;
            if (before > after) /* looped back */
                target = (u16)(target - (before - after));
            if (target == W(t, T_CLOCK))
                break;
        }
    t[T_LOOP] = loop;
    M.g.seeking = 0;
    for (int c = 14; c >= 0; c--)
        t[T_MUTE + c] |= M.seekSave[c];
    recompute_voices();
}

/* midi_call_dispatch_cmd_13000 (074Bh): master volume (slot 12), at most 15. */
static u16 set_volume(u16 cx) {
    u8 cl = (u8)cx;
    if (cl != 0xff && cl > 0x0f)
        cl = 0x0f;
    return drv(12, 0, (u16)((cx & 0xff00) | cl)).ax;
}

/* midi_cmd7_set_global_clamped (0762h): FFh asks the driver (slot 11); 0-10 becomes the
 * global value, passed on now if the first track uses it. Returns the previous value. */
static u16 set_global(u16 cx) {
    u8 cl = (u8)cx;
    if (cl == 0xff)
        return drv(11, 0, cx).ax;
    u8 old = M.g.globalParam;
    if (cl > 0x0a)
        return old;
    M.g.globalParam = cl;
    unsigned char *t = M.registry[0];
    if (t != NULL && t[T_PARAM] == 0x7f)
        drv(11, 0, cx);
    return old;
}

/* midi_cmd0a_adjust_hold_count (0A8Bh): pause (cl != 0) or resume one track, or every
 * track for a null handle. */
static void hold(unsigned char *t, u8 cl) {
    unsigned i;
    if (t == NULL) {
        for (i = 0; i < 16; i++) {
            unsigned char *r = M.registry[i];
            if (r == NULL) {
                if (i != 0)
                    break;
                continue; /* an empty slot 0 is passed over, once */
            }
            if (cl != 0)
                r[T_HOLD]++;
            else if (r[T_HOLD] != 0)
                r[T_HOLD]--;
        }
    } else if (cl != 0) {
        t[T_HOLD]++;
    } else if (t[T_HOLD] != 0) {
        t[T_HOLD]--;
    }
    recompute_voices();
}

/* midi_fade_program (0AF3h): unless already at the target. */
static void fade(unsigned char *t, u8 target, u8 period, u8 step) {
    if (t[T_MASTER] == target)
        return;
    t[T_FADE_TARGET] = target;
    t[T_FADE_PERIOD] = period;
    t[T_FADE_STEP] = step;
    t[T_FADE_COUNT] = 0;
}

/* midi_pitch_bend_step (0B21h): move the high nibble of channels 0-14's T_MUTE up
 * (cx != 0, to F0h) or down (to 0Fh or below) by one. */
static void step_mutes(unsigned char *t, u16 cx) {
    M.g.lock++;
    for (int c = 14; c >= 0; c--) {
        u8 m = t[T_MUTE + c];
        if (cx == 0) {
            if (m > 0x0f)
                m = (u8)(m - 0x10);
        } else if (m < 0xf0) {
            m = (u8)(m + 0x10);
        }
        t[T_MUTE + c] = m;
    }
    recompute_voices();
    M.g.lock--;
}

/* midi_chord_propagate_setting (0B6Eh): a new priority for t. Its entry leaves the
 * registry; the original then means to re-insert it in order, but its test is a MOV
 * (MOV ES:[BX+15Ch],DL where CMP was meant): every track ahead of the first empty slot
 * takes the new priority too, and t goes into that slot. */
static void set_priority(unsigned char *t, u8 cl) {
    if (t[T_PRIORITY] == cl)
        return;
    t[T_PRIORITY] = cl;
    unsigned si = registry_offset(t);
    if (si == NOT_FOUND)
        return;
    unsigned i = si / 4;
    memmove(&M.registry[i], &M.registry[i + 1], (15 - i) * sizeof M.registry[0]);
    M.registry[15] = NULL;
    for (i = 0; M.registry[i] != NULL; i++)
        M.registry[i][T_PRIORITY] = cl;
    M.registry[i] = t;
    recompute_voices();
}

/* midi_stub_return_ax (0C4Ch): the clock as units of 3600 ticks (DX), then CH = the
 * rest / 60 and CL = (the rest % 60) / 2. */
static u16 clock_parts(const unsigned char *t, u16 *dx) {
    u16 clock = W(t, T_CLOCK);
    u16 rest = clock % 3600;
    *dx = clock / 3600;
    return (u16)((rest / 60) << 8 | (rest % 60) >> 1);
}

/* The voice of channel ch of the track at registry offset si, or 16. */
static unsigned voice_of(unsigned si, u8 ch) {
    u8 key = (u8)((si << 2) | ch);
    unsigned v;
    for (v = 0; v < 16 && M.voiceOwner[v] != key; v++)
        ;
    return v;
}

/* midi_cmd13_voice_release / 14 / 16 (0C6Dh, 0CBFh, 0DEEh): a note off, a note on or a
 * program on one channel of t, through its voice if it has one. */
static void voice_command(unsigned char *t, u16 slot, u8 dl, u16 cx) {
    M.g.lock++;
    unsigned si = registry_offset(t);
    if (si != NOT_FOUND) {
        if (slot == 4)
            t[T_KEY + dl] = 0xff;
        else if (slot == 5)
            t[T_KEY + dl] = (u8)(cx >> 8);
        else
            t[T_PROGRAM + dl] = (u8)cx;
        unsigned v = voice_of(si, dl);
        if (v < 16)
            drv(slot, (u16)v, cx);
    }
    M.g.lock--;
}

/* midi_apply_controller_change (0D10h): a controller on channel dl of t, as from the
 * script, then sent through the channel's voice (7Fh: as a program, slot 8). 4Eh moves
 * the channel's T_MUTE count (cl != 0 up) and is not sent. */
static void apply_controller(unsigned char *t, u8 ch, u8 cl, u8 dl) {
    unsigned si = registry_offset(t);
    if (si == NOT_FOUND)
        return;
    switch (ch) {
    case 0x07:
        t[T_VOLUME + dl] = cl;
        cl = scale_volume(cl, t[T_MASTER]);
        break;
    case 0x0a:
        t[T_PAN + dl] = cl;
        break;
    case 0x01:
        t[T_MODULATION + dl] = cl;
        break;
    case 0x40: {
        u16 bend = W(t, T_BEND + 2 * dl) & 0x7fff;
        if (cl != 0)
            bend |= 0x8000;
        SETW(t, T_BEND + 2 * dl, bend);
        break;
    }
    case 0x4e: {
        u8 m = t[T_MUTE + dl];
        if (cl == 0) {
            if (m <= 0x0f)
                return;
            m = (u8)(m - 0x10);
        } else {
            if (m >= 0xf0)
                return;
            m = (u8)(m + 0x10);
        }
        t[T_MUTE + dl] = m;
        recompute_voices();
        return;
    }
    case 0x7f:
        t[T_PROGRAM + dl] = cl;
        break;
    }
    unsigned v = voice_of(si, dl);
    if (v < 16)
        drv(ch == 0x7f ? 8 : 7, (u16)v, (u16)(ch << 8 | cl));
}

/* midi_track_set_word_bc (0E3Fh): set channel dl's bend to cx (keeping the pedal bit)
 * and send it through the channel's voice. */
static void set_bend(unsigned char *t, u16 cx, u8 dl) {
    unsigned si = registry_offset(t);
    if (si == NOT_FOUND)
        return;
    u16 bend = cx;
    if (t[T_BEND + 2 * dl + 1] >= 0x80)
        bend |= 0x8000;
    SETW(t, T_BEND + 2 * dl, bend);
    unsigned v = voice_of(si, dl);
    if (v == 16)
        return;
    u8 hi = (u8)((cx >> 8) << 1 | ((cx & 0xff) >= 0x80));
    u8 lo = (u8)(cx & 0x7f);
    drv(10, (u16)v, (u16)(lo << 8 | hi));
}

/* ---- the C interface ------------------------------------------------------------------ */

unsigned short midi_stub_return_cx_via_nop_alt(void *ptr) {
    return W((unsigned char *)ptr, T_MARKS);
}

void midi_chord_event_thunk_0b6e(void *ptr, s16 note) {
    set_priority(ptr, (u8)note);
}

s16 midi_stub_return_cx_via_nop(void *ptr) {
    u16 dx;
    return (s16)clock_parts(ptr, &dx);
}

void midi_seek_volume_preserve_thunk(void *midi_state) {
    seek(midi_state);
}

void midi_cmd6_far_wrapper(unsigned short voice_count) {
    set_volume(voice_count);
}

void midi_event_callback_thunk_0a8b(void *ptr, s16 count) {
    hold(ptr, (u8)count);
}

/* The chunk's far pointer arrives as offset and segment words. */
unsigned char midi_driver_install_dispatch(u16 offset, u16 seg) {
    return (unsigned char)install(far_ptr(offset, seg));
}

unsigned short midi_cmd1_save_cx_303b4_thunk(void *ptr) {
    return load_bank(ptr);
}

void midi_track_init_thunk(AudioTrackHandle *handle, s16 start_flag) {
    track_init((unsigned char *)handle, (u16)start_flag);
}

void midi_fade_queue_handle(AudioTrackHandle *handle, s16 target, s16 step, s16 dir) {
    fade((unsigned char *)handle, (u8)target, (u8)step, (u8)dir);
}

void midi_cmd4_far_wrapper(s16 a) {
    drv(13, 0, (u16)a);
}

void midi_track_apply_intensity(void *track, s16 intensity) {
    unsigned char *t = track;
    set_master(t, registry_offset(t), (u8)intensity, 0);
}

void midi_track_set_word_bc_far(void *ptr, s16 word_val, s16 byte_val) {
    set_bend(ptr, (u16)word_val, (u8)byte_val);
}

void midi_active_release_thunk(AudioTrackHandle *ptr) {
    track_release((unsigned char *)ptr);
    recompute_voices();
}

void midi_track_set_byte_15a(void *track_ptr, s16 nValue) {
    ((unsigned char *)track_ptr)[T_CUE] = (u8)nValue;
}

unsigned char midi_track_read_byte_158_far(void *ptr) {
    return ((unsigned char *)ptr)[T_STATE];
}

void midi_set_note_velocity_thunk(s16 velocity) {
    set_global((u16)velocity);
}

void midi_apply_ctrl_change_thunk(void *state, s16 ctrl_num, s16 value, s16 channel) {
    apply_controller(state, (u8)ctrl_num, (u8)value, (u8)channel);
}

void midi_init_thunk_0678(void) {
    reset_driver();
}

void midi_pitch_bend_step_far_thunk(void *midi_obj, s16 direction) {
    step_mutes(midi_obj, (u16)direction);
}

/* midi_dispatch_command (0429h): a chain of tests on CL, not a switch. 0Fh, 11h and 12h
 * do not return, and the tests that follow run on what they leave in CL: 0Fh its
 * argument, 11h T_MARKS's low byte, 12h (clock % 60) / 2. */
static void dispatch(u8 cl, unsigned char *t) {
    u16 *slots = M.cmdSlots;
    switch (cl) {
    case 0x00: {
        u16 ax = install(t);
        u16 *p;
        (void)ax;
        p = far_ptr(slots[0], slots[1]);
        *p = M.g.budget;
        p = far_ptr(slots[2], slots[3]);
        *p = M.g.caps;
        p = far_ptr(slots[4], slots[5]);
        *p = M.g.deviceId;
        return;
    }
    case 0x01:
        load_bank(t);
        return;
    case 0x02:
        reset_driver();
        return;
    case 0x03:
        pause_all(slots[0]);
        return;
    case 0x04:
        drv(13, 0, slots[0]);
        return;
    case 0x05:
        seek(t);
        return;
    case 0x06:
        set_volume(slots[0]);
        return;
    case 0x07:
        set_global(slots[0]);
        return;
    case 0x08:
        track_init(t, slots[0]);
        return;
    case 0x09:
        track_release(t);
        recompute_voices();
        return;
    case 0x0a:
        hold(t, (u8)slots[0]);
        return;
    case 0x0b:
        fade(t, (u8)slots[0], (u8)slots[1], (u8)slots[2]);
        return;
    case 0x0c:
        t[T_CUE] = (u8)slots[0];
        return;
    case 0x0d:
        step_mutes(t, slots[0]);
        return;
    case 0x0e:
        set_master(t, registry_offset(t), (u8)slots[0], 0);
        return;
    case 0x0f:
        set_priority(t, (u8)slots[0]);
        cl = (u8)slots[0]; /* loaded into CX for the call, which keeps it */
        break;
    }
    if (cl == 0x11)
        cl = (u8)W(t, T_MARKS);
    if (cl == 0x10)
        return;
    if (cl == 0x19) {
        t[T_STATE] = 0;
        return;
    }
    if (cl == 0x12) {
        u16 dx, cx = clock_parts(t, &dx);
        *(u16 *)far_ptr(slots[0], slots[1]) = (u8)dx;
        *(u16 *)far_ptr(slots[2], slots[3]) = (u8)(cx >> 8);
        *(u16 *)far_ptr(slots[4], slots[5]) = (u8)cx;
        cl = (u8)cx;
    }
    switch (cl) {
    case 0x13:
        voice_command(t, 4, (u8)slots[0], (u16)((u8)slots[1] << 8 | (u8)slots[2]));
        break;
    case 0x14:
        voice_command(t, 5, (u8)slots[0], (u16)((u8)slots[1] << 8 | (u8)slots[2]));
        break;
    case 0x15:
        apply_controller(t, (u8)slots[1], (u8)slots[2], (u8)slots[0]);
        break;
    case 0x16:
        voice_command(t, 8, (u8)slots[0], (u8)slots[1]);
        break;
    case 0x17:
        set_bend(t, slots[1], (u8)slots[0]);
        break;
    }
}

/* midi_queue_command (03C7h): the six words go to the command slots (for 3, 4, 6 and 7
 * slot 0 is the handle's offset word, their argument), then the dispatch. Not called by
 * the game. */
void midi_queue_command(char kind, unsigned short arg2, unsigned short arg3, unsigned short slot0,
                        unsigned short slot1, unsigned short slot2, unsigned short slot3,
                        unsigned short slot4, unsigned short slot5) {
    u8 cl = (u8)kind;
    M.cmdSlots[0] = slot0;
    M.cmdSlots[1] = slot1;
    M.cmdSlots[2] = slot2;
    M.cmdSlots[3] = slot3;
    M.cmdSlots[4] = slot4;
    M.cmdSlots[5] = slot5;
    if (cl == 6 || cl == 3 || cl == 4 || cl == 7)
        M.cmdSlots[0] = arg2;
    dispatch(cl, far_ptr(arg2, arg3));
}

/* midi_task_scheduler_tick (1AF8h), from the timer every fourth tick. */
void midi_task_scheduler_tick(void) {
    unsigned si = 0, di = 0;
    if (M.g.lock != 0)
        return;
    if (M.g.dirty)
        recompute_voices();
    for (; si != 0x40; di += 4) {
        unsigned char *t = M.registry[si / 4];
        if (t == NULL)
            break;
        if (t[T_HOLD] == 0) {
            if (t[T_FADE_STEP] != 0) {
                fade_step(t, si);
                if (t[T_STATE] == 0xff)
                    continue; /* the next track moved up into this slot */
            }
            if (t[T_DIGITAL] != 0)
                check_stream(t);
            else
                track_tick(t, di);
            if (t[T_STATE] == 0xff)
                continue;
        }
        si += 4;
    }
    pump_streams();
    send_queued_volumes();
    drv(3, 0, 0);
}
