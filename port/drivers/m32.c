/*
 * DRIVERS/SX/M32.ASM in C: the Roland MT-32 driver, which sends MIDI through an MPU-401
 * at 330h in UART mode.
 *
 * The driver works on its own chunk: the bank's tables (copied in by slot 1) and a
 * shadow of what it last sent on each channel, at the offsets of the original (M32Image).
 * The bank maps a program to the MT-32 patch to select, a transpose, a volume offset and
 * a velocity curve; channel 9 is the rhythm part, whose notes go through the bank's
 * drum map instead. Volumes (controller 7) are scaled by the master volume, 0-15.
 *
 * The MPU-401 is reached through the emulated I/O ports, with the original's bounded
 * polling: the platform's MPU-401 is always ready, so the loops end at once.
 */
#include <stddef.h>
#include <string.h>

#include "dude.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "bak_game_end.h"

typedef uint8_t u8;

#pragma pack(push, 1)
typedef struct M32Image {
    unsigned char header[0x3a]; /* DUDE header: entry jump, magics, device name, version */
    u8 patch[128];              /* 03Ah  MT-32 patch per program; FFh: none (no sound) */
    u8 patchTranspose[128];     /* 0BAh  semitones, signed */
    u8 patchVolume[128];        /* 13Ah  added to controller 7, signed */
    u8 drumMap[128];            /* 1BAh  channel 9 note to rhythm key; FFh: dropped */
    u8 drumVolume;              /* 23Ah  the rhythm part's volume offset */
    u8 patchCurve[128];         /* 23Bh  velocity curve per program */
    u8 curve[4][128];           /* 2BBh  velocity curves */
    u8 pad[4];
    u8 masterVolume;     /* 4BFh  0-15 */
    u8 enabled;          /* 4C0h  0: volumes are not sent */
    u8 program[16];      /* 4C1h  per channel: the last program; FFh none */
    u8 bend[16][2];      /* 4D1h  the last bend sent: CL, CH; FFh none */
    u8 modulation[16];   /* 4F1h  never written: queries answer FFh */
    u8 volume[16];       /* 501h  controller 7 as received */
    u8 pan[16];          /* 511h */
    u8 sustain[16];      /* 521h  controller 64 */
    u8 played[16];       /* 531h  a note was sent since the last all-notes-off */
    u8 transpose[16];    /* 541h  of the channel's program */
    u8 volumeOffset[16]; /* 551h */
    u8 curveOf[16];      /* 561h */
    u8 patchOf[16];      /* 571h */
    u8 lastStatus;       /* 581h */
    u8 globalParam;      /* 582h  slot 11's value; nothing else reads it */
} M32Image;
#pragma pack(pop)

_Static_assert(offsetof(M32Image, masterVolume) == 0x4bf, "M32.ASM data layout");
_Static_assert(offsetof(M32Image, globalParam) == 0x582, "M32.ASM data layout");

#define MPU_DATA 0x330
#define MPU_STATUS 0x331
#define MPU_OUTPUT_BUSY 0x40
#define MPU_NO_INPUT 0x80

/* m32_mpu_send_cmd: wait for the MPU to accept, send the command, wait for its reply. */
static void mpu_command(u8 cmd) {
    u16 n = 0xffff;
    while (bak_port_in8(MPU_STATUS) & MPU_OUTPUT_BUSY)
        if (--n == 0)
            return;
    bak_port_out8(MPU_STATUS, cmd);
    for (n = 0xffff; bak_port_in8(MPU_STATUS) & MPU_NO_INPUT;)
        if (--n == 0)
            break;
    bak_port_in8(MPU_DATA); /* the acknowledge (FEh), read and dropped */
}

/* m32_mpu_send_data: up to 255 polls, draining any input byte that turns up. */
static void mpu_data(u8 byte) {
    s16 n = 0xff;
    for (;;) {
        u8 status = bak_port_in8(MPU_STATUS);
        if (!(status & MPU_OUTPUT_BUSY))
            break;
        n--;
        if (!(status & MPU_NO_INPUT))
            bak_port_in8(MPU_DATA);
        if (n < 1)
            return;
    }
    bak_port_out8(MPU_DATA, byte);
}

/* m32_mpu_delay: 7500h status reads, the MT-32's time to take in a message. */
static void mpu_delay(void) {
    for (u16 n = 0x7500; n != 0; n--)
        bak_port_in8(MPU_STATUS);
}

/* m32_send_midi: status AH | AL, then CH (except for C0h and D0h) and CL. */
static void send_midi(M32Image *d, u8 ah, u8 al, u8 ch, u8 cl) {
    u8 status = al | ah;
    d->lastStatus = status;
    mpu_data(status);
    if (ah != 0xc0 && ah != 0xd0)
        mpu_data(ch);
    mpu_data(cl);
}

