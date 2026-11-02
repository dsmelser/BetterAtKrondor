/*
 * ROTBLIT.ASM in C: a sprite drawn rotated and scaled (the TTM scripts' opcode A5A7h: the
 * shops' and taverns' animations, chapter 8's ending), one span of the screen at a time
 * through the VGA driver's slot 36.
 *
 * jmp_via_2f48 turns the sprite's rectangle into a quad. Its corners lie along two edges from
 * the origin (moved back to the sprite's centre when the half sizes are given), scale_x and
 * scale_y long at the angle (a 16-bit binary angle; the cosine and sine are Q14, the products
 * kept by their high words after a shift left by 2). The corner highest on the screen starts a
 * walk round the quad clockwise: the next corner, the bottom one and the one before the top.
 * The rotation's octant (which corner is on top) picks how a step across the sprite and a step
 * down it become source offsets, the four cases of each of four places the original jumps to
 * through tables of code addresses (the switches below).
 *
 *   - The quad's left edge, top to bottom through the corner before the top, gives each row's
 *     first x (stretch_walk_x_clipped into the Y source table, after the first row's number).
 *   - Steps across the quad's width at that corner's row give the source offsets of the row's
 *     pixels (the X source table), and two tables of which step each source row and column
 *     starts on.
 *   - Each row from the top corner to the bottom one is a span from the left edge's x, as many
 *     pixels as those tables say, clipped to the clip rectangle and drawn by slot 36 with the
 *     X source table from the first pixel not cut off and the row base the octant's case gives.
 *
 * The original keeps its scratch in a stack frame of 6CAh bytes and indexes its tables past
 * their ends in places (the row offsets at the sprite's height, and past their 200 words at a
 * stepper's far end). The translation keeps the frame as an array laid out
 * as the original's (s_frame, FW), whose contents persist between calls as the stack's do
 * between calls from the same depth, and the corners and the source tables in blocks laid out
 * as in DGROUP, so such reads find what the original's find. What the original reaches past
 * those (other DGROUP data, the caller's frame) stops the port with a message, as do counts
 * that would write there, and a quad with no corner above row 32000 draws nothing (the
 * original indexes its corners with whatever DX held).
 */
#include "../drivers/vga.h"
#include "bak_port.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/RASTER/VGARESET.H"
#include "SRC/GFX/SPRITE/ROTBLIT.H"
#include "SRC/GFX/SPRITE/STRBLIT.H"
#include "SRC/R3D/CORE/R3D.H"
#include "bak_game_end.h"

/* The frame: [bp-6CAh] up to the last argument's word, [bp+14h]. */
#define FRAME_LO 0x6ca
static s16 s_frame[(FRAME_LO + 0x16) / 2];
#define FW(off) s_frame[((off) + FRAME_LO) >> 1]

enum {
    F_NROWS = -0x6ca, /* the source's rows less 1 (its columns', the axes swapped) */
    F_NCOLS = -0x6c8, /* and the other way */
    F_STEPB = -0x6c6, /* the two steppers (StretchStep: four words) */
    F_STEPA = -0x6be,
    F_YSRC = -0x6b6,   /* the Y source table's cursor (natively an index into the tables) */
    F_OCTANT = -0x6b4, /* the octant, in the low byte */
    F_FILLUP = -0x6b2, /* how far the two step tables are filled; the loops' ends later */
    F_FILLDOWN = -0x6b0,
    F_T1 = -0x6ae,   /* the top's clamped row; the ratio's span; the first row drawn */
    F_T2 = -0x6ac,   /* the rows to the second corner; a stepper's place in a span */
    F_POSA = -0x6aa, /* the steppers' places along the width */
    F_POSB = -0x6a8,
    F_COS = -0x6a6,
    F_SIN = -0x6a4,
    F_ROWBASE = -0x6a2, /* a span's row base */
    F_ROWOFF = -0x6a0,  /* 200 words: k times the sprite's width */
    F_ROWEND = -0x510,  /* the row offset at the sprite's height */
    F_WIDTH = -0x50e,   /* the quad's width at the second corner's row, in steps */
    F_SYSIN = -0x50c,
    F_SYCOS = -0x50a,
    F_SXSIN = -0x508,
    F_SXCOS = -0x506,
    F_UP = -0x504,   /* 320 words: the step each source row starts on */
    F_DOWN = -0x284, /* 320 words: the step each source column starts on */
    F_OX = -4,
    F_OY = -2,
    A_X = 8,    /* the arguments the code writes or swaps */
    A_Y = 0x0a, /* reused as the row counter */
    A_SX = 0x0e,
    A_SY = 0x10,
};

