/*
 * POLYRAST.ASM in C: the scaled sprite blit and the edge stepper it shares with the
 * Gouraud quad blit (not ported yet: it traps).
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
 * end. The step becomes (end - start) / steps. The accumulator's fraction is set from a
 * test of the sign meant for the quotient, but JNS reads the flags IDIV leaves, which
 * are AND ECX,0FFFFh's: never negative. So it is -quotient's low word, or 8000h when
 * -quotient has no bits from 15 up. */
void polyrast_edge_init_step(s32 *edge, u16 steps) {
    u32 acc = (u32)edge[0] & 0xffff0000u;
    u32 end = (u32)edge[1] & 0xffff0000u;
    edge[0] = (s32)acc;
    if ((s16)steps <= 0) {
        edge[1] = 0;
        return;
    }
    s32 quotient = (s32)(end - acc) / (s32)steps;
    u32 neg = (u32)-quotient;
    edge[1] = quotient;
    edge[0] = (s32)(acc | ((neg & 0xffff8000u) != 0 ? (neg & 0xffffu) : 0x8000u));
}

void polyrast_gouraud_blit_clipped(u16 *page_ptr, s16 *xs, s16 *ys) {
    (void)page_ptr;
    (void)xs;
    (void)ys;
    BAK_UNIMPLEMENTED("POLYRAST.ASM: polyrast_gouraud_blit_clipped");
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
    u16 srcSeg = s->wImageData;
    u16 data = s->wImageOff;
    if (s->wUnk4 & 0x80) {
        unpack_rle(MK_FP(srcSeg, 0), data);
        srcSeg = FP_SEG(g_pMainScratchBuf);
        data = FP_OFF(g_pMainScratchBuf);
    }
    const u8 *seg = MK_FP(srcSeg, 0);
    u32 extent = bak_seg_extent(srcSeg);

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