/* A melodic note through the channel's transpose, wrapped back into 0-7Fh by octaves. */
static u8 transposed(const M32Image *d, unsigned c, u8 note) {
    u8 step = d->transpose[c] < 0x80 ? 0xf4 : 0x0c;
    note = (u8)(note + d->transpose[c]);
    while (note >= 0x80)
        note = (u8)(note + step);
    return note;
}

/* Slot 4. */
static void note_off(M32Image *d, u8 al, u8 ch) {
    if (al == 9) {
        ch = d->drumMap[ch & 0x7f];
        if (ch == 0xff)
            return;
    } else {
        unsigned c = al & 0x0f;
        if (d->patchOf[c] == 0xff)
            return;
        ch = transposed(d, c, ch);
    }
    send_midi(d, 0x90, al, ch, 0);
}

/* Slot 5: the velocity goes through the channel's curve, the drums' too. */
static void note_on(M32Image *d, u8 al, u8 ch, u8 cl) {
    unsigned c = al & 0x0f;
    if (al == 9) {
        ch = d->drumMap[ch & 0x7f];
        if (ch == 0xff)
            return;
    } else {
        if (d->patchOf[c] == 0xff)
            return;
        ch = transposed(d, c, ch);
    }
    /* curveOf * 128 + velocity, from the curves on: past the fourth curve the original
     * reads on through its own code, which the chunk holds too. */
    cl = d->curve[0][(u16)(0x80 * d->curveOf[c]) + (cl & 0x7f)];
    d->played[c] = 1;
    send_midi(d, 0x90, al, ch, cl);
}

/* Slot 7: CL comes back masked to 7 bits; for controller 7, the volume sent. */
static void control_change(M32Image *d, u8 al, u8 *pch, u8 *pcl) {
    unsigned c = al & 0x0f;
    u8 ch = *pch, cl = (u8)(*pcl & 0x7f);
    switch (ch) {
    case 0x07: {
        d->volume[c] = cl;
        if (d->enabled == 0)
            goto done;
        cl = (u8)(cl + d->volumeOffset[c]);
        if (cl >= 0x80)
            cl = d->volumeOffset[c] < 0x80 ? 0x7f : 1;
        u8 remainder;
        cl = bak_div8((u16)(d->masterVolume * cl), 0x0f, &remainder);
        if (cl == 0 && remainder != 0)
            cl = 1; /* not silent unless the product is */
        break;
    }
    case 0x0a:
        if (d->pan[c] == cl)
            goto done;
        d->pan[c] = cl;
        break;
    case 0x40:
        if (d->sustain[c] == cl)
            goto done;
        d->sustain[c] = cl;
        break;
    case 0x7b:
        if (d->played[c] == 0)
            goto done;
        d->played[c] = 0;
        break;
    default:
        goto done;
    }
    send_midi(d, 0xb0, al, ch, cl);
done:
    *pch = ch;
    *pcl = cl;
}

/* Slot 8: the bank's patch for the program; on a new transpose, or a channel that had
 * no patch, the notes are cut and the volume sent again. */
static void program_change(M32Image *d, u8 al, u8 cl) {
    if (al == 9)
        return;
    unsigned c = al & 0x0f;
    if (d->program[c] == cl)
        return;
    d->program[c] = cl;
    unsigned p = cl & 0x7f;
    u8 patch = d->patch[p], transpose = d->patchTranspose[p], volume = d->patchVolume[p];
    d->curveOf[c] = d->patchCurve[p];
    int reload = d->patchOf[c] == 0xff;
    d->patchOf[c] = patch;
    if (patch == 0xff) {
        send_midi(d, 0xb0, al, 0x7b, 0);
        send_midi(d, 0xb0, al, 0x40, 0);
        return;
    }
    if (transpose != d->transpose[c]) {
        d->transpose[c] = transpose;
        send_midi(d, 0xb0, al, 0x7b, 0);
        send_midi(d, 0xb0, al, 0x40, 0);
        reload = 1;
    }
    if (reload || volume != d->volumeOffset[c]) {
        d->volumeOffset[c] = volume;
        u8 ch = 0x07, vol = d->volume[c];
        control_change(d, al, &ch, &vol);
    }
    send_midi(d, 0xc0, al, transpose, patch);
}

/* Slot 10: sent only when it differs from the last bend on the channel. */
static void pitch_bend(M32Image *d, u8 al, u8 ch, u8 cl) {
    unsigned c = al & 0x0f;
    if (d->bend[c][0] == cl && d->bend[c][1] == ch)
        return;
    d->bend[c][0] = cl;
    d->bend[c][1] = ch;
    send_midi(d, 0xe0, al, ch, cl);
}

