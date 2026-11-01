/*
 * DRIVERS/SX/ADL.ASM in C: the AdLib driver, which plays the sequencer's MIDI on the OPL2 of
 * an AdLib card at 388h (port/platform/opl.c).
 *
 * The driver keeps nine voices, each an OPL2 channel of two operators, and gives them to MIDI
 * channels as notes come: an idle voice that already holds the program, then any idle voice,
 * then one taken from the channel furthest over its reserved voices (controller 75 reserves
 * voices for a channel; what cannot be reserved now is owed and paid when voices come free).
 * Voices are reused oldest first. Channel 9 is the rhythm channel: its notes 1Bh-58h play
 * programs 80h-BDh at the pitch the bank gives each.
 *
 * The bank (slot 1) is 5383 bytes: 190 patches of 28 bytes (programs 0-127, then the rhythm
 * notes' programs), the rhythm notes' 62 pitches and a byte. The driver's buffer holds 5320, so
 * the pitches land on the table after it, which ADL.ASM takes for a constant velocity table, and
 * the last byte on the patch staging buffer, which the reset that follows overwrites. A patch is
 * the modulator's 13 bytes, the carrier's 13 (key scale level, multiplier, feedback, attack,
 * sustain level, sustaining, decay, release, total level, tremolo, vibrato, key scale rate,
 * frequency modulation: 0 for additive) and the two waveforms.
 *
 * The routines are translated register for register (Cpu): what a slot leaves in AX and CX is
 * what the original leaves there, the values its callees leave behind included. The reset's
 * pushes and pops do not match, so slot 2 returns the caller's BX in CX. Every OPL write goes
 * through the emulated ports in the original's order; the original's delay reads of 388h are
 * left out. The driver works on its own chunk, at the offsets of the original (AdlImage), and
 * indexes it as the original does: a channel past 15 reaches into the tables that follow.
 */
#include <stddef.h>
#include <string.h>

#include "dude.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "bak_game_end.h"

typedef uint8_t u8;

#pragma pack(push, 1)
typedef struct AdlImage {
    unsigned char header[0x37]; /* DUDE header: entry jump, magics, device name, version */
    u16 portRegister;           /* 037h  388h */
    u16 portStatus;             /* 039h  388h, read only to wait */
    u16 portData;               /* 03Bh  389h */
    u16 fnum[48];               /* 03Dh  F-numbers, four a semitone, for an octave */
    u8 velocityCurve[64];       /* 09Dh  a note's velocity to a level */
    u8 levelCurve[64];          /* 0DDh  a level to an operator's loudness */
    u8 globalParam;             /* 11Dh  slot 11's value; nothing else reads it */
    u8 masterEnable;            /* 11Eh  0: notes load no patch and sound at level 0 */
    u8 masterVolume;            /* 11Fh  0-15 */
    u8 chProgram[16];           /* 120h */
    u8 chVolume[16];            /* 130h  controller 7, halved */
    u8 chPan[16];               /* 140h  controller 10; nothing reads it */
    u8 chSustain[16];           /* 150h  controller 64 */
    u8 chOwed[16];              /* 160h  voices reserved by controller 75 but not yet given */
    u16 chBend[16];             /* 170h  2000h: none */
    u8 voiceChannel[11];        /* 190h  FFh: none */
    u8 voiceNote[11];           /* 19Bh  FFh: idle */
    u8 voiceVelocity[11];       /* 1A6h  halved */
    u8 voiceHeld[11];           /* 1B1h  released while the sustain pedal is down */
    u8 voiceProgram[11];        /* 1BCh  the patch the voice holds; FFh none */
    u8 voiceAge[11];            /* 1C7h  the voices, oldest first */
    u8 voiceReserved[11];       /* 1D2h  the channel that reserved the voice; FFh none */
    u8 chReserved[16];          /* 1DDh  voices reserved for the channel */
    u8 chActive[16];            /* 1EDh  its notes sounding */
    u8 pad1fd;
    u8 levelOp[18];     /* 1FEh  a voice's carrier, then (+9) its modulator: the operators whose
                         loudness the volume sets */
    u8 opIsCarrier[18]; /* 210h */
    u8 opRegister[18];  /* 222h  an operator's offset in the 20h-F5h register groups */
    u8 opVoice[18];     /* 234h  its channel */
    u8 voiceOps[18];    /* 246h  a voice's modulator and carrier */
    u8 defaultModulator[14]; /* 258h  the reset's operator patches */
    u8 defaultCarrier[14];   /* 266h */
    u8 opPatch[18][14];      /* 274h  each operator's patch as last loaded, its waveform last */
    u8 pad370[2];            /* 370h  00h, B8h: a MOV AX,imm16 never run */
    u16 bankSize;            /* 372h  1507h: the bytes slot 1 copies */
    u8 bank[5320];           /* 374h  190 patches of 28 bytes */
    u8 drumPitch[62];        /* 183Ch the rhythm notes' pitches, the bank's next bytes */
    u8 patchStage[13];       /* 187Ah */
    u8 pad1887;
    u8 noteSelect;    /* 1888h register 08h's bit 6 */
    u8 tremoloDepth;  /* 1889h register BDh's bit 7 */
    u16 vibratoDepth; /* 188Ah its bit 6 (the low byte is tested) */
    u8 rhythm;        /* 188Ch its bits 5-0 */
    u16 waveSelect;   /* 188Dh register 01h; waveforms are written once it is set */
    u8 pad188f[3];
    /* What a voice's patch gives the volume: its operators' key scale levels and the range of
     * their total level. ADL.ASM's labels put the carrier's under "mod" and the other way
     * round; the steps are computed and never read. */
    u16 carrierKsl[11];     /* 1892h */
    u16 carrierRange[11];   /* 18A8h 3Fh - total level */
    u16 carrierStep[11];    /* 18BEh range / 15 */
    u16 modulatorKsl[11];   /* 18D4h */
    u16 modulatorRange[11]; /* 18EAh */
    u8 voiceAdditive[11];   /* 1900h the modulator sounds too */
    u16 modulatorStep[11];  /* 190Bh */
    u16 functions[18];      /* 1921h the slots' near addresses */
} AdlImage;
#pragma pack(pop)