#define YSRC 640 /* where the Y source table starts in the source tables */

/* A corner's x and y by its byte offset (0, 2, 4, 6; the second tie-break would make a top
 * corner of 6 into 8 if corners 2 and 3 lay level then, which no angle gets to: the block's next
 * words, as in DGROUP). */
#define VX(idx) g_anRotBlitState[(u16)(idx) >> 1]
#define VY(idx) g_anRotBlitState[4 + ((u16)(idx) >> 1)]

/* The k-th word of a table in the frame at [bp+off], within the frame as the original reads it. */
static s16 *frame_at(s16 off, s16 k) {
    s32 i = (s32)((off + FRAME_LO) >> 1) + k;
    if (i < 0 || i >= FRAME_LO / 2)
        BAK_UNIMPLEMENTED("ROTBLIT.ASM: a table indexed past jmp_via_2f48's frame");
    return &s_frame[i];
}

/* The source tables' k-th word (the X table's 640 words, then the Y table's 400). */
static s16 *source_at(u16 k) {
    if (k >= 640 + 400)
        BAK_UNIMPLEMENTED("ROTBLIT.ASM: a source table indexed past its end");
    return &g_aStretchSrcTables[k];
}

/* A Q14 product as the original keeps it: the high word of a times b shifted left by 2. */
static s16 q14(s16 a, s16 b) {
    return (s16)((u32)((s32)a * b) << 2 >> 16);
}

static s16 neg16(s16 v) {
    return (s16)(0u - (u16)v);
}

/* Fills count words of a frame table from k with v (REP STOSW). */
static void fill(s16 table, s16 k, u16 count, s16 v) {
    for (u16 i = 0; i < count; i++)
        *frame_at(table, (s16)(k + i)) = v;
}

static s16 row_off(s16 k) {
    return *frame_at(F_ROWOFF, k);
}

/* A stepper in the frame from 0 to end over steps (stretch_step_init: the position's high word
 * the start, the step's the end), and its next place: the position's high word, then the step
 * added in 32 bits. */
static void step_start(s16 at, s16 end, s16 steps) {
    StretchStep s;
    FW(at + 2) = 0;
    FW(at + 6) = end;
    for (int i = 0; i < 4; i++)
        s.w[i] = FW(at + 2 * i);
    stretch_step_init(&s, steps);
    for (int i = 0; i < 4; i++)
        FW(at + 2 * i) = s.w[i];
}

static s16 step_next(s16 at) {
    s16 place = FW(at + 2);
    u32 pos = (u16)FW(at) | (u32)(u16)FW(at + 2) << 16;
    u32 step = (u16)FW(at + 4) | (u32)(u16)FW(at + 6) << 16;
    pos += step;
    FW(at) = (s16)pos;
    FW(at + 2) = (s16)(pos >> 16);
    return place;
}

/* The left edge from (x0, y0) to (x1, y1): each row's x from a stepper over the rows (its place
 * before the step), clipped to the clip rectangle's right, written for the rows from 0 up to the
 * screen's width (the original compares the row with the width), from the table's word out. */
