/*
 * POLYRAST.ASM in C: the scaled sprite blit, the textured quad blit ("Gouraud" in the
 * names) and the edge stepper they share.
 *
 * An edge stepper is a pair of 16.16 values, PolyEdge { nAcc, nStep }: the start and the
 * end go into the high words, and polyrast_edge_init_step turns them into a start and
 * a per-step increment. The scaled blit draws a column-major sprite (RLE-packed ones are
 * unpacked into g_pMainScratchBuf first) one screen column at a time, in Mode Y: each
 * column is one plane, chosen through the sequencer's map mask, as the original does.
 *
 * The remap mode looks its colours up with XLAT in POLYRAST's code segment, at the
 * offset in g_polyRasterState.nRemapTableOff: the fog and shadow tables live there.
 * Natively they are g_polyrastTables, which holds that part of the segment, from
 * FOG_REMAP_TAB_OFF on.
 */
#include <stdint.h>

#include "bak_video.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "gfx169d.h"
#include "SRC/GAME/GMAIN.H"
#include "SRC/GAME/MAINDATA.H"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GFX/RASTER/GOURAUD.H"
#include "SRC/GFX/RASTER/POLYRAST.H"
#include "bak_game_end.h"

typedef uint8_t u8;

#define TABLES_SIZE 0xf00

unsigned char g_polyrastTables[TABLES_SIZE];

static u16 hi16(s32 v) {
    return (u16)((u32)v >> 16);
}

static void set_hi16(s32 *v, u16 hi) {
    *v = (s32)(((u32)*v & 0xffffu) | (u32)hi << 16);
}

/* edge[0] is the accumulator, edge[1] the step; their high words hold the start and the
 * end. The step becomes (end - start) / steps. The accumulator's fraction depends on the
 * quotient's sign: a negative quotient's own low word, otherwise -quotient's low word, or
 * 8000h when -quotient has no bits from 15 up (a quotient of 0). The original tests the
 * sign with JNS right after IDIV, whose flags x86 leaves undefined: DOSBox Staging sets
 * them from the quotient, as the code means (and as the Blue Wheel Inn's mirrored patrons
 * show), while Unicorn leaves them as AND ECX,0FFFFh left them, never negative; the
 * differential harness models DOSBox's (harness/machine.py). */
void polyrast_edge_init_step(s32 *edge, u16 steps) {
    u32 acc = (u32)edge[0] & 0xffff0000u;
    u32 end = (u32)edge[1] & 0xffff0000u;
    edge[0] = (s32)acc;
    if ((s16)steps <= 0) {
        edge[1] = 0;
        return;
    }
    s32 quotient = (s32)(end - acc) / (s32)steps;
    edge[1] = quotient;
    if (quotient < 0) {
        edge[0] = (s32)(acc | ((u32)quotient & 0xffffu));
        return;
    }
    u32 neg = (u32)-quotient;
    edge[0] = (s32)(acc | ((neg & 0xffff8000u) != 0 ? (neg & 0xffffu) : 0x8000u));
}

/* A column's walk down the sprite: LODSB, then the source moves on by the y step's
 * integer part plus the carry of an 8-bit fraction. The fraction grows by the step's
 * bits 8-15 and starts from the y accumulator's top byte, as the original loads it, so
 * a flipped column can end a byte before its first row: before the data, SI wraps to
 * the end of its segment. Past the memory the port has there, a read gives 0. */
typedef struct Walk {
    const u8 *seg; /* the source segment's base */
    u32 extent;    /* its bytes */
    u16 si;
    u16 skip; /* integer step - 1 */
    u8 frac, inc;
} Walk;

static u8 next_pixel(Walk *w) {
    u8 al = w->si < w->extent ? w->seg[w->si] : 0;
    unsigned sum = (unsigned)w->frac + w->inc;
    w->frac = (u8)sum;
    w->si = (u16)(w->si + 1 + w->skip + (sum >> 8));
    return al;
}

typedef struct Target {
    u16 seg;
    unsigned plane;
} Target;

static void put(const Target *t, u16 di, u8 value) {
    u8 *p = bak_vram_window(t->seg, di, t->plane);
    if (p != NULL)
        *p = value;
}

/* The four column loops. Each starts a row above the column, counts h + 1 down (DEC CX
 * and JLE after a transparent pixel, LOOP after a written one) and returns. */
