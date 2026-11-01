/*
 * VGA register file behind the game's inp/outp/outpw calls.
 *
 * Keeps the sequencer, CRTC, graphics-controller, attribute-controller and DAC
 * state the original reads back or relies on (CRTC start address, line compare
 * and pel panning drive the hardware scrolling in UI/SCROLL.C). Vertical retrace
 * comes from a virtual 70 Hz refresh on the platform's clock: Input Status 1 (3DAh)
 * reports it, and bak_vga_wait_retrace waits for it and shows the frame.
 *
 * Also the CPU's path to display memory through the graphics controller (the latches,
 * read modes 0-1 and write modes 0-3), and the register values the BIOS sets for the
 * modes the game uses.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bak_port.h"
#include "bak_video.h"
#include "platform.h"
#include "vgaregs.h"

/* Mode 13h timing: 449 lines of 31.78 us, 70.09 Hz; vertical sync spans 2 lines. */
#define FRAME_NS 14268000ull
#define LINE_NS 31778ull
#define RETRACE_NS (2 * LINE_NS)

/*
 * The registers INT 10h AH=00h loads, from the IBM VGA BIOS's parameter tables: mode 13h
 * (320x200, 256 colours) and mode 10h (640x350, 16 colours).
 */
#define MODE13_SEQ {0x03, 0x01, 0x0F, 0x00, 0x0E}
#define MODE13_CRTC                                                                                \
    {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x41, 0x00, 0x00, 0x00,                 \
     0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3, 0xFF}
#define MODE13_GC {0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF}
#define MODE13_ATTR                                                                                \
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,                             \
     0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x41, 0x00, 0x0F, 0x00, 0x00}
#define MODE13_MISC 0x63

typedef struct ModeRegs {
    uint8_t seq[5], crtc[25], gc[9], attr[21], misc;
} ModeRegs;

static const ModeRegs s_mode13 = {MODE13_SEQ, MODE13_CRTC, MODE13_GC, MODE13_ATTR, MODE13_MISC};
static const ModeRegs s_mode10 = {
    {0x03, 0x01, 0x0F, 0x00, 0x06},
    {0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F, 0x00, 0x40, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x83, 0x85, 0x5D, 0x28, 0x0F, 0x63, 0xBA, 0xE3, 0xFF},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A,
     0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x01, 0x00, 0x0F, 0x00, 0x00},
    0xA3,
};

/* The port starts as if in mode 13h: no text mode is ever shown. */
VgaRegs g_vga = {
    .seq = MODE13_SEQ,
    .crtc = MODE13_CRTC,
    .gc = MODE13_GC,
    .attr = MODE13_ATTR,
    .misc = MODE13_MISC,
    .pelMask = 0xFF,
};
uint8_t g_bakVgaLatch[4];
bool g_platformFreeRunning;

static uint8_t s_crtcIndex;
static uint8_t s_seqIndex;
static uint8_t s_gcIndex;
static uint8_t s_attrIndex;
static int s_attrFlipFlop; /* 0 = next 3C0h write is an index, 1 = data */
static uint8_t s_dacWriteIndex, s_dacReadIndex;
static int s_dacComponent; /* next component (0..2) of the triple being read or written */
static uint8_t s_dacTriple[3];
static uint8_t s_dacState; /* 3C7h read: 0 after a write index was set, 3 after a read index */

static uint64_t frame_phase(uint64_t ns) {
    return ns % FRAME_NS;
}

static uint8_t input_status_1(void) {
    /* A loop waiting on the retrace bit must see time pass on the virtual clock too. */
    clock_spend(CLOCK_POLL_NS);
    uint64_t phase = frame_phase(clock_now_ns());
    uint8_t v = 0;
    if (phase < RETRACE_NS)
        v |= 0x08 | 0x01;
    else if ((phase / LINE_NS) % 2)
        v |= 0x01; /* display enable toggles once per scanline */
    s_attrFlipFlop = 0;
    return v;
}

void bak_vga_wait_retrace(int next) {
    /* The original polls Input Status 1, and reading it resets the attribute flip-flop. */
    s_attrFlipFlop = 0;
    if (g_platformFreeRunning)
        return;
    uint64_t now = clock_now_ns();
    uint64_t phase = frame_phase(now);
    if (next || phase >= RETRACE_NS) {
        uint64_t target = now - phase + FRAME_NS;
        /* Interrupts keep coming while the game waits for retrace. */
        for (;;) {
            bak_poll();
            now = clock_now_ns();
            if (now >= target)
                break;
            uint64_t due = machine_next_due_ns();
            clock_idle(target - now < 250000 ? target - now : 250000, due < target ? due : target);
        }
    }
    video_present();
}