_Static_assert(offsetof(AdlImage, fnum) == 0x3d, "ADL.ASM data layout");
_Static_assert(offsetof(AdlImage, globalParam) == 0x11d, "ADL.ASM data layout");
_Static_assert(offsetof(AdlImage, chBend) == 0x170, "ADL.ASM data layout");
_Static_assert(offsetof(AdlImage, chReserved) == 0x1dd, "ADL.ASM data layout");
_Static_assert(offsetof(AdlImage, levelOp) == 0x1fe, "ADL.ASM data layout");
_Static_assert(offsetof(AdlImage, opPatch) == 0x274, "ADL.ASM data layout");
_Static_assert(offsetof(AdlImage, bank) == 0x374, "ADL.ASM data layout");
_Static_assert(offsetof(AdlImage, drumPitch) == 0x183c, "ADL.ASM data layout");
_Static_assert(offsetof(AdlImage, noteSelect) == 0x1888, "ADL.ASM data layout");
_Static_assert(offsetof(AdlImage, carrierKsl) == 0x1892, "ADL.ASM data layout");
_Static_assert(offsetof(AdlImage, voiceAdditive) == 0x1900, "ADL.ASM data layout");
_Static_assert(offsetof(AdlImage, functions) == 0x1921, "ADL.ASM data layout");

#define OFF(field) ((u16)offsetof(AdlImage, field))

/* The registers the routines pass values in, and leave them in. */
typedef struct Cpu {
    u16 ax, bx, cx, dx, si, di;
} Cpu;

#define LO(r) ((u8)(r))
#define HI(r) ((u8)((r) >> 8))
#define SET_LO(r, v) ((r) = (u16)(((r) & 0xff00) | (u8)(v)))
#define SET_HI(r, v) ((r) = (u16)(((r) & 0x00ff) | (u16)(u8)(v) << 8))

static unsigned char *s_d; /* the chunk of the call: CS */

/* CS:[off], wrapping as a 16-bit offset does. */
static u8 *at(u16 off) {
    return s_d + off;
}

static u16 word(u16 off) {
    return (u16)(s_d[off] | s_d[(u16)(off + 1)] << 8);
}

static void set_word(u16 off, u16 v) {
    s_d[off] = (u8)v;
    s_d[(u16)(off + 1)] = (u8)(v >> 8);
}

/* AX = AL * src (MUL r/m8). */
static void mul8(Cpu *r, u8 src) {
    r->ax = (u16)(LO(r->ax) * src);
}

/* AL = AX / divisor, AH = the remainder (DIV r/m8), as the game's divide-error handler resolves
 * a quotient that overflows. */
static void div8(Cpu *r, u8 divisor) {
    u8 rem;
    u8 q = bak_div8(r->ax, divisor, &rem);
    r->ax = (u16)(rem << 8 | q);
}

/* AX = DX:AX / CX, DX = the remainder (DIV r/m16); the callers' quotients fit. */
static void div16(Cpu *r) {
    uint32_t n = (uint32_t)r->dx << 16 | r->ax;
    r->ax = (u16)(n / r->cx);
    r->dx = (u16)(n % r->cx);
}

/* adl_opl_write (20C0h): register BL, value CL, through the chunk's ports. */
static void opl_write(const Cpu *r) {
    bak_port_out8(word(OFF(portRegister)), LO(r->bx));
    bak_port_out8(word(OFF(portData)), LO(r->cx));
}

/* An operator's patch: op * 14, computed in 16 bits. */
static u16 op_patch_offset(u16 op) {
    return (u16)(op * 14);
}

static void voice_release(Cpu *r);
static void voice_render(Cpu *r);
static void op_patch_load(Cpu *r);

/* ---- the operators' registers (from their patches) -------------------------------------- */

/* adl_opl_write_rhythm (219Dh): register BDh. */
static void write_rhythm(Cpu *r) {
    u16 bx = r->bx, cx = r->cx;
    r->cx = 0;
    if (*at(OFF(tremoloDepth)) != 0)
        r->cx |= 0x80;
    if (*at(OFF(vibratoDepth)) != 0)
        r->cx |= 0x40;
    SET_LO(r->cx, LO(r->cx) | *at(OFF(rhythm)));
    r->bx = 0xbd;
    opl_write(r);
    r->bx = bx;
    r->cx = cx;
}

/* adl_opl_write_notesel (21C6h): register 08h. */
static void write_note_select(Cpu *r) {
    u16 bx = r->bx, cx = r->cx;
    r->cx = *at(OFF(noteSelect)) != 0 ? 0x40 : 0;
    r->bx = 8;
    opl_write(r);
    r->bx = bx;
    r->cx = cx;
}

/* adl_opl_write_op_level (21DEh): 40h + operator BX, key scale level and total level. */
static void write_level(Cpu *r) {
    u16 ax = r->ax, bx = r->bx, cx = r->cx, di = r->di;
    r->di = op_patch_offset(r->bx);
    r->ax = (u8)(*at((u16)(OFF(opPatch) + r->di)) << 6);
    r->cx = r->ax;
    SET_LO(r->ax, *at((u16)(OFF(opPatch) + r->di + 8)) & 0x3f);
    SET_LO(r->cx, LO(r->cx) | LO(r->ax));
    SET_HI(r->bx, 0);
    r->di = r->bx;
    SET_LO(r->bx, *at((u16)(OFF(opRegister) + r->di)) + 0x40);
    opl_write(r);
    r->ax = ax;
    r->bx = bx;
    r->cx = cx;
    r->di = di;
}

/* adl_opl_write_voice_feedback (2226h): C0h + the operator's channel, feedback and connection,
 * for a modulator only. Leaves AX = the feedback byte * 2. */
