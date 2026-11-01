/*
 * DRIVERS/SX/ASB.ASM in C: the "CMS Sound Blaster" driver, which plays blocks of 8-bit samples
 * through the Sound Blaster's DSP and the 8237's channel 1 and takes the DSP's interrupt when a
 * block ends (the card: port/platform/sbdsp.c).
 *
 * The game sends six commands (dude.h's digital entry: AX = the command, SS:SI = its arguments):
 *    0 install ([0]: the base port, 0 for 220h): reset and probe the DSP, find its IRQ by a one
 *      sample block on IRQ 2, 3, 5 and 7, set 11025 Hz. AX = 13E6h, or 0 when it failed
 *    1 the timer's tick (every second one): work only while streaming from a file
 *    2 shut down
 *    3 play ([0] AH: loop, [2] the rate, [4] the samples' far pointer, [8] their count): the
 *      block is cut in two where it crosses a 64 KB page, the 8237's limit
 *    4 poll ([0] AH: loop): AL = 1 once nothing plays, AH = 1 once after a loop started over
 *    5 stop
 * and 6 (the rate), 7, 8 (unmute), 12 (stop), 14 and 15 are as simple. 9, 10, 11 and 13 (the
 * file streaming and its position) trap: the game never sends them, and neither can the paths
 * that only streaming reaches (slot 1's work, the interrupt's streaming half).
 *
 * The state lives in the chunk at the original's offsets (AsbImage). Natively:
 * - The interrupt handlers are C functions given to the platform's vector table; a vector the
 *   driver saves is the handler's pointer, in the four bytes the original keeps a segment and
 *   an offset in.
 * - Slot 0 does not hook INT 10h, 0Dh and 74h nor ask DOS for its InDOS flag. The original does,
 *   to tell when DOS or the BIOS is busy while it streams from a file; nothing else reads them,
 *   and vectors past 1Fh do not exist natively. The saved vectors and the flags' pointers stay 0.
 * - A block's far pointer is a host pointer: its segment and offset come from the segment table
 *   (FP_SEG, FP_OFF), and give the 20-bit address the 8237 is programmed with as in the
 *   original; the platform reads the block back through the same table.
 * - The probe's wait for the interrupt polls (BAK_IDLE): the platform delivers interrupts where
 *   the game polls. The original spins on the byte the handler sets; an interrupt before the
 *   spin, which the driver clears just before, would be missed.
 */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "dude.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "bak_game_end.h"

typedef uint8_t u8;

