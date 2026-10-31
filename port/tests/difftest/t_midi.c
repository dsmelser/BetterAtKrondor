/*
 * AUDIO/DRIVER/MIDI.ASM and SNDDRV.ASM: the sequencer (port/asm/midi.c) driving fake
 * synth and digital drivers that log every call and answer as test_midi.py's do.
 *
 * Memory comes from the emulated DOS allocator, so far-pointer arithmetic on it (FP_SEG,
 * FP_OFF) works as on the original's side. Addresses travel as u32.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "difftest.h"
#include "../../drivers/dude.h"
#include "../../asm/midiseq.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/AUDIO/DRIVER/MIDI.H"
#include "SRC/AUDIO/DRIVER/SNDDRV.H"
#include "SRC/AUDIO/DRIVER/MUSDISP.H"
#include "SRC/AUDIO/DRIVER/DISP35E0.H"
#include "SRC/AUDIO/RES/AUDRESIN.H"
#include "bak_game_end.h"

/* ---- the fake drivers ------------------------------------------------------------- */

static uint8_t s_log[1 << 18];
static size_t s_logSize;
static uint16_t s_infoAx, s_infoCx, s_bankAx, s_bankCx;
static uint16_t s_digitalReplies[256];
static unsigned s_digitalReplyCount, s_digitalReplyNext;

static void log_bytes(const void *p, size_t n) {
    if (s_logSize + n > sizeof s_log)
        dt_fail("driver log overflow");
    memcpy(s_log + s_logSize, p, n);
    s_logSize += n;
}

/* Synth log entry: 'S', u16 slot, u16 ax, u16 cx, u32 bank. Answers: slot 0 and 1 as
 * set by midi.answers, slot 7 masks CL to 7 bits, slots 11-13 return slot * 111h + CL. */
static void fake_synth(unsigned char *chunk, uint16_t slot, DudeRegs *r) {
    (void)chunk;
    uint32_t bank = (uint32_t)(uintptr_t)r->bank;
    log_bytes("S", 1);
    log_bytes(&slot, 2);
    log_bytes(&r->ax, 2);
    log_bytes(&r->cx, 2);
    log_bytes(&bank, 4);
    switch (slot) {
    case 0:
        r->ax = s_infoAx;
        r->cx = s_infoCx;
        break;
    case 1:
        r->ax = s_bankAx;
        r->cx = s_bankCx;
        break;
    case 7:
        r->cx &= 0xff7f;
        break;
    case 11:
    case 12:
    case 13:
        r->ax = (uint16_t)(slot * 0x111 + (r->cx & 0xff));
        break;
    }
}

/* Digital log entry: 'D', u16 cmd, u32 args, then the block (cmd 0: 4 bytes, 3: 10, 4: 2).
 * Command 4 answers from the scripted replies (then 0). */
static uint16_t fake_digital(unsigned char *chunk, uint16_t cmd, void *args) {
    (void)chunk;
    uint32_t at = (uint32_t)(uintptr_t)args;
    log_bytes("D", 1);
    log_bytes(&cmd, 2);
    log_bytes(&at, 4);
    if (cmd == 0)
        log_bytes(args, 4);
    else if (cmd == 3)
        log_bytes(args, 10);
    else if (cmd == 4)
        log_bytes(args, 2);
    if (cmd != 4)
        return (uint16_t)(0x4400 | cmd);
    return s_digitalReplyNext < s_digitalReplyCount ? s_digitalReplies[s_digitalReplyNext++] : 0;
}

/* ---- commands ------------------------------------------------------------------------ */

/* midi.setup u16 infoAx, infoCx, bankAx, bankCx, u16 n, n x u16 digital replies */
static void cmd_setup(DtIn *in, DtOut *out) {
    (void)out;
    s_infoAx = dt_u16(in);
    s_infoCx = dt_u16(in);
    s_bankAx = dt_u16(in);
    s_bankCx = dt_u16(in);
    s_digitalReplyCount = dt_u16(in);
    if (s_digitalReplyCount > 256)
        dt_fail("too many replies");
    for (unsigned i = 0; i < s_digitalReplyCount; i++)
        s_digitalReplies[i] = dt_u16(in);
    s_digitalReplyNext = 0;
    g_dudeTestSynth = fake_synth;
    g_dudeTestDigital = fake_digital;
    s_logSize = 0;
}

/* midi.alloc u32 size -> u32 address of a zeroed DOS block */
static void cmd_alloc(DtIn *in, DtOut *out) {
    void *p = bak_dos_alloc(dt_u32(in), 1);
    if (p == NULL)
        dt_fail("out of DOS memory");
    dt_put_u32(out, (uint32_t)(uintptr_t)p);
}

/* midi.write u32 address, bytes */
static void cmd_write(DtIn *in, DtOut *out) {
    (void)out;
    unsigned char *p = (unsigned char *)(uintptr_t)dt_u32(in);
    size_t n = dt_remaining(in);
    memcpy(p, dt_bytes(in, n), n);
}