static void stretch_walk_x_clipped(u16 out, s16 x0, s16 y0, s16 x1, s16 y1) {
    StretchStep s;
    u16 steps = (u16)(y1 - (y0 - 1));
    s.w[0] = s.w[2] = 0;
    s.w[1] = x0;
    s.w[3] = x1;
    stretch_step_init(&s, (s16)steps);
    s16 y = y0;
    for (u16 n = steps; n != 0; n--, y++) {
        s16 x = s.w[1];
        s.l[0] = (s32)((u32)s.l[0] + (u32)s.l[1]);
        if (y < 0 || y >= g_wScreen_width)
            continue;
        if (x > g_graphics_context.clip.xmax)
            x = g_graphics_context.clip.xmax;
        *source_at(out++) = x;
    }
}

/* A span at row y from x, count pixels: clipped to the clip rectangle's left and right, and
 * drawn by slot 36 from the X source table's entry for the first pixel left. */
static void span(const ImageRecord *sprite, u16 seg, s16 y, s16 x, s16 count) {
    if ((s16)(count + x) > g_graphics_context.clip.xmax)
        count = (s16)(g_graphics_context.clip.xmax - x + 1);
    s16 skip = 0;
    if (x < g_graphics_context.clip.xmin) {
        skip = (s16)(g_graphics_context.clip.xmin - x);
        x = (s16)(x + skip);
        count = (s16)(count - skip);
    }
    if (count <= 0)
        return;
    u16 table = (u16)skip;
    source_at((u16)(table + count - 1));
    ((BlitRotatedFn *)g_renderer_vtable.pfn_blit_sprite_planar_rotated)(
        seg, vga_row_offset(y), sprite, (u16)FW(F_ROWBASE), source_at(table), count, x);
}

/* The quad's width at the second corner's row: how far its right edge (from corner a to b)
 * lies from the corner, by the edge's slope (the low word of the product when the edge is
 * level, the division skipped). */
static s16 width_at(u16 a, u16 b, u16 corner) {
    s32 product = (s32)(s16)(VY(corner) - VY(a)) * (s16)(VX(b) - VX(a));
    s16 rise = (s16)(VY(b) - VY(a));
    s16 x = rise == 0 ? (s16)product : bak_idiv16(product, rise, NULL);
    return (s16)(x + VX(a) - VX(corner));
}

/* The number of source steps the other stepper covers: |scale * num / den| times the source's
 * rows or columns less 1, saturated at FFFFh, over the other scale (an unsigned DIV), at most
 * 320. */
static u16 ratio(s16 scale, s16 num, s16 den, s16 extent, s16 other) {
    s16 r = 0;
    if (den != 0) {
        r = bak_idiv16((s32)scale * num, den, NULL);
        r = (s16)(r < 0 ? neg16(r) : r);
    }
    s32 product = (s32)r * extent;
    u16 lo = (u16)product;
    if ((u16)((u32)product >> 16) != 0)
        lo = 0xffff;
    u16 q = (u16)(lo / (u16)other);
    return q >= 0x140 ? 0x140 : q;
}