#pragma pack(push, 1)
typedef struct AsbImage {
    unsigned char header[0x34];        /* DUDE header (tag "audblast"), an 8-byte curve */
    u8 page1;                          /* 034h  the block's page and that of its part past it */
    u8 page2;                          /* 035h */
    u8 stereo;                         /* 036h  streaming only, as all fields marked (s) */
    u8 accumulator;                    /* 037h  (s) */
    u8 autoInitDsp;                    /* 038h  the DSP (2.00 on) plays auto-init blocks */
    u8 dmaPage;                        /* 039h  the part being played's page */
    u8 halfToggle;                     /* 03Ah  (s) */
    u8 streaming;                      /* 03Bh  1: playing from a file (slot 11) */
    u8 producingHalf;                  /* 03Ch  (s) */
    u8 firstHalf;                      /* 03Dh  set by slot 3; (s) */
    u8 inInt0D;                        /* 03Eh  (s) the hooks' flags */
    u8 inInt74;                        /* 03Fh */
    u8 advancing;                      /* 040h */
    u8 producing;                      /* 041h */
    u8 inInt10;                        /* 042h */
    u8 fileLoop;                       /* 043h  (s) */
    u8 irq;                            /* 044h  the IRQ line the probe found */
    u8 restarted;                      /* 045h  a loop started over: slot 4 tells and clears it */
    u8 loop;                           /* 046h */
    u8 rhythm;                         /* 047h  (s) */
    u8 mute;                           /* 048h  (s) */
    u8 autoInit;                       /* 049h  (s) */
    u8 refill;                         /* 04Ah  (s) */
    u8 part;                           /* 04Bh  0: the first part plays, 1: the one past the page */
    u8 paused;                         /* 04Ch  (s) */
    u8 picMask;                        /* 04Dh  the 8259's mask before the last hook */
    u8 format[4];                      /* 04Eh  (s) */
    u8 stopped;                        /* 052h  1: nothing plays */
    u8 held;                           /* 053h  (s) */
    u16 length1;                       /* 054h  the first part's samples, and its address */
    u16 address1;                      /* 056h */
    u16 length2;                       /* 058h  the part past the page (0: none) */
    u16 address2;                      /* 05Ah */
    u16 ring[3];                       /* 05Ch  (s) */
    u16 played[2];                     /* 062h  (s) high and low words */
    u16 w66, w68;                      /* 066h */
    u16 dmaLength;                     /* 06Ah  the transfer programmed */
    u16 partLength;                    /* 06Ch  the part being played, and its address */
    u16 partAddress;                   /* 06Eh */
    u16 ticks;                         /* 070h  (s) */
    u16 base;                          /* 072h  the DSP's base port */
    u16 rate;                          /* 074h  the rate asked for */
    u16 file;                          /* 076h  (s) the file's handle, FFFFh: none */
    u16 stream[4];                     /* 078h  (s) */
    u16 rateSet;                       /* 080h  the rate the DSP was given */
    u16 remaining[2];                  /* 082h  (s) */
    u8 savedVector[4];                 /* 086h  the IRQ's vector before the driver's handler */
    u8 dosFlags[8];                    /* 08Ah  (s) InDOS and the critical error flag's pointers */
    u8 savedInt0D[4];                  /* 092h  (s) the hooked vectors' previous ones */
    u8 savedInt74[4];                  /* 096h */
    u8 savedInt10[4];                  /* 09Ah */
    u8 probeSample;                    /* 09Eh  the probe's block */
    unsigned char code[0x15D3 - 0x9F]; /* the rest of the zeros, the dispatch table, code */
    u8 probeVectors[4][4];             /* 15D3h  IRQ 2, 3, 5 and 7's vectors while the probe runs */
    unsigned char code2[0x16FB - 0x15E3];
    u8 cdCheck; /* 16FBh  CDh: install only from a CD drive; the image has DCh */
} AsbImage;
#pragma pack(pop)

_Static_assert(offsetof(AsbImage, page1) == 0x34, "ASB.ASM data layout");
_Static_assert(offsetof(AsbImage, stopped) == 0x52, "ASB.ASM data layout");
_Static_assert(offsetof(AsbImage, base) == 0x72, "ASB.ASM data layout");
_Static_assert(offsetof(AsbImage, savedVector) == 0x86, "ASB.ASM data layout");
_Static_assert(offsetof(AsbImage, probeSample) == 0x9E, "ASB.ASM data layout");
_Static_assert(offsetof(AsbImage, probeVectors) == 0x15D3, "ASB.ASM data layout");
_Static_assert(sizeof(AsbImage) == 0x16FC, "ASB.ASM data layout");
/* A saved vector is a handler's pointer in four bytes. */
_Static_assert(sizeof(BakIsr *) == 4, "x86: vectors fit the original's four bytes");

#define CURRENT_DRIVE 2 /* INT 21h AH=19h: the game runs from C: */
#define SUCCESS 0x13E6  /* slot 0's own offset */

static AsbImage *s_image; /* the chunk the game last called: the handlers' */

static u16 rd16(const void *p) {
    const u8 *b = p;
    return (u16)(b[0] | b[1] << 8);
}

/* The DSP's ports as the original computes them: some add to DL alone, losing the carry. */
static u16 port_byte(const AsbImage *d, u8 reg) {
    return (u16)((d->base & 0xFF00) | (u8)(d->base + reg));
}

static u16 port_word(const AsbImage *d, u8 reg) {
    return (u16)(d->base + reg);
}

/* asb_dsp_wait_ready: wait for the DSP to take a byte (no limit), send it. */
static void dsp_send(u16 port, u8 v) {
    while (bak_port_in8(port) & 0x80)
        ;
    bak_port_out8(port, v);
}

