/* AUDIO/CDAUDIO.ASM: the 1.02 CD audio (port/asm/cdaudio.c) over the emulated MSCDEX. */
#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/AUDIO/CDAUDIO.H"
#include "bak_game_end.h"

extern unsigned char g_cdaudio_state[70];

/* cd.attach s8 drive (-1: MSCDEX absent), u8 first, u8 last, (min sec frame) per track */
static void cmd_attach(DtIn *in, DtOut *out) {
    (void)out;
    int drive = (signed char)dt_u8(in);
    int first = dt_u8(in), last = dt_u8(in);
    const unsigned char (*starts)[3] = NULL;
    if (drive >= 0)
        starts = (const unsigned char (*)[3])dt_bytes(in, (size_t)(last - first + 1) * 3);
    bak_cdrom_attach(drive, first, last, starts);
}

static void cmd_detect(DtIn *in, DtOut *out) {
    dt_put_u16(out, (u16)v102_cddrive_detect((char)dt_u8(in)));
}

/* cd.set_track s16 track, s32 bias, s32 offset */
static void cmd_set_track(DtIn *in, DtOut *out) {
    (void)out;
    s16 track = (s16)dt_u16(in);
    s32 bias = (s32)dt_u32(in), offset = (s32)dt_u32(in);
    v102_cdaudio_set_track_msf(track, bias, offset);
}

static void cmd_play(DtIn *in, DtOut *out) {
    (void)in;
    (void)out;
    v102_cdaudio_play();
}

static void cmd_stop(DtIn *in, DtOut *out) {
    (void)in;
    (void)out;
    v102_cdaudio_stop();
}

const DtEntry dt_cdaudio_entries[] = {
    DT_COMMAND("cd.attach", cmd_attach),
    DT_COMMAND("cd.detect", cmd_detect),
    DT_COMMAND("cd.set_track", cmd_set_track),
    DT_COMMAND("cd.play", cmd_play),
    DT_COMMAND("cd.stop", cmd_stop),
    DT_GLOBAL(g_cdaudio_state),
    DT_END,
};
