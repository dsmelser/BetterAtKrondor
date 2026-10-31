/*
 * VGA register file behind the game's inp/outp/outpw calls.
 *
 * Keeps the sequencer, CRTC, graphics-controller, attribute-controller and DAC
 * state the original reads back or relies on (CRTC start address, line compare
 * and pel panning drive the hardware scrolling in UI/SCROLL.C). Input status
 * (3DAh) is derived from a 70 Hz virtual refresh so retrace waits terminate.
 */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bak_port.h"
#include "vgaregs.h"

VgaRegs g_vga;

static uint8_t s_crtcIndex;
static uint8_t s_seqIndex;
static uint8_t s_gcIndex;
static uint8_t s_attrIndex;
static int s_attrFlipFlop; /* 0 = next 3C0h write is an index, 1 = data */
static uint8_t s_dacWriteIndex, s_dacReadIndex;
static int s_dacComponent;

static uint8_t input_status_1(void) {
    /* 70 Hz refresh; vertical retrace for roughly the last 5% of each frame. */
    uint64_t ns = SDL_GetTicksNS();
    uint64_t frame = 1000000000ull / 70;
    uint64_t phase = ns % frame;
    uint8_t v = 0;
    if (phase > frame - frame / 20)
        v |= 0x08 | 0x01;
    else if ((phase / 31778) % 2)
        v |= 0x01; /* display-enable toggles once per scanline */
    s_attrFlipFlop = 0;
    return v;
}

uint8_t bak_port_in8(uint16_t port) {
    switch (port) {
    case 0x3DA:
        return input_status_1();
    case 0x3C5:
        return g_vga.seq[s_seqIndex & 7];
    case 0x3D5:
        return g_vga.crtc[s_crtcIndex & 0x1F];
    case 0x3CF:
        return g_vga.gc[s_gcIndex & 15];
    case 0x3C1:
        return g_vga.attr[s_attrIndex & 0x1F];
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
    case 0x3C7:
        s_dacReadIndex = value;
        s_dacComponent = 0;
        break;
    case 0x3C8:
        s_dacWriteIndex = value;
        s_dacComponent = 0;
        break;
    case 0x3C9:
        g_vga.dac[s_dacWriteIndex][s_dacComponent] = value & 0x3F;
        if (++s_dacComponent == 3) {
            s_dacComponent = 0;
            s_dacWriteIndex++;
        }
        g_vga.dacDirty = 1;
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

uint16_t vga_crtc_start_address(void) {
    return (uint16_t)(g_vga.crtc[0x0C] << 8 | g_vga.crtc[0x0D]);
}