static void column_copy(const Target *t, Walk *w, u16 di, s16 h) {
    u16 cx = (u16)(h + 1);
    di = (u16)(di - 0x50);
row:
    di = (u16)(di + 0x50);
    if ((s16)--cx <= 0)
        return;
pixel: {
    u8 al = next_pixel(w);
    if (al == 0)
        goto row;
    put(t, di, al);
    di = (u16)(di + 0x50);
    if (--cx != 0)
        goto pixel;
}
}

static void column_fill(const Target *t, Walk *w, u16 di, s16 h, u8 colour) {
    u16 cx = (u16)(h + 1);
    di = (u16)(di - 0x50);
row:
    di = (u16)(di + 0x50);
    if ((s16)--cx <= 0)
        return;
pixel:
    if (next_pixel(w) == 0)
        goto row;
    put(t, di, colour);
    di = (u16)(di + 0x50);
    if (--cx != 0)
        goto pixel;
}

/* Opaque pixels alternate: the colour, then the sprite's own; a transparent one starts
 * over with the colour. */
static void column_outline(const Target *t, Walk *w, u16 di, s16 h, u8 colour) {
    u16 cx = (u16)(h + 1);
    di = (u16)(di - 0x50);
row:
    di = (u16)(di + 0x50);
    if ((s16)--cx <= 0)
        return;
pixel: {
    u8 al = next_pixel(w);
    if (al == 0)
        goto row;
    put(t, di, colour);
    di = (u16)(di + 0x50);
    if ((s16)--cx <= 0)
        return;
    al = next_pixel(w);
    if (al == 0)
        goto row;
    put(t, di, al);
    di = (u16)(di + 0x50);
    if (--cx != 0)
        goto pixel;
}
}

static u8 remap(u16 table, u8 al) {
    unsigned at = (u16)(table + al) - (unsigned)FOG_REMAP_TAB_OFF;
    if (at >= TABLES_SIZE)
        bak_unimplemented("POLYRAST.ASM: a remap table outside the fog and cursor tables "
                          "(the original reads its own code)",
                          __FILE__, __LINE__);
    return g_polyrastTables[at];
}

static void column_remap(const Target *t, Walk *w, u16 di, s16 h, u16 table) {
    u16 cx = (u16)(h + 1);
    di = (u16)(di - 0x50);
row:
    di = (u16)(di + 0x50);
    if ((s16)--cx <= 0)
        return;
pixel: {
    u8 al = next_pixel(w);
    if (al == 0)
        goto row;
    put(t, di, remap(table, al));
    di = (u16)(di + 0x50);
    if (--cx != 0)
        goto pixel;
}
}

/* RLE: n | 80h then a byte: n copies of it; n: n bytes as they are; 0 ends. Unpacked
 * into g_pMainScratchBuf. */
static void unpack_rle(const u8 *seg, u16 si) {
    u8 *out = g_pMainScratchBuf;
    for (;;) {
        u8 n = seg[si++];
        if (n & 0x80) {
            u8 value = seg[si++];
            for (n &= 0x7f; n != 0; n--)
                *out++ = value;
        } else if (n == 0) {
            return;
        } else {
            for (; n != 0; n--)
                *out++ = seg[si++];
        }
    }
}

/* Clips one axis: start, size and the stepper move to the clip range's first pixel, and
 * the size stops at its last. Returns 0 when nothing of the axis is inside. */
static int clip_axis(s16 *start, s16 *size, PolyEdge *e, s16 lo, s16 hi) {
    if (*start > hi || (s16)(*start + *size) < lo)
        return 0;
    s16 cut = (s16)(lo - *start);
    if (cut > 0) {
        *start = lo;
        e->nAcc = (s32)((u32)e->nAcc + (u32)e->nStep * (u16)cut);
        *size = (s16)(*size - cut);
    }
    cut = (s16)(*start + *size - 1 - hi);
    if (cut > 0)
        *size = (s16)(*size - cut);
    return 1;
}

/* flags: 1 flip vertically, 2 horizontally (a negative h or w flips too), 4 fill with
 * colour, 8 outline with colour; otherwise copy, through the remap table if one is set. */
