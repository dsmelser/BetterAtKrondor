/*
 * VGA register file behind the game's inp/outp/outpw calls.
 *
 * Keeps the sequencer, CRTC, graphics-controller, attribute-controller and DAC
 * state the original reads back or relies on (CRTC start address, line compare
 * and pel panning drive the hardware scrolling in UI/SCROLL.C). Vertical retrace
 * comes from a virtual 70 Hz refresh: Input Status 1 (3DAh) reports it, and
 * bak_vga_wait_retrace waits for it and shows the frame.
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

VgaRegs g_vga = {.pelMask = 0xFF};
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
    uint64_t phase = frame_phase(SDL_GetTicksNS());
    uint8_t v = 0;
    if (phase < RETRACE_NS)
        v |= 0x08 | 0x01;
    else if ((phase / LINE_NS) % 2)
        v |= 0x01; /* display enable toggles once per scanline */
    s_attrFlipFlop = 0;
    return v;
}

void bak_vga_wait_retrace(int next) {
    if (g_platformFreeRunning)
        return;
    uint64_t now = SDL_GetTicksNS();
    uint64_t phase = frame_phase(now);
    if (next || phase >= RETRACE_NS) {
        uint64_t target = now - phase + FRAME_NS;
        /* Interrupts keep coming while the game waits for retrace. */
        for (;;) {
            bak_poll();
            now = SDL_GetTicksNS();
            if (now >= target)
                break;
            SDL_DelayPrecise(target - now < 250000 ? target - now : 250000);
        }
    }
    video_present();
}

uint8_t bak_port_in8(uint16_t port) {
    uint8_t value;
    if (machine_port_in8(port, &value) || mpu401_port_in8(port, &value))
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
    if (machine_port_out8(port, value) || mpu401_port_out8(port, value))
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