uint8_t bak_port_in8(uint16_t port) {
    uint8_t value;
    if (machine_port_in8(port, &value) || mpu401_port_in8(port, &value) ||
        opl_port_in8(port, &value))
        return value;
    switch (port) {
    case 0x3DA:
        return input_status_1();
    case 0x3C4:
        return s_seqIndex;
    case 0x3C5:
        return g_vga.seq[s_seqIndex & 7];
    case 0x3CE:
        return s_gcIndex;
    case 0x3D4:
        return s_crtcIndex;
    case 0x3D5:
        return g_vga.crtc[s_crtcIndex & 0x1F];
    case 0x3CF:
        return g_vga.gc[s_gcIndex & 15];
    case 0x3C0:
        return s_attrIndex;
    case 0x3C1:
        return g_vga.attr[s_attrIndex & 0x1F];
    case 0x3C6:
        return g_vga.pelMask;
    case 0x3C7:
        return s_dacState;
    case 0x3C8:
        return s_dacWriteIndex;
    case 0x3C9: {
        uint8_t v = g_vga.dac[s_dacReadIndex][s_dacComponent];
        if (++s_dacComponent == 3) {
            s_dacComponent = 0;
            s_dacReadIndex++;
        }
        return v;
    }
    case 0x3CC:
        return g_vga.misc;
    default:
        fprintf(stderr, "vga: unhandled in %03Xh\n", port);
        return 0xFF;
    }
}

void bak_port_out8(uint16_t port, uint8_t value) {
    if (machine_port_out8(port, value) || mpu401_port_out8(port, value) ||
        opl_port_out8(port, value))
        return;
    switch (port) {
    case 0x3C4:
        s_seqIndex = value;
        break;
    case 0x3C5:
        g_vga.seq[s_seqIndex & 7] = value;
        break;
    case 0x3D4:
        s_crtcIndex = value;
        break;
    case 0x3D5:
        g_vga.crtc[s_crtcIndex & 0x1F] = value;
        break;
    case 0x3CE:
        s_gcIndex = value;
        break;
    case 0x3CF:
        g_vga.gc[s_gcIndex & 15] = value;
        break;
    case 0x3C0:
        if (s_attrFlipFlop == 0)
            s_attrIndex = value;
        else
            g_vga.attr[s_attrIndex & 0x1F] = value;
        s_attrFlipFlop ^= 1;
        break;
    case 0x3C2:
        g_vga.misc = value;
        break;
    case 0x3C6:
        g_vga.pelMask = value;
        break;
    case 0x3C7:
        s_dacReadIndex = value;
        s_dacComponent = 0;
        s_dacState = 3;
        break;
    case 0x3C8:
        s_dacWriteIndex = value;
        s_dacComponent = 0;
        s_dacState = 0;
        break;
    case 0x3C9:
        /* The DAC commits an entry only once all three components have arrived. */
        s_dacTriple[s_dacComponent] = value & 0x3F;
        if (++s_dacComponent == 3) {
            memcpy(g_vga.dac[s_dacWriteIndex], s_dacTriple, 3);
            s_dacComponent = 0;
            s_dacWriteIndex++;
            g_vga.dacDirty = 1;
        }
        break;
    default:
        fprintf(stderr, "vga: unhandled out %03Xh <- %02Xh\n", port, value);
        break;
    }
}

void bak_port_out16(uint16_t port, uint16_t value) {
    /* outpw(index_port, data << 8 | index): an index write followed by a data write. */
    bak_port_out8(port, (uint8_t)(value & 0xFF));
    bak_port_out8((uint16_t)(port + 1), (uint8_t)(value >> 8));
}

uint8_t bak_vga_sequencer(uint8_t reg) {
    return g_vga.seq[reg & 7];
}

uint16_t vga_crtc_start_address(void) {
    return (uint16_t)(g_vga.crtc[0x0C] << 8 | g_vga.crtc[0x0D]);
}

/* ---- display memory through the graphics controller -------------------------------- */

uint8_t bak_vga_read(uint16_t seg, uint16_t off) {
    uint8_t *p = bak_vram_window(seg, off, 0);
    if (p == NULL)
        return 0xFF;
    memcpy(g_bakVgaLatch, p, 4);
    if (!(g_vga.gc[5] & 0x08))
        return g_bakVgaLatch[g_vga.gc[4] & 3];
    /* Read mode 1: a bit is set where the pixel's colour matches the colour compare
     * register in the planes colour don't care selects. */
    uint8_t result = 0xFF;
    for (unsigned plane = 0; plane < 4; plane++) {
        if (g_vga.gc[7] & (1u << plane)) {
            uint8_t want = g_vga.gc[2] & (1u << plane) ? 0xFF : 0x00;
            result &= (uint8_t)~(g_bakVgaLatch[plane] ^ want);
        }
    }
    return result;
}