void polyrast_spr_scaled_blit_planar(void *sprite, s16 x, s16 y, u16 flags, s16 w, s16 h,
                                     s16 colour) {
    ImageRecord *s = sprite;
    PolyEdge *ex = &g_polyRasterState.pEdge[0], *ey = &g_polyRasterState.pEdge[1];
    u16 start, end;

    if (w < 0) {
        w = (s16)-w;
        flags ^= 2;
    }
    if (h < 0) {
        h = (s16)-h;
        flags ^= 1;
    }
    start = 0;
    end = (u16)(s->nWidth - 1);
    if (flags & 2) {
        start = end;
        end = 0;
    }
    set_hi16(&ex->nAcc, start);
    set_hi16(&ex->nStep, end);
    polyrast_edge_init_step(&ex->nAcc, (u16)(w - 1));
    u16 height = (u16)s->nHeight;
    start = 0;
    end = (u16)(height - 1);
    if (flags & 1) {
        start = end;
        end = 0;
    }
    set_hi16(&ey->nAcc, start);
    set_hi16(&ey->nStep, end);
    polyrast_edge_init_step(&ey->nAcc, (u16)(h - 1));

    Target t = {g_graphics_context.wGfxBlitDstPage, 0};
    const u8 *seg = s->pImageBase;
    u32 extent = s->nImageExtent;
    u16 data = s->wImageOff;
    if (s->wUnk4 & 0x80) {
        unpack_rle(seg, data);
        seg = MK_FP(FP_SEG(g_pMainScratchBuf), 0);
        extent = bak_seg_extent(FP_SEG(g_pMainScratchBuf));
        data = FP_OFF(g_pMainScratchBuf);
    }

    if (g_graphics_context.bClip_enabled & 0x0f) {
        const ClipRect *c = &g_graphics_context.clip;
        if (!clip_axis(&x, &w, ex, c->xmin, c->xmax) || !clip_axis(&y, &h, ey, c->ymin, c->ymax))
            return;
    }
    if (w <= 0 || h <= 0)
        return;

    u16 di = (u16)(y * 0x50 + ((u16)x >> 2));
    u8 mask = (u8)(1 << (x & 3));
    bak_port_out8(0x3c4, 2);
    for (;;) {
        Walk walk = {seg,
                     extent,
                     (u16)(data + (u16)(hi16(ex->nAcc) * height) + hi16(ey->nAcc)),
                     (u16)(hi16(ey->nStep) - 1),
                     (u8)((u32)ey->nAcc >> 24),
                     (u8)((u32)ey->nStep >> 8)};
        bak_port_out8(0x3c5, mask);
        t.plane = mask == 1 ? 0 : mask == 2 ? 1 : mask == 4 ? 2 : 3;
        if (flags & 4)
            column_fill(&t, &walk, di, h, (u8)colour);
        else if (flags & 8)
            column_outline(&t, &walk, di, h, (u8)colour);
        else if (g_polyRasterState.nRemapTableOff != 0)
            column_remap(&t, &walk, di, h, (u16)g_polyRasterState.nRemapTableOff);
        else
            column_copy(&t, &walk, di, h);
        ex->nAcc = (s32)((u32)ex->nAcc + (u32)ex->nStep);
        mask <<= 1;
        if (!(mask & 0x0f)) {
            mask = 1;
            di++;
        }
        if (--w == 0)
            return;
    }
}

/*
 * The "Gouraud" blit draws an image onto a quad whose left and right sides are vertical
 * (xs[0] == xs[3], xs[1] == xs[2]) and whose ends come in order (ys[0] <= ys[3], ys[1] <=
 * ys[2]); any other quad goes to GOURAUD.ASM's general rasterizer. page_ptr is an
 * ImageRecord (segment, offset, flags, width, height) of column-major pixels. Each screen
 * column runs from the top edge (ys[0] to ys[1]) to the bottom one (ys[3] to ys[2]) and
 * shows one source column, stepped across the image's width; its rows step down the
 * image's height in 8.8 fixed point. A quad given right to left is turned round in place
 * and drawn mirrored. With clipping on, a quad with a corner above or below the clip
 * rectangle has each column clipped; the others are only clipped left and right.
 */
static void quad_rows(const Target *t, Walk *w, u16 di, u16 n, u16 table) {
    for (; n != 0; n--, di = (u16)(di + 0x50)) {
        u8 al = next_pixel(w);
        if (al != 0)
            put(t, di, table != 0 ? remap(table, al) : al);
    }
}

static void swap16(s16 *a, s16 *b) {
    s16 v = *a;
    *a = *b;
    *b = v;
}

static void edges_advance(PolyEdge *e, s16 n, u16 by) {
    for (s16 i = 0; i < n; i++)
        e[i].nAcc = (s32)((u32)e[i].nAcc + (u32)e[i].nStep * by);
}