static void write_feedback(Cpu *r) {
    u16 bx = r->bx, cx = r->cx, di = r->di;
    r->di = r->bx;
    if (*at((u16)(OFF(opIsCarrier) + r->di)) == 0) {
        r->di = op_patch_offset(r->di);
        r->ax = (u16)(*at((u16)(OFF(opPatch) + r->di + 2)) << 1);
        r->cx = r->ax;
        if (*at((u16)(OFF(opPatch) + r->di + 12)) == 0)
            SET_LO(r->cx, LO(r->cx) + 1);
        r->di = r->bx;
        SET_HI(r->bx, 0);
        SET_LO(r->bx, *at((u16)(OFF(opVoice) + r->di)) + 0xc0);
        SET_LO(r->cx, LO(r->cx) & 0x0f);
        opl_write(r);
    }
    r->bx = bx;
    r->cx = cx;
    r->di = di;
}

/* adl_opl_write_op_ar_dr (2276h) and adl_opl_write_op_sl_rr (22BCh): 60h or 80h + operator BX,
 * from the patch's bytes hi (the high nibble) and lo. Leave AX = byte lo & 0Fh. */
static void write_nibbles(Cpu *r, unsigned hi, unsigned lo, u8 group) {
    u16 bx = r->bx, cx = r->cx, di = r->di;
    r->di = op_patch_offset(r->bx);
    r->ax = (u8)(*at((u16)(OFF(opPatch) + r->di + hi)) << 4);
    r->cx = r->ax;
    SET_LO(r->ax, *at((u16)(OFF(opPatch) + r->di + lo)) & 0x0f);
    SET_LO(r->cx, LO(r->cx) | LO(r->ax));
    r->di = r->bx;
    SET_HI(r->bx, 0);
    SET_LO(r->bx, *at((u16)(OFF(opRegister) + r->di)) + group);
    opl_write(r);
    r->bx = bx;
    r->cx = cx;
    r->di = di;
}

/* adl_opl_write_op_flags_mult (2302h): 20h + operator BX, tremolo, vibrato, sustaining, key
 * scale rate and multiplier. Leaves AX = the multiplier & 0Fh. */
static void write_flags(Cpu *r) {
    u16 cx = r->cx, di = r->di;
    r->di = op_patch_offset(r->bx);
    const u8 *p = at((u16)(OFF(opPatch) + r->di));
    r->cx = 0;
    if (p[9] != 0)
        r->cx |= 0x80;
    if (p[10] != 0)
        r->cx |= 0x40;
    if (p[5] != 0)
        r->cx |= 0x20;
    if (p[11] != 0)
        r->cx |= 0x10;
    r->ax = p[1] & 0x0f;
    SET_LO(r->cx, LO(r->cx) | LO(r->ax));
    u16 bx = r->bx;
    r->di = r->bx;
    SET_HI(r->bx, 0);
    SET_LO(r->bx, *at((u16)(OFF(opRegister) + r->di)) + 0x20);
    opl_write(r);
    r->bx = bx;
    r->cx = cx;
    r->di = di;
}

/* adl_opl_write_op_waveform (2370h): E0h + operator BX, once register 01h enables waveforms. */
static void write_waveform(Cpu *r) {
    u16 bx = r->bx, cx = r->cx, di = r->di;
    if (word(OFF(waveSelect)) != 0) {
        SET_HI(r->bx, 0);
        r->di = op_patch_offset(r->bx);
        r->cx = *at((u16)(OFF(opPatch) + r->di + 13));
        r->di = r->bx;
        SET_LO(r->bx, *at((u16)(OFF(opRegister) + r->di)) + 0xe0);
        opl_write(r);
    }
    r->bx = bx;
    r->cx = cx;
    r->di = di;
}

/* adl_op_apply_all_regs (2184h): every register operator BX's patch reaches. */
static void op_write_all(Cpu *r) {
    write_rhythm(r);
    write_note_select(r);
    write_level(r);
    write_feedback(r);
    write_nibbles(r, 3, 6, 0x60); /* attack, decay */
    write_nibbles(r, 4, 7, 0x80); /* sustain level, release */
    write_flags(r);
    write_waveform(r);
}

/* adl_op_patch_load (213Bh): operator BX takes the 13 bytes at CS:CX and the waveform DL & 3. */
static void op_patch_load(Cpu *r) {
    u16 cx = r->cx, dx = r->dx, si = r->si, di = r->di;
    r->si = op_patch_offset(r->bx);
    u16 waveform = r->dx;
    for (r->di = 0; r->di != 0x0d; r->di++) {
        SET_LO(r->dx, *at(r->cx));
        *at((u16)(OFF(opPatch) + r->si)) = LO(r->dx);
        r->si++;
        r->cx++;
    }
    r->dx = waveform;
    SET_LO(r->dx, LO(r->dx) & 3);
    *at((u16)(OFF(opPatch) + r->si)) = LO(r->dx);
    u16 bx = r->bx;
    op_write_all(r);
    r->bx = bx;
    r->cx = cx;
    r->dx = dx;
    r->si = si;
    r->di = di;
}

/* adl_op_patch_stage_load (2114h): the 13 bytes at CS:CX through the staging buffer. */
static void op_patch_stage_load(Cpu *r) {
    u16 bx = r->bx, cx = r->cx, dx = r->dx, di = r->di, si = r->si;
    r->si = r->cx;
    SET_HI(r->cx, 0);
    for (r->di = 0; r->di != 0x0d; r->di++, r->si++) {
        SET_LO(r->cx, *at(r->si));
        *at((u16)(OFF(patchStage) + r->di)) = LO(r->cx);
    }
    r->si = si;
    r->cx = OFF(patchStage);
    op_patch_load(r);
    r->bx = bx;
    r->cx = cx;
    r->dx = dx;
    r->di = di;
}

/* adl_opl_init_all_ops (20E7h): the default patches into every operator. */
static void op_reset_all(Cpu *r) {
    u16 bx = r->bx, cx = r->cx, dx = r->dx, di = r->di;
    for (r->di = 0; r->di != 0x12; r->di++) {
        r->bx = r->di;
        r->dx = 0;
        r->cx =
            *at((u16)(OFF(opIsCarrier) + r->di)) == 0 ? OFF(defaultModulator) : OFF(defaultCarrier);
        op_patch_stage_load(r);
    }
    r->bx = bx;
    r->cx = cx;
    r->dx = dx;
    r->di = di;
}