void bak_vga_write(uint16_t seg, uint16_t off, uint8_t value) {
    uint8_t *p = bak_vram_window(seg, off, 0);
    if (p == NULL)
        return;
    unsigned mode = g_vga.gc[5] & 3;
    unsigned mapMask = g_vga.seq[2] & 0x0F;
    if (mode == 1) {
        for (unsigned plane = 0; plane < 4; plane++)
            if (mapMask & (1u << plane))
                p[plane] = g_bakVgaLatch[plane];
        return;
    }
    unsigned rotate = g_vga.gc[3] & 7;
    unsigned op = (g_vga.gc[3] >> 3) & 3;
    uint8_t bitMask = g_vga.gc[8];
    if (mode == 0 || mode == 3)
        value = (uint8_t)(value >> rotate | value << (8 - rotate));
    if (mode == 3)
        bitMask &= value;
    for (unsigned plane = 0; plane < 4; plane++) {
        if (!(mapMask & (1u << plane)))
            continue;
        uint8_t setReset = g_vga.gc[0] & (1u << plane) ? 0xFF : 0x00;
        uint8_t v;
        if (mode == 0)
            v = g_vga.gc[1] & (1u << plane) ? setReset : value;
        else if (mode == 2)
            v = value & (1u << plane) ? 0xFF : 0x00;
        else
            v = setReset;
        if (op == 1)
            v &= g_bakVgaLatch[plane];
        else if (op == 2)
            v |= g_bakVgaLatch[plane];
        else if (op == 3)
            v ^= g_bakVgaLatch[plane];
        p[plane] = (uint8_t)((v & bitMask) | (g_bakVgaLatch[plane] & ~bitMask));
    }
}

/* ---- BIOS mode set ------------------------------------------------------------------ */

void bak_int10_set_mode(uint8_t mode) {
    const ModeRegs *m;
    switch (mode) {
    case 0x13:
        m = &s_mode13;
        break;
    case 0x10:
        m = &s_mode10;
        break;
    default: {
        char what[40];
        snprintf(what, sizeof what, "INT 10h video mode %02Xh", mode);
        bak_unimplemented(what, __FILE__, __LINE__);
    }
    }
    memset(g_vga.seq, 0, sizeof g_vga.seq);
    memset(g_vga.crtc, 0, sizeof g_vga.crtc);
    memset(g_vga.gc, 0, sizeof g_vga.gc);
    memset(g_vga.attr, 0, sizeof g_vga.attr);
    memcpy(g_vga.seq, m->seq, sizeof m->seq);
    memcpy(g_vga.crtc, m->crtc, sizeof m->crtc);
    memcpy(g_vga.gc, m->gc, sizeof m->gc);
    memcpy(g_vga.attr, m->attr, sizeof m->attr);
    g_vga.misc = m->misc;
    g_vga.pelMask = 0xFF;
    s_seqIndex = s_crtcIndex = s_gcIndex = 0;
    s_attrIndex = 0x20; /* the BIOS ends by turning the display on (palette address source) */
    s_attrFlipFlop = 0;
    /* The BIOS clears display memory; the DAC keeps its colours (a known divergence: the
     * BIOS loads its default palette). */
    memset(g_bakVram, 0, sizeof g_bakVram);
}

/* The BIOS's attribute-controller calls end by writing index 20h (display on), which
 * leaves the flip-flop expecting data. */
void bak_int10_palette_paging(uint8_t bl, uint8_t bh) {
    (void)bak_port_in8(0x3DA);
    bak_port_out8(0x3C0, 0x10);
    uint8_t mode = bak_port_in8(0x3C1);
    if (bl == 0) { /* select the paging mode: bit 7 of the mode control register */
        mode = (uint8_t)(bh ? mode | 0x80 : mode & 0x7F);
        bak_port_out8(0x3C0, mode);
    } else { /* select a page: the colour select register */
        bak_port_out8(0x3C0, mode);
        bak_port_out8(0x3C0, 0x14);
        bak_port_out8(0x3C0, (uint8_t)((mode & 0x80 ? bh : bh << 2) & 0x0F));
    }
    bak_port_out8(0x3C0, 0x20);
}

void bak_int10_set_border(uint8_t color) {
    (void)bak_port_in8(0x3DA);
    bak_port_out8(0x3C0, 0x11);
    bak_port_out8(0x3C0, color);
    bak_port_out8(0x3C0, 0x20);
}

/* ---- the differential tests' view ----------------------------------------------------- */

void vga_regs_snapshot(uint8_t out[VGA_REGS_SNAPSHOT]) {
    uint8_t *p = out;
    memcpy(p, g_vga.seq, 8), p += 8;
    memcpy(p, g_vga.crtc, 32), p += 32;
    memcpy(p, g_vga.gc, 16), p += 16;
    memcpy(p, g_vga.attr, 32), p += 32;
    *p++ = g_vga.misc;
    *p++ = g_vga.pelMask;
    *p++ = s_seqIndex;
    *p++ = s_crtcIndex;
    *p++ = s_gcIndex;
    *p++ = s_attrIndex;
    *p++ = (uint8_t)s_attrFlipFlop;
    memcpy(p, g_bakVgaLatch, 4);
}

void vga_regs_restore(const uint8_t in[VGA_REGS_SNAPSHOT]) {
    const uint8_t *p = in;
    memcpy(g_vga.seq, p, 8), p += 8;
    memcpy(g_vga.crtc, p, 32), p += 32;
    memcpy(g_vga.gc, p, 16), p += 16;
    memcpy(g_vga.attr, p, 32), p += 32;
    g_vga.misc = *p++;
    g_vga.pelMask = *p++;
    s_seqIndex = *p++;
    s_crtcIndex = *p++;
    s_gcIndex = *p++;
    s_attrIndex = *p++;
    s_attrFlipFlop = *p++;
    memcpy(g_bakVgaLatch, p, 4);
}
