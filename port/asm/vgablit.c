/*
 * VGABLIT.ASM in C: span fillers that copy a background texture instead of a colour. The
 * game swaps them into the renderer's span slot (27): dialog frames lay their parchment
 * with vgablit_planar_runs_untiled, the sky its panorama with vgablit_planar_runs (and,
 * in the line slot, vgablit_span_emit_textured).
 *
 * They take the span table (the first row, the row count, then x0 and x1 of each row)
 * and copy each run through the VGA latches (write mode 1) from the texture at AC00h in
 * display memory, one texture row per screen row. The texture rows: untiled, from
 * g_vgablit_si_base on, g_vgablit_si_stride apart and wrapped by g_vgablit_si_mask; tiled,
 * from (first - g_vgablit_row_sub) * stride + base when g_vgablit_row_count is 0, else
 * from base, back to base every row_count rows.
 *
 * vga_planar_fill_rects has no caller in the game and traps.
 */
#include "../drivers/vga.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "vgablit.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GFX/RASTER/VGABLIT.H"
#include "bak_game_end.h"

#define TEXTURE_SEG 0xAC00

static const unsigned char s_lastMask[4] = {0x0f, 0x01, 0x03, 0x07};

void vga_planar_fill_rects(void) {
    BAK_UNIMPLEMENTED("VGABLIT.ASM: vga_planar_fill_rects (no caller in the game)");
}

static u16 word_at(const unsigned char *p) {
    return (u16)(p[0] | p[1] << 8);
}

/* MOVSB in write mode 1: the read loads the latches from the texture, the write stores
 * them into the planes of the map mask. */
static void movsb(u16 dst, u16 *di, u16 *si, unsigned mask) {
    vga_read_latches(TEXTURE_SEG, *si);
    vga_write_latches(dst, *di, mask);
    (*si)++;
    (*di)++;
}

static unsigned map_mask(unsigned char mask) {
    bak_port_out8(0x3C5, mask);
    return mask;
}

/* One run, x0..x1, from texture offset si to the row at di. A run within one 4-pixel
 * group (bits 2-11 of the ends agree) is one byte under both edge masks; otherwise none
 * when x1 <= x0. */
static void copy_run(u16 dst, u16 di, u16 si, u16 x0, u16 x1) {
    unsigned mask;
    if (((x0 ^ x1) & 0x0ffc) == 0) {
        u16 at = x0 >> 2;
        si = (u16)(si + at);
        di = (u16)(di + at);
        mask = map_mask((unsigned char)(0x0f << (x0 & 3)) & s_lastMask[(x1 + 1) & 3]);
        movsb(dst, &di, &si, mask);
        return;
    }
    if ((s16)x1 <= (s16)x0)
        return;
    u16 rest = (u16)(x1 - x0 + (x0 & 3) - 3);
    u16 at = x0 >> 2;
    si = (u16)(si + at);
    di = (u16)(di + at);
    mask = map_mask((unsigned char)(0x0f << (x0 & 3)));
    movsb(dst, &di, &si, mask);
    mask = map_mask(0x0f);
    for (u16 n = rest >> 2; n != 0; n--)
        movsb(dst, &di, &si, mask);
    if (rest & 3) {
        unsigned rot = rest & 3;
        mask = map_mask((unsigned char)(0xf0 << rot | 0xf0 >> (8 - rot)));
        movsb(dst, &di, &si, mask);
    }
}

static void runs(const unsigned char *spans, int tiled) {
    u16 dst = g_graphics_context.wGfxBlitDstPage;
    u16 first = word_at(spans);
    u16 di = vga_row_offset((s16)first);
    u16 si, rowsLeft = (u16)g_vgablit_row_count;
    if (!tiled || rowsLeft != 0)
        si = (u16)g_vgablit_si_base;
    else
        si = (u16)((u16)(first - g_vgablit_row_sub) * (u16)g_vgablit_si_stride + g_vgablit_si_base);
    bak_port_out16(0x3CE, 0x4105);
    bak_port_out8(0x3C4, 2);
    u16 count = word_at(spans + 2);
    const unsigned char *run = spans + 4;
    do {
        copy_run(dst, di, si, word_at(run), word_at(run + 2));
        run += 4;
        di = (u16)(di + 0x50);
        si = (u16)(si + g_vgablit_si_stride);
        if (!tiled) {
            si &= (u16)g_vgablit_si_mask;
        } else if (--rowsLeft == 0) {
            si = (u16)g_vgablit_si_base;
            rowsLeft = (u16)g_vgablit_row_count;
        }
    } while (--count != 0);
    bak_port_out16(0x3CE, 0x4005);
}

void vgablit_planar_runs(const unsigned char *spans) {
    runs(spans, 1);
}

/* The line slot's stand-in: a horizontal line (y0 == y1) is a one-row run; others are
 * dropped. */
void vgablit_span_emit_textured(u16 seg, u16 x0, u16 y0, u16 x1, u16 y1) {
    (void)seg;
    if (y0 != y1)
        return;
    unsigned char span[8] = {(unsigned char)y0,
                             (unsigned char)(y0 >> 8),
                             1,
                             0,
                             (unsigned char)x0,
                             (unsigned char)(x0 >> 8),
                             (unsigned char)x1,
                             (unsigned char)(x1 >> 8)};
    vgablit_planar_runs(span);
}

void vgablit_planar_runs_untiled(const unsigned char *spans) {
    runs(spans, 0);
}