/* adl_silence_and_reset (23AFh): every register to 0, waveforms enabled, the default patches.
 * It pushes BX then CX and pops BX then CX: they come back swapped. */
static void silence_and_reset(Cpu *r) {
    u16 bx = r->bx, cx = r->cx;
    r->bx = 0;
    r->cx = 0;
    for (; r->bx < 0xf6; r->bx++)
        opl_write(r);
    set_word(OFF(waveSelect), 0x20);
    r->bx = 1;
    r->cx = word(OFF(waveSelect));
    opl_write(r);
    op_reset_all(r);
    r->bx = cx;
    r->cx = bx;
}

/* ---- voices ------------------------------------------------------------------------------- */

/* adl_voice_apply_patch (2013h): voice BX takes the patch at CS:CX. */
static void voice_apply_patch(Cpu *r) {
    Cpu saved = *r;
    r->di = r->cx;
    u16 bx = r->bx, cx = r->cx, dx = r->dx;
    *at((u16)(OFF(voiceAdditive) + r->bx)) = 1;
    if (*at((u16)(r->di + 0x0c)) == 0) {
        r->bx <<= 1;
        r->ax = *at(r->di);
        set_word((u16)(OFF(modulatorKsl) + r->bx), r->ax);
        r->ax = (u8)(0x3f - *at((u16)(r->di + 8)));
        set_word((u16)(OFF(modulatorRange) + r->bx), r->ax);
        r->dx = 0;
        r->cx = 0x0f;
        div16(r);
        set_word((u16)(OFF(modulatorStep) + r->bx), r->ax);
    } else {
        *at((u16)(OFF(voiceAdditive) + r->bx)) = 0;
        r->bx <<= 1;
    }
    r->ax = *at((u16)(r->di + 0x0d));
    set_word((u16)(OFF(carrierKsl) + r->bx), r->ax);
    r->ax = (u8)(0x3f - *at((u16)(r->di + 0x15)));
    set_word((u16)(OFF(carrierRange) + r->bx), r->ax);
    r->dx = 0;
    r->cx = 0x0f;
    div16(r);
    set_word((u16)(OFF(carrierStep) + r->bx), r->ax);
    r->dx = dx;
    r->cx = cx;
    r->bx = bx;
    r->di += 0x1a;
    SET_LO(r->ax, *at(r->di));
    r->di++;
    r->dx = *at(r->di);
    r->si = (u16)(r->cx + 0x0d);
    r->bx <<= 1;
    r->di = r->bx;
    r->bx = *at((u16)(OFF(voiceOps) + r->di));
    u16 carrierWave = r->dx;
    r->dx = r->ax;
    op_patch_load(r); /* the modulator: the patch's first 13 bytes, waveform byte 1Ah */
    r->dx = carrierWave;
    r->di++;
    r->bx = *at((u16)(OFF(voiceOps) + r->di));
    r->cx = r->si;
    op_patch_load(r); /* the carrier: the next 13, waveform byte 1Bh */
    *r = saved;
}

/* adl_voice_render_volume (1F77h): voice BX's loudness from level AL: the carrier's, and the
 * modulator's when it sounds too. */
static void voice_render_level(Cpu *r) {
    u16 ax = r->ax, cx = r->cx, dx = r->dx, di = r->di;
    r->ax = LO(r->ax);
    r->di = r->ax;
    SET_LO(r->ax, *at((u16)(OFF(levelCurve) + r->di)));
    u16 level = r->ax;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            r->ax = level;
            if (*at((u16)(OFF(voiceAdditive) + r->bx)) == 0)
                break;
        }
        r->bx <<= 1;
        r->cx = word((u16)((pass == 0 ? OFF(carrierRange) : OFF(modulatorRange)) + r->bx));
        mul8(r, LO(r->cx));
        SET_LO(r->cx, 0x3f);
        div8(r, 0x3f);
        SET_LO(r->cx, LO(r->cx) - LO(r->ax));
        r->ax = word((u16)((pass == 0 ? OFF(carrierKsl) : OFF(modulatorKsl)) + r->bx));
        r->bx >>= 1;
        SET_HI(r->cx, LO(r->ax) << 6);
        SET_LO(r->cx, LO(r->cx) | HI(r->cx));
        SET_HI(r->cx, 0);
        u16 bx = r->bx;
        r->dx = *at((u16)(OFF(levelOp) + r->bx + (pass == 0 ? 0 : 9)));
        r->di = r->dx;
        r->bx = (u16)(*at((u16)(OFF(opRegister) + r->di)) + 0x40);
        opl_write(r);
        r->bx = bx;
    }
    r->ax = ax;
    r->cx = cx;
    r->dx = dx;
    r->di = di;
}

/* adl_voice_pitchbend_apply (1F20h): CX, a note * 4, moved by voice BX's channel's bend (2000h:
 * none; 0ABh a step), and kept at or below 1FCh. */
static void voice_bend(Cpu *r) {
    Cpu saved = *r;
    r->ax = *at((u16)(OFF(voiceChannel) + r->bx));
    r->si = (u16)(r->ax << 1);
    r->di = word((u16)(OFF(chBend) + r->si));
    u8 sign;
    if (r->di == 0x2000) {
        r->ax = 0;
        sign = 0;
    } else if (r->di > 0x2000) {
        r->ax = (u16)(r->di - 0x2000);
        sign = 1;
    } else {
        r->ax = (u16)(0x2000 - r->di);
        sign = 0xff;
    }
    SET_LO(r->bx, 0xab);
    div8(r, 0xab);
    r->ax = LO(r->ax);
    u16 cx = sign == 1 ? (u16)(r->cx + r->ax) : (u16)(r->cx - r->ax);
    if (cx > 0x1fc)
        cx = 0x1fc;
    *r = saved;
    r->cx = cx;
}

