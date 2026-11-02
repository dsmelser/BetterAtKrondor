/*
 * GOURAUD.ASM in C: the textured quad rasterizer for quads with slanted sides (the
 * "Gouraud" of the names shades nothing), the texture coordinates of a quad's corners, the
 * slope comparison that picks the way round the quad, and the edge stepper.
 *
 * A quad's corners are (xs[i], ys[i]), i = 0 to 3 in order round it, and the texture (an
 * ImageRecord of column-major pixels, nHeight bytes a column) is pinned to them: corner 0 at
 * its column 0, row 0; corner 1 at column width - 2, row 0; corner 2 at column width - 2,
 * row height - 2; corner 3 at column 0, row height - 2. The quad is drawn one screen column
 * at a time, from its leftmost corner to its rightmost, along the two chains of edges that
 * join them: the "long" chain, the way compare_edge_slopes points, and the "short" one the
 * other way. Each column's span runs from the short chain's edge to the long one's, and the
 * texture column and row at both ends come from steppers along the edges (edge_step_init:
 * 16.16 fixed point, their high words the start and the end, as POLYRAST.ASM's), then step
 * along the span. A column is one plane of Mode Y, chosen through the sequencer's map mask,
 * and rows are 50h bytes apart. (The decompiler named the corners' arrays and the
 * comparison's parameters with x and y swapped; the code says which is which: the plane is
 * chosen by the coordinate it walks, the row offset table indexed by the other.)
 *
 * One thing is not as the original has it. A column clipped at the top skips as many steps
 * of its texture walk as rows were cut, and the original keeps that count for the columns
 * after it, clipped or not: their textures start as many steps in, and a span shorter than
 * the count walks its texture column past the table of column offsets, into the stack. The
 * port starts each column's count at 0 (gfx_gouraud_blit_quad, unless BAK_GOURAUD_SKIP is
 * "original": the goldens, whose pictures must be the original's); the differential test
 * runs the original's way through gouraud_blit_quad_as_original. Kept as they are: a
 * remapped span counts its rows in a byte, and the column table has 319 entries.
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
#include "bak_game_end.h"

typedef uint8_t u8;

#define COLUMNS 319 /* the column table's entries */
#define ROWS 200    /* the row offset table's */

static u16 hi16(s32 v) {
    return (u16)((u32)v >> 16);
}

static void set_hi16(s32 *v, u16 hi) {
    *v = (s32)(((u32)*v & 0xffffu) | (u32)hi << 16);
}

static void advance(s32 *step) {
    step[0] = (s32)((u32)step[0] + (u32)step[1]);
}

/* Starts a stepper over count + 1 steps: step[0] is the accumulator and step[1] the step,
 * their high words the start and the end. The low words are zeroed, the step becomes
 * (end - start) / (count + 1), and the accumulator's fraction follows the quotient's sign,
 * read with JNS after IDIV (DOSBox's flags, as polyrast_edge_init_step): a negative
 * quotient's own low word, otherwise -quotient's, or 8000h when -quotient has no bits from
 * 15 up. A count of 0 or less leaves a step of 0. */
void edge_step_init(s32 *step, s16 count) {
    u32 acc = (u32)step[0] & 0xffff0000u;
    u32 end = (u32)step[1] & 0xffff0000u;
    step[0] = (s32)acc;
    if (count <= 0) {
        step[1] = 0;
        return;
    }
    s32 quotient = (s32)(end - acc) / (s32)(u16)(count + 1);
    step[1] = quotient;
    if (quotient < 0) {
        step[0] = (s32)(acc | ((u32)quotient & 0xffffu));
        return;
    }
    u32 neg = (u32)-quotient;
    step[0] = (s32)(acc | ((neg & 0xffff8000u) != 0 ? (neg & 0xffffu) : 0x8000u));
}

/* Which way round the quad the long chain goes from the leftmost corner (x0, y0): 1 for the
 * next corner (xB, yB), -1 for the one before (xA, yA). Each edge is its rows and its columns
 * from the corner; an edge with no rows counts one row over 7FFFh columns. Edges going up
 * and down different ways settle it at once; otherwise their columns per row are compared,
 * the integer quotients and then the fractions, the edge before winning ties. Each division
 * is the original's: CWD, then an unsigned DIV, which would fault for a negative count of
 * columns (none, from the leftmost corner) and here would truncate instead. */