/* ---- the handlers' vectors (asb_helper_0c50, 0c8e) ------------------------------------------ */

/* Line's vector to isr, the previous one saved, the line unmasked at the 8259 (the mask before
 * kept). The lines are 0-7: the vector is 8 + line. */
static void hook(AsbImage *d, u8 line, BakIsr *isr, u8 saved[4]) {
    BakIsr *old = bak_get_vector((uint8_t)(line + 8));
    memcpy(saved, &old, sizeof old);
    bak_set_vector((uint8_t)(line + 8), isr);
    u8 mask = bak_port_in8(0x21);
    d->picMask = mask;
    bak_port_out8(0x21, (u8)(mask & ~(line < 8 ? 1u << line : 0u)));
}

static void unhook(AsbImage *d, u8 line, const u8 saved[4]) {
    BakIsr *old;
    memcpy(&old, saved, sizeof old);
    bak_set_vector((uint8_t)(line + 8), old);
    bak_port_out8(0x21, d->picMask);
}

/* ---- DMA (asb_helper_segoff, asb_helper_0a3d, 0a95, 0b14) ------------------------------------ */

/* A segment and an offset as the 20-bit address the 8237 takes: the page, and the address. */
static u16 dma_address(u16 seg, u16 off, u8 *page) {
    u16 r = (u16)(seg << 4 | seg >> 12);
    uint32_t sum = (uint32_t)off + (u16)(r & 0xFFF0);
    *page = (u8)((r & 0x0F) + (sum >> 16));
    return (u16)sum;
}

/* The 8237 at the part to play, then the DSP's single-cycle block (or, streaming on a DSP that
 * plays them, its auto-init block of twice the samples). */
static void dma_start(AsbImage *d) {
    bool autoInit = d->streaming == 1 && d->autoInit == 1;
    u16 count = d->partLength;
    u8 mode = 0x49;
    bak_port_out8(0x0A, 5);
    bak_port_out8(0x0C, 0);
    bak_port_out8(0x02, (u8)d->partAddress);
    bak_port_out8(0x02, (u8)(d->partAddress >> 8));
    if (autoInit) {
        mode = 0x59;
        count = (u16)(count << 1);
    }
    d->dmaLength = count;
    bak_port_out8(0x0B, mode);
    bak_port_out8(0x83, d->dmaPage);
    bak_port_out8(0x03, (u8)(count - 1));
    bak_port_out8(0x03, (u8)((u16)(count - 1) >> 8));
    bak_port_out8(0x0A, 1);
    u16 port = port_byte(d, 0x0C);
    if (autoInit) {
        dsp_send(port, 0x1C);
        return;
    }
    u16 n = (u16)(d->partLength - 1);
    dsp_send(port, 0x14);
    dsp_send(port, (u8)n);
    dsp_send(port, (u8)(n >> 8));
}

/* The part asb_flag_4b names, if streaming lets it start yet. */
static void play_part(AsbImage *d) {
    if (d->part == 0) {
        d->partLength = d->length1;
        d->dmaPage = d->page1;
        d->partAddress = d->address1;
    } else {
        d->partLength = d->length2;
        d->dmaPage = d->page2;
        d->partAddress = d->address2;
    }
    if (d->streaming == 1 && d->autoInit == 1) {
        if (d->firstHalf == 0)
            return;
        d->firstHalf = 0;
    }
    dma_start(d);
}

/* The channel masked, the DSP's interrupt acknowledged. */
static void dma_acknowledge(AsbImage *d) {
    bak_port_out8(0x0A, 5);
    bak_port_in8(port_byte(d, 0x0E));
}

/* ---- the DSP (asb_helper_0bbd, 0c1b) --------------------------------------------------------- */

/* DIV with the game's divide-error handler (INT00.ASM): an overflowing quotient is tried again
 * with the dividend halved; a zero divisor hangs the original. */