/* adl_voice_render_freq (1E55h): voice BX plays note CL, keyed on when DX != 0: its F-number,
 * block and key-on, and its loudness from the velocity, the channel's volume and the master
 * volume. */
static void voice_render(Cpu *r) {
    Cpu saved = *r;
    *at((u16)(OFF(voiceNote) + r->bx)) = LO(r->cx);
    r->ax = *at((u16)(OFF(voiceChannel) + r->bx));
    r->si = r->ax;
    SET_HI(r->cx, 0);
    SET_LO(r->ax, *at((u16)(OFF(voiceProgram) + r->bx)));
    if (LO(r->ax) >= 0x80) { /* a rhythm patch: its own pitch */
        u8 note = LO(r->cx);
        note = note < 0x1b ? 0x1b : note > 0x58 ? 0x58 : note;
        SET_LO(r->cx, *at((u16)(OFF(drumPitch) + (u8)(note - 0x1b))));
    }
    r->cx = (u16)(r->cx << 2);
    voice_bend(r);
    if (r->cx == 0xffff) /* never: voice_bend keeps CX at or below 1FCh */
        goto done;
    r->ax = r->cx;
    SET_HI(r->dx, 0x30);
    div8(r, 0x30); /* AL = the octave, AH = a quarter semitone in it */
    r->cx = HI(r->ax);
    r->di = (u16)(r->cx << 1);
    r->cx = word((u16)(OFF(fnum) + r->di));
    r->bx += 0xa0;
    opl_write(r);
    if (LO(r->ax) > 0)
        SET_LO(r->ax, LO(r->ax) - 1);
    SET_LO(r->ax, (u8)(LO(r->ax) << 2) | HI(r->cx));
    if (LO(r->dx) != 0)
        SET_LO(r->ax, LO(r->ax) | 0x20);
    r->cx = LO(r->ax);
    r->bx += 0x10;
    u16 keyRegister = r->bx, keyValue = r->cx;
    r->bx -= 0xb0;
    SET_LO(r->ax, *at((u16)(OFF(chVolume) + r->si)) + 1);
    r->cx = *at((u16)(OFF(voiceVelocity) + r->bx));
    r->di = r->cx;
    SET_LO(r->cx, *at((u16)(OFF(velocityCurve) + r->di)) + 1);
    mul8(r, LO(r->cx));
    SET_LO(r->cx, 6);
    r->ax >>= 6;
    SET_LO(r->cx, *at(OFF(masterVolume)) + 1);
    mul8(r, LO(r->cx));
    SET_LO(r->cx, 4);
    r->ax >>= 4;
    if (LO(r->ax) != 0)
        SET_LO(r->ax, LO(r->ax) - 1);
    if (*at(OFF(masterEnable)) == 0)
        SET_LO(r->ax, 0);
    voice_render_level(r);
    r->cx = keyValue;
    r->bx = keyRegister;
    opl_write(r);
done:
    *r = saved;
}

/* adl_voice_lru_remove (1E26h): voice BL becomes the youngest. Leaves BH = 0. */
static void voice_touch(Cpu *r) {
    u8 *age = at(OFF(voiceAge));
    for (unsigned i = 0; i < 9; i++) {
        if (age[i] != LO(r->bx))
            continue;
        for (; i < 8; i++)
            age[i] = age[i + 1];
    }
    SET_HI(r->bx, 0);
    age[8] = LO(r->bx);
}

/* adl_voice_release (1DF6h): voice BX keyed off and idle. */
static void voice_release(Cpu *r) {
    u16 cx = r->cx, dx = r->dx, si = r->si;
    *at((u16)(OFF(voiceHeld) + r->bx)) = 0;
    r->cx = *at((u16)(OFF(voiceNote) + r->bx));
    r->dx = 0;
    voice_render(r);
    *at((u16)(OFF(voiceNote) + r->bx)) = 0xff;
    voice_touch(r);
    r->dx = *at((u16)(OFF(voiceChannel) + r->bx));
    r->si = r->dx;
    (*at((u16)(OFF(chActive) + r->si)))--;
    r->cx = cx;
    r->dx = dx;
    r->si = si;
}

/* adl_voice_keyon (1D77h): voice BX plays note CH at velocity CL (halved), loading its
 * channel's program first if it holds another (a rhythm note's program on channel 9). */
static void voice_keyon(Cpu *r) {
    u16 cx = r->cx, dx = r->dx, di = r->di, si = r->si;
    r->dx = *at((u16)(OFF(voiceChannel) + r->bx));
    r->si = r->dx;
    SET_LO(r->dx, *at((u16)(OFF(chProgram) + r->si)));
    (*at((u16)(OFF(chActive) + r->si)))++;
    voice_touch(r);
    if (r->si == 9) {
        u8 note = HI(r->cx);
        note = note < 0x1b ? 0x1b : note > 0x58 ? 0x58 : note;
        SET_LO(r->dx, note + 0x65);
    }
    if (LO(r->dx) != *at((u16)(OFF(voiceProgram) + r->bx)) && *at(OFF(masterEnable)) != 0) {
        u16 velocity = r->cx;
        *at((u16)(OFF(voiceProgram) + r->bx)) = LO(r->dx);
        r->di = (u16)(LO(r->dx) * 28);
        r->cx = (u16)(r->di + OFF(bank));
        voice_apply_patch(r);
        r->cx = velocity;
    }
    *at((u16)(OFF(voiceVelocity) + r->bx)) = LO(r->cx);
    SET_LO(r->cx, HI(r->cx));
    r->dx = 1;
    voice_render(r);
    r->cx = cx;
    r->dx = dx;
    r->di = di;
    r->si = si;
}

/* adl_voice_allocate (1AD4h): a voice for channel AL in BX (FFFFh: none), oldest first: idle
 * with the channel's program, idle, then one of the channel furthest over its reserved voices
 * (or of channel AL), released. Leaves AH = 0. */