s16 compare_edge_slopes(s16 yA, s16 xA, s16 y0, s16 x0, s16 yB, s16 xB) {
    s16 colsA = (s16)(xA - x0), colsB = (s16)(xB - x0);
    s16 rowsA = (s16)(yA - y0), rowsB = (s16)(yB - y0);
    if (rowsA == 0) {
        rowsA = 1;
        colsA = 0x7fff;
    }
    if (rowsB == 0) {
        rowsB = 1;
        colsB = 0x7fff;
    }
    if (rowsA < 0 && rowsB < 0) {
        s16 t = rowsB;
        rowsB = (s16)-rowsA;
        rowsA = (s16)-t;
        t = colsB;
        colsB = colsA;
        colsA = t;
    } else {
        if (rowsA < 0 && rowsB > 0)
            return 1;
        if (rowsB < 0 && rowsA > 0)
            return -1;
    }
    u32 dividendA = (u32)(s32)colsA, dividendB = (u32)(s32)colsB;
    u16 divisorA = (u16)rowsA, divisorB = (u16)rowsB;
    u16 qA = (u16)(dividendA / divisorA), rA = (u16)(dividendA % divisorA);
    u16 qB = (u16)(dividendB / divisorB), rB = (u16)(dividendB % divisorB);
    if ((s16)qB < (s16)qA)
        return 1;
    if (qB > qA)
        return -1;
    u16 fracB = (u16)(((u32)rB << 16) / divisorB);
    u16 fracA = (u16)(((u32)rA << 16) / divisorA);
    return fracB > fracA ? -1 : 1;
}

/* The texture column and row at two corners of the quad, v0 and v1, adjacent in either
 * order: corner 0 is column 0, row 0; corner 1 column width - 2, row 0; corner 2 column
 * width - 2, row height - 2; corner 3 column 0, row height - 2. The original dispatches the
 * eight ordered pairs of neighbours through a jump table, which any other pair would jump
 * past. */
void gfx_gouraud_span_dispatch(s16 v0, s16 v1, ImageRecord *texture, s16 *column0, s16 *column1,
                               s16 *row0, s16 *row1) {
    s16 w = (s16)(texture->nWidth - 2), h = (s16)(texture->nHeight - 2);
    const s16 columns[4] = {0, w, w, 0};
    const s16 rows[4] = {0, 0, h, h};
    if (((v0 - v1) & 3) != 1 && ((v1 - v0) & 3) != 1)
        bak_unimplemented("GOURAUD.ASM: corners that are not neighbours (the original jumps "
                          "off its table)",
                          __FILE__, __LINE__);
    *column0 = columns[v0 & 3];
    *column1 = columns[v1 & 3];
    *row0 = rows[v0 & 3];
    *row1 = rows[v1 & 3];
}

static void put(u16 page, unsigned plane, u16 di, u8 value) {
    u8 *p = bak_vram_window(page, di, plane);
    if (p != NULL)
        *p = value;
}

/* XLAT in POLYRAST's code segment, where the fog and shadow tables live (g_polyrastTables
 * holds that part of it, from FOG_REMAP_TAB_OFF on). */
static u8 remap(u16 table, u8 al) {
    unsigned at = (u16)(table + al) - (unsigned)FOG_REMAP_TAB_OFF;
    if (at >= sizeof g_polyrastTables)
        bak_unimplemented("GOURAUD.ASM: a remap table outside the fog and cursor tables "
                          "(the original reads POLYRAST's code)",
                          __FILE__, __LINE__);
    return g_polyrastTables[at];
}

/* The texture's byte at the column and row the span's steppers hold, which then step on. The
 * column indexes the table of column offsets; past its 319 entries the original reads its
 * stack, which only its own way with the skip count reaches (above): then *past is set and
 * nothing is drawn, or the port stops here. The byte comes from the texture's segment, 0
 * past the memory the port has there. */