/* Slot 12: the volumes of channels 1-9 are sent again at the new level. */
static u16 set_master_volume(M32Image *d, u8 cl) {
    u16 old = d->masterVolume;
    if (cl == 0xff)
        return old;
    d->masterVolume = cl;
    if (d->enabled == 0)
        return old;
    for (u8 al = 1; al != 0x0a; al++) {
        u8 ch = 0x07, vol = d->volume[al];
        if (vol != 0xff)
            control_change(d, al, &ch, &vol);
    }
    return old;
}

/* Slot 13: off sends volume 0 on channels 1-9; on sends their volumes again. */
static u16 set_enabled(M32Image *d, u8 cl) {
    u16 old = d->enabled;
    if (cl == 0xff)
        return old;
    if (cl == 0) {
        d->enabled = 0;
        for (u8 al = 1; al != 0x0a; al++)
            send_midi(d, 0xb0, al, 0x07, 0);
    } else {
        d->enabled = 1;
        set_master_volume(d, d->masterVolume);
    }
    return old;
}

/* Slot 17: AH = status, AL = channel, CH = controller; FFFFh for anything else. */
static u16 query(const M32Image *d, u16 ax, u8 ch) {
    unsigned c = ax & 0x0f;
    u8 ah = (u8)(ax >> 8);
    switch (ah) {
    case 0xe0: {
        u16 bend = (u16)(d->bend[c][0] | d->bend[c][1] << 8);
        if (bend == 0xffff)
            return bend;
        u8 lo = (u8)(bend >> 8), hi = (u8)bend;
        if (hi & 1)
            lo |= 0x80;
        return (u16)((hi >> 1) << 8 | lo);
    }
    case 0xc0:
        return (u16)(ax & 0xff00) | d->program[c];
    case 0xb0:
        switch (ch) {
        case 0x01:
            return (u16)(ax & 0xff00) | d->modulation[c];
        case 0x07:
            return (u16)(ax & 0xff00) | d->volume[c];
        case 0x0a:
            return (u16)(ax & 0xff00) | d->pan[c];
        case 0x40:
            return (u16)(ax & 0xff00) | d->sustain[c];
        }
        return 0xffff;
    default:
        return 0xffff;
    }
}

/* Slot 1: reset the MPU and put it in UART mode, take the bank's tables, send its SysEx
 * messages (a word count, then the bytes) with a pause after each, master volume 12. */
static void load_bank(M32Image *d, const unsigned char *bank, DudeRegs *r) {
    mpu_command(0xff);
    mpu_command(0x3f);
    mpu_delay();
    mpu_command(0x3f);
    mpu_delay();
    mpu_command(0x3f);
    mpu_delay();
    mpu_delay();
    memcpy(d->patch, bank, 0x481);
    d->volumeOffset[9] = d->drumVolume;
    u16 n = (u16)(bank[0x481] | bank[0x482] << 8);
    for (const unsigned char *p = bank + 0x483; n != 0; n--) {
        u8 b = *p++;
        mpu_data(b);
        if (b == 0xf7) {
            mpu_delay();
            mpu_delay();
        }
    }
    set_master_volume(d, 0x0c);
    r->ax = 0x0961;
    r->cx = 0x0801; /* voices 1-8 */
}

void m32_dispatch(unsigned char *chunk, uint16_t slot, DudeRegs *r) {
    M32Image *d = (M32Image *)chunk;
    u8 al = (u8)r->ax, ch = (u8)(r->cx >> 8), cl = (u8)r->cx;
    switch (slot) {
    case 0:
        r->ax = 0x0101;
        r->cx = 0x0c20; /* device 0Ch, 32 partials */
        break;
    case 1:
        load_bank(d, r->bank, r);
        break;
    case 2:
        mpu_command(0xff);
        break;
    case 4:
        note_off(d, al, ch);
        break;
    case 5:
        note_on(d, al, ch, cl);
        break;
    case 7:
        control_change(d, al, &ch, &cl);
        r->cx = (u16)(ch << 8 | cl);
        break;
    case 8:
        program_change(d, al, cl);
        break;
    case 10:
        pitch_bend(d, al, ch, cl);
        break;
    case 11:
        r->ax = d->globalParam;
        if (cl != 0xff)
            d->globalParam = cl;
        break;
    case 12:
        r->ax = set_master_volume(d, cl);
        break;
    case 13:
        r->ax = set_enabled(d, cl);
        break;
    case 17:
        r->ax = query(d, r->ax, ch);
        break;
    default: /* 3, 6, 9, 14-16: a bare RET */
        break;
    }
}