static void voice_allocate(Cpu *r) {
    u16 cx = r->cx, dx = r->dx, si = r->si;
    SET_HI(r->dx, 0);
    SET_HI(r->ax, 0);
    r->si = r->ax;
    r->bx = 0;
    SET_LO(r->dx, *at((u16)(OFF(chProgram) + r->si)));
    for (r->si = 0; r->si < 9; r->si++) {
        SET_LO(r->bx, *at((u16)(OFF(voiceAge) + r->si)));
        if (*at((u16)(OFF(voiceNote) + r->bx)) == 0xff &&
            *at((u16)(OFF(voiceProgram) + r->bx)) == LO(r->dx)) {
            *at((u16)(OFF(voiceChannel) + r->bx)) = LO(r->ax);
            goto done;
        }
    }
    r->bx = 0;
    for (r->si = 0; r->si < 9; r->si++) {
        SET_LO(r->bx, *at((u16)(OFF(voiceAge) + r->si)));
        if (*at((u16)(OFF(voiceNote) + r->bx)) == 0xff) {
            *at((u16)(OFF(voiceChannel) + r->bx)) = LO(r->ax);
            goto done;
        }
    }
    r->bx = 0;
    r->cx = 0;
    r->dx = 0;
    for (r->si = 0; r->si < 16; r->si++) {
        u8 active = *at((u16)(OFF(chActive) + r->si)),
           reserved = *at((u16)(OFF(chReserved) + r->si));
        SET_LO(r->cx, active);
        if (active <= reserved)
            continue;
        SET_LO(r->cx, active - reserved);
        if (LO(r->bx) >= LO(r->cx))
            continue;
        SET_LO(r->bx, LO(r->cx));
        r->dx = r->si;
    }
    SET_LO(r->cx, LO(r->bx) > 0 ? LO(r->dx) : LO(r->ax));
    r->bx = 0;
    for (r->si = 0; r->si < 9; r->si++) {
        SET_LO(r->bx, *at((u16)(OFF(voiceAge) + r->si)));
        if (*at((u16)(OFF(voiceChannel) + r->bx)) == LO(r->cx)) {
            voice_release(r);
            *at((u16)(OFF(voiceChannel) + r->bx)) = LO(r->ax);
            goto done;
        }
    }
    r->bx = 0xffff;
done:
    r->cx = cx;
    r->dx = dx;
    r->si = si;
}

/* ---- the reserved voices (controller 75) ------------------------------------------------ */

/* adl_sustain_pool_reserve (1C1Eh): reserve CL voices for channel AL among those no channel
 * reserved (releasing their notes); what cannot be reserved is owed. */
static void pool_reserve(Cpu *r) {
    u16 cx = r->cx, bx = r->bx, si = r->si;
    for (r->bx = 0; r->bx < 9 && LO(r->cx) > 0; r->bx++) {
        if (*at((u16)(OFF(voiceReserved) + r->bx)) != 0xff)
            continue;
        if (*at((u16)(OFF(voiceNote) + r->bx)) != 0xff)
            voice_release(r);
        *at((u16)(OFF(voiceReserved) + r->bx)) = LO(r->ax);
        SET_HI(r->ax, 0);
        r->si = r->ax;
        (*at((u16)(OFF(chReserved) + r->si)))++;
        SET_LO(r->cx, LO(r->cx) - 1);
    }
    r->si = LO(r->ax);
    *at((u16)(OFF(chOwed) + r->si)) += LO(r->cx);
    r->cx = cx;
    r->bx = bx;
    r->si = si;
}

/* adl_sustain_pool_release (1C62h): give back CL of channel AL's reserved voices: owed ones
 * first, then idle ones, then sounding ones (released). */
static void pool_release(Cpu *r) {
    u16 bx = r->bx, cx = r->cx, si = r->si;
    r->si = LO(r->ax);
    u8 *owed = at((u16)(OFF(chOwed) + r->si));
    if (*owed >= LO(r->cx)) {
        *owed -= LO(r->cx);
        goto done;
    }
    SET_LO(r->cx, LO(r->cx) - *owed);
    *owed = 0;
    for (int sounding = 0; sounding < 2; sounding++) {
        for (r->bx = 0; r->bx < 9; r->bx++) {
            if (*at((u16)(OFF(voiceReserved) + r->bx)) != LO(r->ax))
                continue;
            if (!sounding && *at((u16)(OFF(voiceNote) + r->bx)) != 0xff)
                continue;
            if (sounding)
                voice_release(r);
            *at((u16)(OFF(voiceReserved) + r->bx)) = 0xff;
            SET_HI(r->ax, 0);
            r->si = r->ax;
            (*at((u16)(OFF(chReserved) + r->si)))--;
            SET_LO(r->cx, LO(r->cx) - 1);
            if (LO(r->cx) == 0)
                goto done;
        }
    }
done:
    r->bx = bx;
    r->cx = cx;
    r->si = si;
}

/* adl_sustain_pool_drain (1BC4h): the voices no channel reserved go to the channels owed some,
 * from channel 0. */
static void pool_drain(Cpu *r) {
    u16 ax = r->ax, cx = r->cx, si = r->si;
    SET_LO(r->cx, 0);
    for (r->si = 0; r->si < 9; r->si++)
        if (*at((u16)(OFF(voiceReserved) + r->si)) == 0xff)
            SET_LO(r->cx, LO(r->cx) + 1);
    if (LO(r->cx) == 0)
        goto done;
    for (r->si = 0; r->si < 16; r->si++) {
        SET_HI(r->cx, *at((u16)(OFF(chOwed) + r->si)));
        if (HI(r->cx) == 0)
            continue;
        if (HI(r->cx) >= LO(r->cx)) { /* owed as many or more: it takes them all */
            SET_HI(r->cx, HI(r->cx) - LO(r->cx));
            *at((u16)(OFF(chOwed) + r->si)) = HI(r->cx);
            r->ax = r->si;
            pool_reserve(r);
            goto done;
        }
        SET_LO(r->cx, LO(r->cx) - HI(r->cx));
        u16 left = r->cx;
        SET_LO(r->cx, HI(r->cx));
        *at((u16)(OFF(chOwed) + r->si)) = 0;
        r->ax = r->si;
        pool_reserve(r);
        r->cx = left;
    }
done:
    r->ax = ax;
    r->cx = cx;
    r->si = si;
}