static u8 texel(const u16 *columns, s32 *column, s32 *row, const u8 *seg, u32 extent, int *past) {
    u16 c = hi16(column[0]), r = hi16(row[0]);
    advance(column);
    advance(row);
    if (c >= COLUMNS) {
        if (past == NULL)
            bak_unimplemented("GOURAUD.ASM: a texture column past the table (the original "
                              "reads its stack)",
                              __FILE__, __LINE__);
        *past = 1;
        return 0;
    }
    u16 si = (u16)(columns[c] + r);
    return si < extent ? seg[si] : 0;
}

static int blit_quad(ImageRecord *texture, s16 *xs, s16 *ys, int asOriginal) {
    u16 columns[COLUMNS];
    s32 longY[2] = {0, 0}, longC[2] = {0, 0}, longR[2] = {0, 0};
    s32 shortY[2] = {0, 0}, shortC[2] = {0, 0}, shortR[2] = {0, 0};
    s32 spanC[2] = {0, 0}, spanR[2] = {0, 0};
    s16 c0, c1, r0, r1; /* an edge's texture column and row at its ends */
    int past = 0;

    u16 at = texture->wImageOff, height = (u16)texture->nHeight;
    for (int i = 0; i < COLUMNS; i++, at = (u16)(at + height))
        columns[i] = at;
    const u8 *seg = texture->pImageBase;
    u32 extent = texture->nImageExtent;
    u16 page = g_graphics_context.wGfxBlitDstPage;
    bak_port_out8(0x3c4, 2);

    /* The leftmost corner (the later of equals) and the rightmost (the earlier). */
    s16 clipSkip = 0, left = 0, right = 0, minX = 32000, maxX = -32000;
    for (s16 vi = 0; vi < 4; vi++) {
        s16 x = xs[vi];
        if (!(x > minX)) {
            minX = x;
            left = vi;
        }
        if (x > maxX) {
            maxX = x;
            right = vi;
        }
    }
    s16 vi = left;
    s16 next = (s16)((left + 1) & 3), before = (s16)((left - 1) & 3);
    s16 dir = compare_edge_slopes(ys[before], xs[before], ys[left], xs[left], ys[next], xs[next]);
    s16 longNext = (s16)((dir + left) & 3);
    s16 span = (s16)(xs[longNext] - xs[left]); /* the columns an edge spans */
    s16 longRemain = span;
    set_hi16(&longY[0], (u16)ys[left]);
    set_hi16(&longY[1], (u16)ys[longNext]);
    edge_step_init(longY, longRemain);
    gfx_gouraud_span_dispatch(left, longNext, texture, &c0, &c1, &r0, &r1);
    set_hi16(&longC[0], (u16)c0);
    set_hi16(&longC[1], (u16)c1);
    edge_step_init(longC, span);
    set_hi16(&longR[0], (u16)r0);
    set_hi16(&longR[1], (u16)r1);
    edge_step_init(longR, span);

    s16 shortVert;
    do {
        shortVert = (s16)((vi - dir) & 3);
        span = (s16)(xs[shortVert] - xs[vi]);
        gfx_gouraud_span_dispatch(vi, shortVert, texture, &c0, &c1, &r0, &r1);
        set_hi16(&shortC[0], (u16)c0);
        set_hi16(&shortC[1], (u16)c1);
        edge_step_init(shortC, span);
        set_hi16(&shortR[0], (u16)r0);
        set_hi16(&shortR[1], (u16)r1);
        edge_step_init(shortR, span);
        set_hi16(&shortY[0], (u16)ys[vi]);
        set_hi16(&shortY[1], (u16)ys[shortVert]);
        edge_step_init(shortY, span);
        s16 x = xs[vi];
        for (;;) {
            x = (s16)(x + 1);
            span = (s16)(span - 1);
            if (span < 0)
                break;
            if (longRemain == 0) {
                /* The long chain's next edge. */
                s16 longCur = longNext;
                longNext = (s16)((longNext + dir) & 3);
                longRemain = (s16)(xs[longNext] - xs[longCur]);
                set_hi16(&longY[0], (u16)ys[longCur]);
                set_hi16(&longY[1], (u16)ys[longNext]);
                edge_step_init(longY, longRemain);
                gfx_gouraud_span_dispatch(longCur, longNext, texture, &c0, &c1, &r0, &r1);
                set_hi16(&longC[0], (u16)c0);
                set_hi16(&longC[1], (u16)c1);
                edge_step_init(longC, longRemain);
                set_hi16(&longR[0], (u16)r0);
                set_hi16(&longR[1], (u16)r1);
                edge_step_init(longR, longRemain);
                longRemain = (s16)(longRemain + 1);
            }
            longRemain = (s16)(longRemain - 1);
            /* The column's span and texture coordinates: what the steppers hold, which then
             * step on. */
            u16 yStart = hi16(shortY[0]);
            advance(shortY);
            u16 yEnd = hi16(longY[0]);
            advance(longY);
            u16 cStart = hi16(shortC[0]);
            advance(shortC);
            u16 cEnd = hi16(longC[0]);
            advance(longC);
            u16 rStart = hi16(shortR[0]);
            advance(shortR);
            u16 rEnd = hi16(longR[0]);
            advance(longR);
            const ClipRect *clip = &g_graphics_context.clip;
            if (x < clip->xmin || x > clip->xmax)
                continue;
            set_hi16(&spanC[0], cStart);
            set_hi16(&spanC[1], cEnd);
            s16 rows = (s16)(yEnd - yStart);
            edge_step_init(spanC, rows);
            set_hi16(&spanR[0], rStart);
            set_hi16(&spanR[1], rEnd);
            edge_step_init(spanR, rows);
            if (!asOriginal)
                clipSkip = 0; /* the original keeps the last clipped column's */
            if (clip->ymin > (s16)yStart) {
                clipSkip = (s16)(clip->ymin - (s16)yStart);
                yStart = (u16)clip->ymin;
            }
            s16 draw = (s16)(yEnd - yStart);
            if (draw < 0)
                continue;
            s16 last = (s16)(draw + (s16)yStart);
            if (last < clip->ymax) {
                if (last < 0)
                    continue;
            } else {
                draw = (s16)(clip->ymax - (s16)yStart);
                if (draw < 0)
                    continue;
            }
            if (yStart >= ROWS)
                bak_unimplemented("GOURAUD.ASM: a span starting past the row offset table "
                                  "(the original reads past it)",
                                  __FILE__, __LINE__);
            unsigned plane = (unsigned)x & 3;
            u16 di = (u16)(g_pRow_offset_lut[yStart] + ((u16)x >> 2));
            bak_port_out8(0x3c5, (u8)(1u << plane));
            for (u16 n = (u16)clipSkip; n != 0; n--) {
                advance(spanC);
                advance(spanR);
            }
            u16 count = (u16)(draw + 1);
            u16 table = (u16)g_polyRasterState.nRemapTableOff;
            int *stack = asOriginal ? &past : NULL;
            if (table == 0) {
                do {
                    u8 al = texel(columns, spanC, spanR, seg, extent, stack);
                    if (al != 0)
                        put(page, plane, di, al);
                    di = (u16)(di + 0x50);
                } while (--count != 0);
            } else {
                u8 n = (u8)count; /* the original counts these in AH */
                do {
                    u8 al = texel(columns, spanC, spanR, seg, extent, stack);
                    if (al != 0)
                        put(page, plane, di, remap(table, al));
                    di = (u16)(di + 0x50);
                } while (--n != 0);
            }
        }
        vi = (s16)((vi - dir) & 3);
    } while (right != shortVert);
    return past;
}

void gfx_gouraud_blit_quad(ImageRecord *texture, s16 *xs, s16 *ys) {
    blit_quad(texture, xs, ys, bak_gouraud_skip_as_original());
}

/* The original's way, for the differential test: each column keeps the skip count of the
 * last one clipped at the top. Returns whether a texture column went past the table, where
 * the original reads its stack and this draws nothing. */
int gouraud_blit_quad_as_original(ImageRecord *texture, s16 *xs, s16 *ys) {
    return blit_quad(texture, xs, ys, 1);
}