static u16 div_resolved(uint32_t dxax, u16 divisor) {
    if (divisor == 0)
        bak_unimplemented("ASB.ASM: a rate of 0 Hz divides by zero (the original hangs)", __FILE__,
                          __LINE__);
    while (dxax / divisor > 0xFFFF)
        dxax = (uint32_t)((int32_t)dxax >> 1);
    return (u16)(dxax / divisor);
}

/* The rate as the DSP's time constant: 256 - 1000000 / rate, a byte. */
static void set_rate(AsbImage *d, u16 rate) {
    d->rateSet = rate;
    u8 tc = (u8) - (u8)div_resolved(1000000, rate);
    u16 port = port_byte(d, 0x0C);
    dsp_send(port, 0x40);
    dsp_send(port, tc);
}

/* Command D0h, the block held, then the DSP ready again. */
static void dsp_halt(AsbImage *d) {
    u16 port = port_byte(d, 0x0C);
    dsp_send(port, 0xD0);
    while (bak_port_in8(port) & 0x80)
        ;
}

/* asb_set_master_volume (slot 12): whatever plays stops, its handler unhooked. */
static void stop(AsbImage *d) {
    if (d->stopped == 1)
        return;
    d->stopped = 1;
    dsp_halt(d);
    dma_acknowledge(d);
    unhook(d, d->irq, d->savedVector);
}

/* ---- the interrupts (asb_isr_dma_complete, asb_irq_isr_a..d, asb_irq_tail) ------------------ */

/* A block ended: the part past the page plays next, or the block again when it loops, or it
 * all stops. */
static void block_ended(void) {
    AsbImage *d = s_image;
    if (d->streaming == 1)
        bak_unimplemented("ASB.ASM: the file streaming's interrupt (slot 11)", __FILE__, __LINE__);
    if (d->length2 != 0 && (d->part ^= 1) != 0) {
        play_part(d);
    } else if (d->loop == 1) {
        play_part(d);
        d->restarted = 1;
    } else {
        stop(d);
    }
    bak_port_in8(port_byte(d, 0x0E));
    bak_port_out8(0x20, 0x20);
}

/* The probe's: which line the one sample block came on. */
static void probe_tail(u8 line) {
    AsbImage *d = s_image;
    d->irq = line;
    bak_port_in8(port_word(d, 0x0E));
    bak_port_out8(0x20, 0x20);
}

static void probe_irq2(void) {
    probe_tail(2);
}

static void probe_irq3(void) {
    probe_tail(3);
}

static void probe_irq5(void) {
    probe_tail(5);
}

static void probe_irq7(void) {
    probe_tail(7);
}

static const struct {
    u8 line;
    BakIsr *isr;
} s_probeLines[4] = {{2, probe_irq2}, {3, probe_irq3}, {5, probe_irq5}, {7, probe_irq7}};

/* ---- the probe (asb_probe and its helpers) ----------------------------------------------- */

/* asb_dsp_read_byte: up to 800h looks for a byte to read. */
static u8 dsp_read_byte(const AsbImage *d, bool *timedOut) {
    u16 port = port_byte(d, 0x0E);
    u8 v = 0;
    for (u16 n = 0x800; n != 0; n--) {
        v = bak_port_in8(port);
        if (v & 0x80) {
            *timedOut = false;
            return bak_port_in8((u16)((port & 0xFF00) | (u8)(port - 4)));
        }
    }
    *timedOut = true;
    return v;
}

/* asb_dsp_read_buf: a byte to read, however long it takes. */
static u8 dsp_read_wait(const AsbImage *d) {
    u16 port = port_byte(d, 0x0E);
    while (!(bak_port_in8(port) & 0x80))
        ;
    return bak_port_in8((u16)((port & 0xFF00) | (u8)(port - 4)));
}

/* asb_dsp_write_busy: up to 800h looks for the DSP ready, then the byte; false on time out. */
static bool dsp_send_bounded(u16 port, u8 v) {
    for (u16 n = 0x800; n != 0; n--)
        if (!(bak_port_in8(port) & 0x80)) {
            bak_port_out8(port, v);
            return true;
        }
    return false;
}