/* adl_sustain_pool_resize (1B84h): channel AL to have CL voices reserved, owed ones counted. */
static void pool_resize(Cpu *r) {
    u16 si = r->si, cx = r->cx, dx = r->dx;
    r->dx = 0;
    for (r->si = 0; r->si < 9; r->si++)
        if (*at((u16)(OFF(voiceReserved) + r->si)) == LO(r->ax))
            SET_LO(r->dx, LO(r->dx) + 1);
    r->si = LO(r->ax);
    SET_LO(r->dx, LO(r->dx) + *at((u16)(OFF(chOwed) + r->si)));
    if (LO(r->dx) == LO(r->cx))
        goto done;
    if (LO(r->dx) < LO(r->cx)) {
        SET_LO(r->cx, LO(r->cx) - LO(r->dx));
        pool_reserve(r);
    } else {
        SET_LO(r->dx, LO(r->dx) - LO(r->cx));
        SET_LO(r->cx, LO(r->dx));
        pool_release(r);
        pool_drain(r);
    }
done:
    r->si = si;
    r->cx = cx;
    r->dx = dx;
}

/* ---- controllers ----------------------------------------------------------------------- */

/* The channel's sounding voices rendered again (a volume or pan changed): CL is left with the
 * note of the channel's last voice, idle or not. */
static void channel_render(Cpu *r) {
    r->cx = 0;
    r->dx = 1;
    for (r->bx = 0; r->bx < 9; r->bx++) {
        if (*at((u16)(OFF(voiceChannel) + r->bx)) != LO(r->ax))
            continue;
        SET_LO(r->cx, *at((u16)(OFF(voiceNote) + r->bx)));
        if (LO(r->cx) != 0xff)
            voice_render(r);
    }
}

/* adl_cc_main_volume (1CDBh): controller 7, halved. */
static void cc_volume(Cpu *r) {
    u16 bx = r->bx, cx = r->cx, dx = r->dx, si = r->si;
    r->si = LO(r->ax);
    *at((u16)(OFF(chVolume) + r->si)) = LO(r->cx) >> 1;
    channel_render(r);
    r->bx = bx;
    r->cx = cx;
    r->dx = dx;
    r->si = si;
}

/* adl_cc_pan (1D14h): controller 10, which no register takes. It leaves CX and DX as the
 * render loop does. */
static void cc_pan(Cpu *r) {
    u16 si = r->si, bx = r->bx;
    r->si = LO(r->ax);
    *at((u16)(OFF(chPan) + r->si)) = LO(r->cx);
    channel_render(r);
    r->bx = bx;
    r->si = si;
}

/* adl_cc_sustain_pedal (1D47h): controller 64; let go, it releases the notes it held. */
static void cc_sustain(Cpu *r) {
    u16 bx = r->bx, si = r->si;
    r->si = LO(r->ax);
    *at((u16)(OFF(chSustain) + r->si)) = LO(r->cx);
    if (LO(r->cx) == 0)
        for (r->bx = 0; r->bx < 9; r->bx++)
            if (*at((u16)(OFF(voiceChannel) + r->bx)) == LO(r->ax) &&
                *at((u16)(OFF(voiceHeld) + r->bx)) != 0)
                voice_release(r);
    r->bx = bx;
    r->si = si;
}

/* ---- the slots --------------------------------------------------------------------------- */

/* Slot 4, adl_midi_note_off (1952h): channel AL's note CH off, or held while its pedal is down. */
static void note_off(Cpu *r) {
    u16 bx = r->bx, si = r->si;
    for (r->bx = 0; r->bx < 9; r->bx++) {
        if (*at((u16)(OFF(voiceChannel) + r->bx)) != LO(r->ax) ||
            *at((u16)(OFF(voiceNote) + r->bx)) != HI(r->cx))
            continue;
        r->si = LO(r->ax);
        if (*at((u16)(OFF(chSustain) + r->si)) != 0)
            *at((u16)(OFF(voiceHeld) + r->bx)) = 1;
        else
            voice_release(r);
    }
    r->bx = bx;
    r->si = si;
}

/* Slot 5, adl_midi_note (1988h): channel AL's note CH at velocity CL (0: off). Notes outside
 * 0Ch-6Bh are dropped; the velocity is halved in CL. */
static void note_on(Cpu *r) {
    u16 bx = r->bx, si = r->si;
    if (LO(r->cx) == 0) {
        note_off(r);
        goto done;
    }
    if (HI(r->cx) < 0x0c || HI(r->cx) > 0x6b)
        goto done;
    SET_LO(r->cx, LO(r->cx) >> 1);
    for (r->bx = 0; r->bx < 9; r->bx++) {
        if (*at((u16)(OFF(voiceChannel) + r->bx)) == LO(r->ax) &&
            *at((u16)(OFF(voiceNote) + r->bx)) == HI(r->cx)) {
            voice_release(r);
            voice_keyon(r);
            goto done;
        }
    }
    voice_allocate(r);
    if (LO(r->bx) != 0xff)
        voice_keyon(r);
done:
    r->bx = bx;
    r->si = si;
}

/* Slot 7, adl_midi_control_change (19CFh): controller CH of channel AL to CL. */
static void control_change(Cpu *r) {
    u16 bx = r->bx;
    switch (HI(r->cx)) {
    case 0x07:
        cc_volume(r);
        break;
    case 0x0a:
        cc_pan(r);
        break;
    case 0x40:
        cc_sustain(r);
        break;
    case 0x4b:
        pool_resize(r);
        break;
    case 0x7b: /* all notes off */
        for (r->bx = 0; r->bx < 9; r->bx++)
            if (*at((u16)(OFF(voiceChannel) + r->bx)) == LO(r->ax) &&
                *at((u16)(OFF(voiceNote) + r->bx)) != 0xff)
                voice_release(r);
        break;
    default:
        break;
    }
    r->bx = bx;
}