void polyrast_gouraud_blit_clipped(ImageRecord *texture, s16 *xs, s16 *ys) {
    if (xs[0] != xs[3] || xs[1] != xs[2] || ys[0] > ys[3] || ys[1] > ys[2]) {
        gfx_gouraud_blit_quad(texture, xs, ys);
        return;
    }
    int mirrored = 0;
    u16 count = (u16)(xs[1] - xs[0]);
    if (count == 0)
        return;
    if (!(xs[1] > xs[0])) {
        swap16(&xs[0], &xs[1]);
        swap16(&ys[0], &ys[1]);
        swap16(&xs[2], &xs[3]);
        swap16(&ys[2], &ys[3]);
        count = (u16)-count;
        mirrored = 1;
    }
    u16 x = (u16)xs[0];

    const ClipRect *c = &g_graphics_context.clip;
    int clipping = (g_graphics_context.bClip_enabled & 0x0f) != 0;
    int clipRows = 0;
    for (int i = 0; clipping && i < 4; i++)
        if (c->ymin > ys[i] || c->ymax < ys[i])
            clipRows = 1;

    PolyEdge *e = g_polyRasterState.pEdge; /* top, bottom, source column */
    set_hi16(&e[0].nAcc, (u16)ys[0]);
    set_hi16(&e[0].nStep, (u16)ys[1]);
    polyrast_edge_init_step(&e[0].nAcc, count);
    set_hi16(&e[1].nAcc, (u16)ys[3]);
    set_hi16(&e[1].nStep, (u16)ys[2]);
    polyrast_edge_init_step(&e[1].nAcc, count);
    u16 height = (u16)texture->nHeight;
    u16 first = 0, last = (u16)(texture->nWidth - 1);
    if (mirrored) {
        first = last;
        last = 0;
    }
    set_hi16(&e[2].nAcc, first);
    set_hi16(&e[2].nStep, last);
    polyrast_edge_init_step(&e[2].nAcc, count);

    const u8 *src = texture->pImageBase;
    u32 extent = texture->nImageExtent;
    u16 base = texture->wImageOff;

    if (clipping) {
        if ((s16)x > c->xmax || (s16)(x + count) < c->xmin)
            return;
        if (c->xmin > (s16)x) {
            u16 cut = (u16)(c->xmin - x);
            x = (u16)c->xmin;
            edges_advance(e, 3, cut);
            count = (u16)(count - cut);
        }
        if ((s16)(x + count - 1) > c->xmax)
            count = (u16)(count - (u16)(x + count - 1 - c->xmax));
    }
    if ((s16)count <= 0)
        return;

    Target t = {g_graphics_context.wGfxBlitDstPage, 0};
    u16 table = (u16)g_polyRasterState.nRemapTableOff;
    do {
        u16 top = hi16(e[0].nAcc), bottom = hi16(e[1].nAcc);
        u16 di = (u16)(top * 0x50 + (x >> 2));
        t.plane = x & 3;
        bak_port_out16(0x3c4, (u16)(0x100u << (x & 3) | 2));
        s16 rows = (s16)(bottom - top);
        u16 skip = 0, cut = 0;
        if (clipRows) {
            if (c->ymin > (s16)top)
                skip = (u16)(c->ymin - top);
            if ((s16)(bottom - 1) > c->ymax)
                cut = (u16)(bottom - 1 - c->ymax);
        }
        if (rows > 1) {
            /* IDIV: (height - 1, as a byte) * 256 over the rows, at most 7F80h */
            u16 step = (u16)((u16)((u8)((u8)height - 1) << 8) / (u16)rows);
            Walk w = {src,
                      extent,
                      (u16)(base + (u16)(hi16(e[2].nAcc) * height)),
                      (u16)((step >> 8) - 1),
                      0x80,
                      (u8)step};
            if (clipRows && (s16)rows <= (s16)cut) {
                rows = 0;
            } else if (clipRows) {
                rows = (s16)(rows - cut);
                if (skip != 0) {
                    if ((s16)rows <= (s16)skip) {
                        rows = 0;
                    } else {
                        rows = (s16)(rows - skip);
                        for (u16 n = skip; n != 0; n--, di = (u16)(di + 0x50))
                            next_pixel(&w);
                    }
                }
            }
            quad_rows(&t, &w, di, (u16)rows, table);
        }
        edges_advance(e, 3, 1);
        x++;
    } while (--count != 0);
}