/* asb_dsp_reset: 1 then 0 at the reset port, then up to 32 bytes until AAh. AX: 0 or 2. */
static u16 dsp_reset(const AsbImage *d) {
    u16 port = port_byte(d, 0x06);
    bool timedOut;
    u8 v;
    bak_port_out8(port, 1);
    for (int i = 0; i < 4; i++) /* the reset pulse's few microseconds */
        bak_port_in8(port);
    bak_port_out8(port, 0);
    u16 n = 0x20;
    do
        v = dsp_read_byte(d, &timedOut);
    while (--n != 0 && v != 0xAA);
    return v == 0xAA ? 0 : 2;
}

/* asb_probe_1516: identification E0h AAh answers 55h. AX: 0 or 2. */
static u16 dsp_identify(const AsbImage *d) {
    u16 port = port_word(d, 0x0C);
    bool timedOut;
    if (!dsp_send_bounded(port, 0xE0) || !dsp_send_bounded(port, 0xAA))
        return 2;
    u8 v = dsp_read_byte(d, &timedOut);
    return !timedOut && v == 0x55 ? 0 : 2;
}

/* asb_dsp_get_version: E1h, the major then the minor version; below 1.01 will not do (AX 1),
 * from 2.00 on the DSP plays auto-init blocks (which only the file streaming uses). The
 * comparisons are signed. */
static u16 dsp_version(AsbImage *d) {
    dsp_send(port_byte(d, 0x0C), 0xE1);
    u8 major = dsp_read_wait(d);
    s16 v = (s16)(major << 8 | dsp_read_wait(d));
    if (v < 0x101)
        return 1;
    d->autoInitDsp = v < 0x200 ? 0 : 1;
    return 0;
}

/* asb_init_setup: a one sample block from the chunk's byte 9Eh with IRQ 2, 3, 5 and 7 hooked,
 * then up to 800h polls for one of them to answer. AX: 0, or 3 when none did. */
static u16 find_irq(AsbImage *d) {
    for (int i = 0; i < 4; i++)
        hook(d, s_probeLines[i].line, s_probeLines[i].isr, d->probeVectors[i]);
    u8 page;
    u16 address = dma_address(FP_SEG(d), (u16)(FP_OFF(d) + offsetof(AsbImage, probeSample)), &page);
    bak_port_out8(0x0A, 5); /* asb_dma_program: one byte, mode 49h */
    bak_port_out8(0x0C, 0);
    bak_port_out8(0x0B, 0x49);
    bak_port_out8(0x02, (u8)address);
    bak_port_out8(0x02, (u8)(address >> 8));
    bak_port_out8(0x03, 0);
    bak_port_out8(0x03, 0);
    bak_port_out8(0x83, page);
    bak_port_out8(0x0A, 1);
    u16 port = port_word(d, 0x0C);
    dsp_send(port, 0x40); /* the rate 1000000 / (256 - 64h) */
    dsp_send(port, 0x64);
    dsp_send(port, 0x14); /* one sample */
    dsp_send(port, 0);
    dsp_send(port, 0);
    d->irq = 0;
    u16 n = 0x800;
    do
        BAK_IDLE();
    while (d->irq == 0 && --n != 0);
    u16 result = d->irq == 0 ? 3 : 0;
    for (int i = 0; i < 4; i++)
        unhook(d, s_probeLines[i].line, d->probeVectors[i]);
    return result;
}

/* asb_probe: the DSP reset, identified, its version, its IRQ; then the speaker on. */
static u16 probe(AsbImage *d) {
    u16 r;
    if ((r = dsp_reset(d)) != 0 || (r = dsp_identify(d)) != 0 || (r = dsp_version(d)) != 0 ||
        (r = find_irq(d)) != 0)
        return r;
    dsp_send(port_word(d, 0x0C), 0xD1);
    return 0;
}

/* ---- the commands -------------------------------------------------------------------------- */

