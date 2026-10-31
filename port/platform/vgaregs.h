/* vgaregs.h - emulated VGA register state (see vgaregs.c). */
#ifndef BAK_VGAREGS_H
#define BAK_VGAREGS_H

#include <stdint.h>

typedef struct VgaRegs {
    uint8_t seq[8];      /* sequencer (3C4h/3C5h): [2] = map mask */
    uint8_t crtc[32];    /* CRTC (3D4h/3D5h): [0Ch:0Dh] start address, [18h] line compare */
    uint8_t gc[16];      /* graphics controller (3CEh/3CFh): [4] read map, [8] bit mask */
    uint8_t attr[32];    /* attribute controller (3C0h): [13h] horizontal pel panning */
    uint8_t misc;        /* miscellaneous output (3C2h / 3CCh) */
    uint8_t pelMask;     /* DAC pixel mask (3C6h) */
    uint8_t dac[256][3]; /* DAC palette, 6 bits per component */
    int dacDirty;
} VgaRegs;

extern VgaRegs g_vga;

uint16_t vga_crtc_start_address(void);

#endif