/* Slot 10, adl_midi_pitch_bend (1A29h): channel AL's bend from CH (low 7 bits) and CL (high
 * 7), then its sounding voices rendered again. */
static void pitch_bend(Cpu *r) {
    u16 bx = r->bx, cx = r->cx, dx = r->dx;
    u8 high = LO(r->cx), low = HI(r->cx);
    if (high & 1)
        low |= 0x80;
    r->cx = (u16)((high >> 1) << 8 | low);
    r->bx = (u16)(LO(r->ax) << 1);
    set_word((u16)(OFF(chBend) + r->bx), r->cx);
    for (r->bx = 0; r->bx < 9; r->bx++) {
        if (*at((u16)(OFF(voiceChannel) + r->bx)) != LO(r->ax) ||
            *at((u16)(OFF(voiceNote) + r->bx)) == 0xff)
            continue;
        SET_LO(r->cx, *at((u16)(OFF(voiceNote) + r->bx)));
        r->dx = 1;
        voice_render(r);
    }
    r->bx = bx;
    r->cx = cx;
    r->dx = dx;
}

/* Slot 12, adl_set_master_volume (1A8Dh): CL (FFh: only ask); the sounding voices rendered again.
 * AX = the previous; CL is left with the last sounding voice's note. */
static void set_master_volume(Cpu *r) {
    u16 bx = r->bx, dx = r->dx;
    r->ax = *at(OFF(masterVolume));
    if (LO(r->cx) != 0xff) {
        *at(OFF(masterVolume)) = LO(r->cx);
        for (r->bx = 0; r->bx < 9; r->bx++) {
            if (*at((u16)(OFF(voiceNote) + r->bx)) == 0xff)
                continue;
            SET_LO(r->cx, *at((u16)(OFF(voiceNote) + r->bx)));
            r->dx = 1;
            voice_render(r);
        }
    }
    r->bx = bx;
    r->dx = dx;
}

/* Slot 13, adl_set_master_enable (1A68h): CL (FFh: only ask). The voices are rendered again at
 * the master volume, or at 0 when disabled, and the master volume kept. AX = the previous. */
static void set_master_enable(Cpu *r) {
    r->ax = *at(OFF(masterEnable));
    if (LO(r->cx) == 0xff)
        return;
    u16 cx = r->cx, ax = r->ax;
    *at(OFF(masterEnable)) = LO(r->cx);
    if (LO(r->cx) != 0)
        SET_LO(r->cx, *at(OFF(masterVolume)));
    set_master_volume(r);
    *at(OFF(masterVolume)) = LO(r->ax);
    r->ax = ax;
    r->cx = cx;
}

/* Slot 17, adl_query_state (23D9h): channel AL's bend (AH = E0h), program (C0h) or controller
 * CH (B0h: 7, 10, 64, or 75: its owed voices, or with CL = FFh its reserved ones); else FFFFh. */
static void query(Cpu *r) {
    u16 si = LO(r->ax);
    switch (HI(r->ax)) {
    case 0xe0:
        r->ax = word((u16)(OFF(chBend) + 2 * si));
        return;
    case 0xc0:
        r->ax = *at((u16)(OFF(chProgram) + si));
        return;
    case 0xb0:
        switch (HI(r->cx)) {
        case 0x07:
            r->ax = *at((u16)(OFF(chVolume) + si));
            return;
        case 0x0a:
            r->ax = *at((u16)(OFF(chPan) + si));
            return;
        case 0x40:
            r->ax = *at((u16)(OFF(chSustain) + si));
            return;
        case 0x4b:
            r->ax = *at((u16)((LO(r->cx) == 0xff ? OFF(chReserved) : OFF(chOwed)) + si));
            return;
        default:
            break;
        }
        break;
    default:
        break;
    }
    r->ax = 0xffff;
}

/* Slot 1, adl_load_patch_bank (2446h): the bank's bankSize bytes, a reset, master volume 15.
 * AX = 2446h (the routine's address), CX = 0800h. */
static void load_bank(Cpu *r, const unsigned char *bank) {
    u16 bx = r->bx, dx = r->dx, si = r->si, di = r->di;
    u16 size = word(OFF(bankSize));
    for (u16 i = 0; i < size; i++)
        *at((u16)(OFF(bank) + i)) = bank[i];
    if (size != 0)
        SET_LO(r->bx, bank[size - 1]);
    r->cx = 0;
    silence_and_reset(r);
    SET_LO(r->cx, 0x0f);
    set_master_volume(r);
    r->ax = 0x2446;
    r->cx = 0x0800;
    r->bx = bx;
    r->dx = dx;
    r->si = si;
    r->di = di;
}

void adl_dispatch(unsigned char *chunk, uint16_t slot, DudeRegs *regs) {
    Cpu r = {regs->ax, regs->bx, regs->cx, 0, 0, 0};
    s_d = chunk;
    switch (slot) {
    case 0: /* adl_get_info: interface 1, device 3, 9 voices */
        r.ax = 0x0103;
        r.cx = 0x0009;
        break;
    case 1:
        load_bank(&r, regs->bank);
        break;
    case 2: /* adl_reset_voices */
        silence_and_reset(&r);
        break;
    case 4:
        note_off(&r);
        break;
    case 5:
        note_on(&r);
        break;
    case 7:
        control_change(&r);
        break;
    case 8: /* adl_midi_program_change */
        *at((u16)(OFF(chProgram) + LO(r.ax))) = LO(r.cx);
        break;
    case 10:
        pitch_bend(&r);
        break;
    case 11: /* adl_set_global_param_11d */
        r.ax = *at(OFF(globalParam));
        if (LO(r.cx) != 0xff)
            *at(OFF(globalParam)) = LO(r.cx);
        break;
    case 12:
        set_master_volume(&r);
        break;
    case 13:
        set_master_enable(&r);
        break;
    case 17:
        query(&r);
        break;
    default: /* 3, 6, 9, 14-16: a RET */
        break;
    }
    regs->ax = r.ax;
    regs->bx = r.bx;
    regs->cx = r.cx;
}