static u16 install(AsbImage *d, const void *args) {
    u16 base = args != NULL ? rd16(args) : 0;
    d->base = base != 0 ? base : 0x220;
    if (probe(d) != 0)
        return 0;
    d->stopped = 1;
    if (d->cdCheck == 0xCD) {
        u16 bx = 0, ax = 0x150B;
        if (bak_mscdex_installed())
            ax = bak_mscdex_drive_check(CURRENT_DRIVE, &bx);
        if (bx != 0xADAD || ax == 0)
            return 0;
    }
    d->rate = 0x2B11; /* 11025 Hz */
    set_rate(d, d->rate);
    d->producing = 0;
    d->advancing = 0;
    d->file = 0xFFFF;
    return SUCCESS;
}

/* The block, cut where it crosses a 64 KB page. AX is what the original leaves: the last byte
 * the DSP got, twice. */
static u16 play(AsbImage *d, const unsigned char *args) {
    unsigned char *samples;
    u8 page;
    stop(d);
    d->loop = (rd16(args) >> 8) != 0;
    set_rate(d, rd16(args + 2));
    memcpy(&samples, args + 4, sizeof samples);
    u16 address = dma_address(FP_SEG(samples), FP_OFF(samples), &page);
    d->page1 = page;
    d->address1 = address;
    d->length1 = rd16(args + 8);
    uint32_t end = (uint32_t)d->length1 + address;
    if (end > 0xFFFF) {
        d->length2 = (u16)end;
        d->length1 = (u16)(d->length1 - d->length2);
        d->address2 = 0;
        d->page2 = (u8)(page + 1);
    } else {
        d->length2 = 0;
    }
    hook(d, d->irq, block_ended, d->savedVector);
    d->part = 0;
    d->streaming = 0;
    d->firstHalf = 1;
    play_part(d);
    d->ticks = 0;
    d->stopped = 0;
    d->restarted = 0;
    u8 last = (u8)((u16)(d->partLength - 1) >> 8);
    return (u16)(last << 8 | last);
}

uint16_t asb_dispatch(unsigned char *chunk, uint16_t cmd, void *args) {
    AsbImage *d = (AsbImage *)chunk;
    u16 r;
    s_image = d;
    switch (cmd) {
    case 0:
        return install(d, args);
    case 1:
        if (d->stopped == 1 || d->streaming != 1)
            return 0;
        bak_unimplemented("ASB.ASM: the timer's tick while streaming (slot 11)", __FILE__,
                          __LINE__);
    case 2: /* (the original restores INT 10h, 0Dh and 74h: not hooked natively) */
        stop(d);
        /* The original closes the streamed file's DOS handle; slot 9 never opens one, but
         * after a failed install the handle is still 0 and DOS's handle 0 gets closed. There
         * are no DOS handles natively. */
        d->file = 0xFFFF;
        return 0;
    case 3:
        return play(d, args);
    case 4:
        d->loop = (rd16(args) >> 8) != 0;
        r = (u16)(d->restarted << 8 | d->stopped);
        d->restarted = 0;
        return r;
    case 5:
    case 12:
        stop(d);
        return 0;
    case 6:
        d->rate = rd16(args);
        set_rate(d, d->rate);
        return 0;
    case 7:
    case 15:
        return FP_SEG(chunk); /* a bare RET: AX holds the CS the dispatch loaded for DS */
    case 8:
        d->mute = 0;
        return 0;
    case 14:
        return 0;
    case 9:
        bak_unimplemented("ASB.ASM: slot 9 (open a file to stream)", __FILE__, __LINE__);
    case 10:
        bak_unimplemented("ASB.ASM: slot 10 (pause the streaming)", __FILE__, __LINE__);
    case 11:
        bak_unimplemented("ASB.ASM: slot 11 (start the streaming)", __FILE__, __LINE__);
    case 13:
        bak_unimplemented("ASB.ASM: slot 13 (the streaming's position)", __FILE__, __LINE__);
    default:
        bak_unimplemented("ASB.ASM: a command past its 16 slots", __FILE__, __LINE__);
    }
}

int asb_isr_offset(BakIsr *isr) {
    if (isr == block_ended)
        return 0x0B22;
    for (int i = 0; i < 4; i++)
        if (isr == s_probeLines[i].isr)
            return 0x16BC + 9 * i;
    return isr == NULL ? 0 : -1;
}
