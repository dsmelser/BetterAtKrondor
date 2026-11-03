/*
 * VGAFILL.ASM in C: the sky's flat bands across the 3-D view. Both fill rows of planar
 * bytes 3..76, pixels 13..306: byte 3 under planes 1-3, bytes 4..75 under all four and
 * byte 76 under planes 0-2, in three passes that each go from the bottom row up. They
 * write as the CPU does (STOSB, STOSW) into the page g_graphics_context.wGfxBlitDstPage,
 * through the graphics controller as it stands. The column strip shades each row from a
 * ramp, starting at the bottom row; the band fills with one colour.
 *
 * A row count of 0 runs the original's LOOPs 65536 times, as it does here.
 */
#include "../platform/scene.h"
#include "bak_video.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GFX/RASTER/VGAFILL.H"
#include "bak_game_end.h"

#define ROW_BYTES 0x50
#define MID_WORDS 0x24

/* The strip's ramp of palette indices, from the bottom row up (the GPU's view reads it too:
 * scene.h). A taller strip would read the code after it in the original. */
const unsigned char g_skyShadeRamp[34] = {
    0xd0, 0xd0, 0xd0, 0xd1, 0xd2, 0xd2, 0xd3, 0xd3, 0xd4, 0xd5, 0xd5, 0xd6,
    0xd6, 0xd6, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7,
    0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7, 0xd7,
};

/* OUT DX,AX with DX = 3C4h: the sequencer's index 2, the map mask. */
static void map_mask(unsigned char mask) {
    bak_port_out16(0x3C4, (u16)(mask << 8 | 2));
}

/* Row k's fill byte from the bottom: the strip's ramp entry, or the band's colour. */
static unsigned char row_byte(int shaded, unsigned char color, u16 k) {
    if (!shaded)
        return color;
    if (k >= sizeof g_skyShadeRamp)
        BAK_UNIMPLEMENTED("vgaplanar_clear_column_strip taller than its ramp (34 rows): the "
                          "original reads its own code for the colours");
    return g_skyShadeRamp[k];
}

/* An edge byte in each row, bottom up: STOSB, then 51h back. */
static void fill_edge(u16 page, u16 di, u16 rows, int shaded, unsigned char color) {
    u16 k = 0;
    do {
        bak_vga_write(page, di, row_byte(shaded, color, k));
        di = (u16)(di + 1 - (ROW_BYTES + 1));
        k++;
    } while (--rows != 0);
}

static void fill_rows(u16 base, u16 rows, int shaded, unsigned char color) {
    u16 page = g_graphics_context.wGfxBlitDstPage;
    u16 si = (u16)(base + 4);
    u16 n = rows;
    u16 k = 0;

    map_mask(0x0e);
    fill_edge(page, (u16)(base + 3), rows, shaded, color);

    map_mask(0x0f);
    do {
        unsigned char b = row_byte(shaded, color, k);
        u16 di = si;
        for (int w = 0; w < MID_WORDS; w++) { /* REP STOSW: the low byte, then the high */
            bak_vga_write(page, di, b);
            bak_vga_write(page, (u16)(di + 1), b);
            di = (u16)(di + 2);
        }
        si = (u16)(si - ROW_BYTES);
        k++;
    } while (--n != 0);

    map_mask(0x07);
    fill_edge(page, (u16)(base + 0x4c), rows, shaded, color);
}

void vgaplanar_clear_column_strip(s16 vga_base_offset, s16 row_count) {
    fill_rows((u16)vga_base_offset, (u16)row_count, 1, 0);
}

/* The band's rows start_row.. start_row + row_count - 1, from the last one's offset up. */
void vgaplanar_fill_band(s16 start_row, s16 row_count, s16 color) {
    u16 bottom = (u16)(start_row + row_count - 1);
    fill_rows((u16)(bottom * ROW_BYTES), (u16)row_count, 0, (unsigned char)color);
}