/* midi.read u32 address, u32 size -> bytes */
static void cmd_read(DtIn *in, DtOut *out) {
    const unsigned char *p = (const unsigned char *)(uintptr_t)dt_u32(in);
    dt_put_bytes(out, p, dt_u32(in));
}

/* midi.log -> the driver calls since the last read */
static void cmd_log(DtIn *in, DtOut *out) {
    (void)in;
    dt_put_bytes(out, s_log, s_logSize);
    s_logSize = 0;
}

/* midi.op u8 op, u32 pointer, u16 a, b, c -> u16 result */
static void cmd_op(DtIn *in, DtOut *out) {
    uint8_t op = dt_u8(in);
    void *p = (void *)(uintptr_t)dt_u32(in);
    s16 a = (s16)dt_u16(in), b = (s16)dt_u16(in), c = (s16)dt_u16(in);
    uint32_t bits = (uint32_t)(uintptr_t)p;
    uint16_t result = 0;
    switch (op) {
    case 0:
        result = midi_driver_install_dispatch((u16)bits, (u16)(bits >> 16));
        break;
    case 1:
        result = midi_cmd1_save_cx_303b4_thunk(p);
        break;
    case 2:
        midi_track_init_thunk(p, a);
        break;
    case 3:
        midi_active_release_thunk(p);
        break;
    case 4:
        midi_event_callback_thunk_0a8b(p, a);
        break;
    case 5:
        midi_fade_queue_handle(p, a, b, c);
        break;
    case 6:
        midi_track_apply_intensity(p, a);
        break;
    case 7:
        midi_chord_event_thunk_0b6e(p, a);
        break;
    case 8:
        midi_track_set_byte_15a(p, a);
        break;
    case 9:
        midi_track_set_word_bc_far(p, a, b);
        break;
    case 10:
        midi_apply_ctrl_change_thunk(p, a, b, c);
        break;
    case 11:
        midi_set_note_velocity_thunk(a);
        break;
    case 12:
        midi_cmd6_far_wrapper((u16)a);
        break;
    case 13:
        midi_cmd4_far_wrapper(a);
        break;
    case 14:
        midi_init_thunk_0678();
        break;
    case 15:
        midi_pitch_bend_step_far_thunk(p, a);
        break;
    case 16:
        midi_seek_volume_preserve_thunk(p);
        break;
    case 17:
        result = midi_stub_return_cx_via_nop_alt(p);
        break;
    case 18:
        result = (uint16_t)midi_stub_return_cx_via_nop(p);
        break;
    case 19:
        result = midi_track_read_byte_158_far(p);
        break;
    case 20:
        midi_task_scheduler_tick();
        break;
    case 21:
        snddrv_patch_farptr((u16)bits, (u16)(bits >> 16));
        break;
    default:
        dt_fail("unknown midi.op %u", op);
    }
    dt_put_u16(out, result);
}

/* midi.queue u8 kind, u16 arg2, arg3, slot0..slot5 */
static void cmd_queue(DtIn *in, DtOut *out) {
    (void)out;
    char kind = (char)dt_u8(in);
    u16 w[8];
    for (int i = 0; i < 8; i++)
        w[i] = dt_u16(in);
    midi_queue_command(kind, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
}

const DtEntry dt_midi_entries[] = {
    DT_COMMAND("midi.setup", cmd_setup), DT_COMMAND("midi.alloc", cmd_alloc),
    DT_COMMAND("midi.write", cmd_write), DT_COMMAND("midi.read", cmd_read),
    DT_COMMAND("midi.log", cmd_log),     DT_COMMAND("midi.op", cmd_op),
    DT_COMMAND("midi.queue", cmd_queue), DT_GLOBAL(g_midiData),
    DT_GLOBAL(g_bMusicDriverInstalled),  DT_END,
};

/* ---- MUSDISP.ASM and DISP35E0.ASM ---------------------------------------------------- */

/* snddrv.op u8 op, u32 chunk, u16 a, b -> u16 result (the fake digital driver logs) */
static void cmd_snddrv_op(DtIn *in, DtOut *out) {
    uint8_t op = dt_u8(in);
    g_pMusicDriver = (void *)(uintptr_t)dt_u32(in);
    s16 a = (s16)dt_u16(in), b = (s16)dt_u16(in);
    uint16_t result = 0;
    switch (op) {
    case 0:
        result = (uint16_t)audio_driver_disp_seg1000_alt(a, b);
        break;
    case 1:
        audio_music_tick();
        break;
    case 2:
        audio_driver_disp_seg1000_far();
        break;
    case 3:
        dispatch_via_35e0_cmd0d_seg1000();
        break;
    case 4:
        FUN_1000_13d7();
        break;
    case 5:
        FUN_1000_13e9();
        break;
    case 6:
        FUN_1000_13f0();
        break;
    case 7:
        FUN_1000_13f7();
        break;
    case 8:
        FUN_1000_1405();
        break;
    default:
        dt_fail("unknown snddrv.op %u", op);
    }
    dt_put_u16(out, result);
}

const DtEntry dt_snddrv_entries[] = {
    DT_COMMAND("snddrv.op", cmd_snddrv_op),
    DT_END,
};