void jmp_via_2f48(unsigned char *sprite_, s16 x, s16 y, s16 angle, s16 scale_x, s16 scale_y,
                  s16 half_w, s16 half_h) {
    const ImageRecord *sprite = (const ImageRecord *)sprite_;
    FW(A_X) = x;
    FW(A_Y) = y;
    FW(A_SX) = scale_x;
    FW(A_SY) = scale_y;
    if (FW(A_SX) == 0 || FW(A_SY) == 0)
        goto done;
    if (FW(A_SX) < 0)
        FW(A_SX) = neg16(FW(A_SX));
    if (FW(A_SY) < 0)
        FW(A_SY) = neg16(FW(A_SY));
    FW(F_COS) = r3d_tbl_cos(angle);
    FW(F_SIN) = r3d_tbl_sin(angle);
    FW(F_OX) = x;
    FW(F_OY) = y;
    if (half_h != 0) {
        s16 q = bak_idiv16((s32)half_h * FW(A_SY), sprite->nHeight, NULL);
        FW(F_OY) = (s16)(FW(F_OY) - q14(q, FW(F_COS)));
        FW(F_OX) = (s16)(FW(F_OX) + q14(q, FW(F_SIN)));
    }
    if (half_w != 0) {
        s16 q = bak_idiv16((s32)half_w * FW(A_SX), sprite->nWidth, NULL);
        FW(F_OX) = (s16)(FW(F_OX) - q14(q, FW(F_COS)));
        FW(F_OY) = (s16)(FW(F_OY) - q14(q, FW(F_SIN)));
    }
    FW(F_SYCOS) = q14(FW(A_SY), FW(F_COS));
    FW(F_SYSIN) = q14(FW(A_SY), FW(F_SIN));
    FW(F_SXCOS) = q14(FW(A_SX), FW(F_COS));
    FW(F_SXSIN) = q14(FW(A_SX), FW(F_SIN));

    /* The corners: the origin, along the width, down the height, back. */
    VX(0) = FW(F_OX);
    VX(2) = (s16)(VX(0) + FW(F_SXCOS));
    VX(4) = (s16)(VX(2) - FW(F_SYSIN));
    VX(6) = (s16)(VX(4) - FW(F_SXCOS));
    VY(0) = FW(F_OY);
    VY(2) = (s16)(VY(0) + FW(F_SXSIN));
    VY(4) = (s16)(VY(2) + FW(F_SYCOS));
    VY(6) = (s16)(VY(4) - FW(F_SXSIN));

    u16 rowOff = 0;
    for (int k = 0; k < 200; k++, rowOff = (u16)(rowOff + sprite->nWidth))
        FW(F_ROWOFF + 2 * k) = (s16)rowOff;
    FW(F_ROWEND) = row_off(sprite->nHeight);

    /* The top corner: the first of the highest, then two tie-breaks for exact turns. */
    s16 best = 0x7d00;
    int top = -1;
    for (int i = 0; i < 4; i++) {
        if (VY(2 * i) < best) {
            best = VY(2 * i);
            top = 2 * i;
        }
    }
    if (top < 0)
        goto done;
    g_wRotBlitIdxTop = (u16)top;
    if (g_wRotBlitIdxTop == 0 && (u16)angle >= 0x4000)
        g_wRotBlitIdxTop = 6;
    if ((u16)angle < 0x8000 && VY(4) == VY(6))
        g_wRotBlitIdxTop += 2;
    g_wRotBlitIdxNextCW = (u16)((g_wRotBlitIdxTop + 2) & 6);
    g_wRotBlitIdxBottom = (u16)((g_wRotBlitIdxNextCW + 2) & 6);
    g_wRotBlitIdxOppCW = (u16)((g_wRotBlitIdxBottom + 2) & 6);
    u16 topIdx = g_wRotBlitIdxTop, next = g_wRotBlitIdxNextCW;
    u16 bottom = g_wRotBlitIdxBottom, opp = g_wRotBlitIdxOppCW;
    unsigned octant = ((8u - topIdx) & 6) >> 1;
    FW(F_OCTANT) = (s16)(((u16)FW(F_OCTANT) & 0xff00) | octant);

    /* The left edge: the first row (the top's, or 0), then each row's x. */
    FW(F_YSRC) = YSRC;
    FW(F_T1) = VY(topIdx) < 0 ? 0 : VY(topIdx);
    *source_at(YSRC) = FW(F_T1);
    stretch_walk_x_clipped(YSRC + 1, VX(topIdx), VY(topIdx), VX(opp), VY(opp));
    s16 rows = (s16)(VY(opp) - FW(F_T1));
    FW(F_T2) = rows < 0 ? 0 : rows;
    stretch_walk_x_clipped((u16)(YSRC + FW(F_T2) + 1), VX(opp), VY(opp), VX(bottom), VY(bottom));

    FW(F_NROWS) = (s16)(sprite->nHeight - 1);
    FW(F_NCOLS) = (s16)(sprite->nWidth - 1);
    if (octant & 1) {
        s16 t = FW(F_NROWS);
        FW(F_NROWS) = FW(F_NCOLS);
        FW(F_NCOLS) = t;
        t = FW(F_COS);
        FW(F_COS) = FW(F_SIN);
        FW(F_SIN) = t;
        t = FW(A_SX);
        FW(A_SX) = FW(A_SY);
        FW(A_SY) = t;
    }

    /* Steps across the quad's width at the second corner's row: the source offset of each, and
     * the step each source row (UP) and column (DOWN) starts on. */
    FW(F_FILLUP) = 0;
    FW(F_FILLDOWN) = 0;
    s16 si = 0;
    if (VY(opp) < VY(next)) {
        FW(F_WIDTH) = width_at(topIdx, next, opp);
        if (FW(F_WIDTH) == 0)
            goto done;
        FW(F_T1) = (s16)ratio(FW(A_SY), FW(F_COS), FW(F_SIN), FW(F_NCOLS), FW(A_SX));
        step_start(F_STEPA, FW(F_NROWS), FW(F_WIDTH));
        step_start(F_STEPB, FW(F_T1), FW(F_WIDTH));
    } else {
        FW(F_WIDTH) = width_at(bottom, next, opp);
        if (FW(F_WIDTH) == 0)
            goto done;
        FW(F_T1) = (s16)ratio(FW(A_SX), FW(F_SIN), FW(F_COS), FW(F_NROWS), FW(A_SY));
        step_start(F_STEPA, FW(F_T1), FW(F_WIDTH));
        step_start(F_STEPB, FW(F_NCOLS), FW(F_WIDTH));
    }
    do {
        FW(F_POSA) = step_next(F_STEPA);
        FW(F_POSB) = step_next(F_STEPB);
        /* The counts are tested as the original's flags after INC test them: exactly. */
        int n = (s16)(FW(F_POSA) - FW(F_FILLUP)) + 1;
        if (n > 0) {
            s16 from = FW(F_FILLUP);
            FW(F_FILLUP) = (s16)(FW(F_FILLUP) + n);
            fill(F_UP, from, (u16)n, si);
        }
        n = (s16)(FW(F_POSB) - FW(F_FILLDOWN)) + 1;
        if (n >= 0) {
            s16 from = FW(F_FILLDOWN);
            FW(F_FILLDOWN) = (s16)(FW(F_FILLDOWN) + n);
            fill(F_DOWN, from, (u16)n, si);
        }
        s16 v;
        switch (octant) {
        case 0:
            v = (s16)(FW(F_POSB) - row_off(FW(F_POSA)));
            break;
        case 1:
            v = (s16)(neg16(FW(F_POSA)) - row_off(FW(F_POSB)));
            break;
        case 2:
            v = (s16)(row_off(FW(F_POSA)) - FW(F_POSB));
            break;
        default:
            v = (s16)(FW(F_POSA) + row_off(FW(F_POSB)));
            break;
        }
        *source_at((u16)si) = v;
        si++;
    } while (si < FW(F_WIDTH));
    int n = (s16)(FW(F_NROWS) + 1) - FW(F_FILLUP);
    if (n >= 0)
        fill(F_UP, FW(F_FILLUP), (u16)n, si);
    n = (s16)(FW(F_NCOLS) + 1) - FW(F_FILLDOWN);
    if (n >= 0)
        fill(F_DOWN, FW(F_FILLDOWN), (u16)n, si);

    /* The rows from the top corner to the one before it. */
    step_start(F_STEPA, FW(F_NROWS), (s16)(VY(opp) - VY(topIdx)));
    if (g_graphics_context.bVideoAdapter == 2 || g_graphics_context.bVideoAdapter == 0x0c ||
        g_graphics_context.bVideoAdapter == 0x0d) {
        bak_port_out16(0x3CE, 0x0001);
        bak_port_out16(0x3CE, 0x0005);
        bak_port_out8(0x3CE, 8);
    }
    FW(F_T1) = *source_at((u16)FW(F_YSRC));
    FW(F_YSRC) = (s16)(FW(F_YSRC) + 1);
    u16 seg = g_graphics_context.wGfxBlitDstPage;
    FW(F_FILLUP) = VY(opp);
    for (si = VY(topIdx); si < FW(F_FILLUP); si = (s16)(FW(A_Y) + 1)) {
        FW(A_Y) = si;
        FW(F_T2) = step_next(F_STEPA);
        if (si < FW(F_T1))
            continue;
        if (si >= g_graphics_context.clip.ymin && si <= g_graphics_context.clip.ymax) {
            s16 left = *source_at((u16)FW(F_YSRC));
            s16 count = si > (s16)(VY(next) + 1) ? *frame_at(F_DOWN, FW(F_NCOLS))
                                                 : *frame_at(F_UP, FW(F_T2));
            switch (octant) {
            case 0:
                FW(F_ROWBASE) = row_off(FW(F_T2));
                break;
            case 1:
                FW(F_ROWBASE) = (s16)(FW(F_ROWEND) - sprite->nWidth + FW(F_T2));
                break;
            case 2:
                FW(F_ROWBASE) = (s16)(FW(F_ROWEND) - row_off(FW(F_T2)) - 1);
                break;
            default:
                FW(F_ROWBASE) = (s16)(sprite->nWidth - FW(F_T2) - 1);
                break;
            }
            span(sprite, seg, si, left, count);
        }
        FW(F_YSRC) = (s16)(FW(F_YSRC) + 1);
    }

    /* And from there to the bottom corner. */
    step_start(F_STEPA, FW(F_NCOLS), (s16)(VY(bottom) - VY(opp)));
    FW(F_FILLUP) = VY(bottom);
    for (si = VY(opp); si < FW(F_FILLUP); si = (s16)(FW(A_Y) + 1)) {
        FW(A_Y) = si;
        FW(F_T2) = step_next(F_STEPA);
        if (si < FW(F_T1))
            continue;
        if (si >= g_graphics_context.clip.ymin && si <= g_graphics_context.clip.ymax) {
            s16 left = *source_at((u16)FW(F_YSRC));
            s16 count = si > (s16)(VY(next) - 1) ? *frame_at(F_DOWN, (s16)(FW(F_NCOLS) - FW(F_T2)))
                                                 : *frame_at(F_UP, FW(F_NROWS));
            switch (octant) {
            case 0:
                FW(F_ROWBASE) = (s16)(FW(F_ROWEND) - sprite->nWidth + FW(F_T2));
                break;
            case 1:
                FW(F_ROWBASE) = (s16)(FW(F_ROWEND) - row_off(FW(F_T2)) - 1);
                break;
            case 2:
                FW(F_ROWBASE) = (s16)(sprite->nWidth - FW(F_T2) - 1);
                break;
            default:
                FW(F_ROWBASE) = row_off(FW(F_T2));
                break;
            }
            span(sprite, seg, si, left, count);
        }
        FW(F_YSRC) = (s16)(FW(F_YSRC) + 1);
    }
done:
    vga_reset_gc_default();
}

void read_4words_from_2tables(unsigned short *x_opp, unsigned short *y_top, unsigned short *x_next,
                              unsigned short *y_bottom) {
    *x_opp = (unsigned short)VX(g_wRotBlitIdxOppCW);
    *y_top = (unsigned short)VY(g_wRotBlitIdxTop);
    *x_next = (unsigned short)VX(g_wRotBlitIdxNextCW);
    *y_bottom = (unsigned short)VY(g_wRotBlitIdxBottom);
}
