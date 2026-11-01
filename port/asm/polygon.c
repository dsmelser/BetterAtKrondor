/*
 * POLYGON.ASM in C: the polygon rasterizer and the span slot's dispatcher.
 *
 * fill_spans_dispatch hands a span table to the renderer's span slot (27), which takes
 * it in ES:SI. draw_polygon fills a polygon through the same slot:
 *
 * - It copies the vertices into the graphics context, has POLYCLIP.ASM clip them to the
 *   clip rectangle when clipping is on, and drops each vertex that repeats the one kept
 *   before it (walking them last to first).
 * - Two vertices, or all on one row, are a line (draw_line), or a span when the outline
 *   colour is the fill colour. With fill off, only the outline is drawn.
 * - Otherwise it walks two chains from the top vertex: the first to the bottom vertex,
 *   into the second word of each row of the span table, the second back up, into the
 *   first word. The table is the context's span buffer, four bytes a row from row 0 on;
 *   the header (first row, rows) goes in the four bytes before the top row, addressed
 *   from the segment before the buffer's.
 * - Which way round the chains go comes from the slopes of the two edges at the top
 *   vertex. When they are equal the polygon is filled twice, the second time with its
 *   vertices reversed.
 * - An outline colour other than the fill colour draws the outline after the fill.
 *
 * The vertex arrays are the context's, addressed as the original does by byte offsets, so
 * that more vertices than an array holds run on into the next one as they do there.
 *
 * The original's edge routines take their arguments in registers; here they are static
 * functions with the registers as parameters, and their public names trap.
 * blit_rect_buf_dispatch (slot 23) has no caller in the game and traps too.
 */
#include <string.h>

#include "../drivers/vga.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/RASTER/DRAWLINE.H"
#include "SRC/GFX/RASTER/POLYCLIP.H"
#include "SRC/GFX/RASTER/POLYGON.H"
#include "bak_game_end.h"

/* The context's vertex arrays, by offset in g_gfxContextBlock. */
#define CTX_POLY_X 0x0ac
#define CTX_POLY_Y 0x0d4
#define CTX_CLIP_X 0x0fc
#define CTX_CLIP_Y 0x124
#define CTX_SAVED_X 0x14c
#define CTX_SAVED_Y 0x174

/* The stub chain the edge rows run through has 400 entries. */
#define EDGE_ROWS_MAX 400

void fill_spans_dispatch(unsigned short off, unsigned short seg) {
    ((FillSpansFn *)g_renderer_vtable.pfn_fill_spans_dithered_modey)(MK_FP(seg, off));
}

void blit_rect_buf_dispatch(unsigned char *far_ptr) {
    (void)far_ptr;
    BAK_UNIMPLEMENTED("POLYGON.ASM: blit_rect_buf_dispatch (no caller in the game)");
}

/* ---- the context's words and the span buffer ---------------------------------------- */

static unsigned char *ctx_at(u32 off) {
    if (off + 2 > sizeof g_gfxContextBlock)
        BAK_UNIMPLEMENTED("draw_polygon: a vertex past the graphics context (the original "
                          "reaches other DGROUP data)");
    return (unsigned char *)&g_gfxContextBlock + off;
}

static u16 ctx_get(u32 off) {
    const unsigned char *p = ctx_at(off);
    return (u16)(p[0] | p[1] << 8);
}

static void ctx_set(u32 off, u16 value) {
    unsigned char *p = ctx_at(off);
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

/* REP MOVSW within the context: count words, upwards. */
static void ctx_copy(u32 dst, u32 src, u16 count) {
    for (u16 i = 0; i < count; i++)
        ctx_set(dst + 2u * i, ctx_get(src + 2u * i));
}

/* A word of the span buffer at seg:off (the original's STOSW through ES). */
static void span_put(u16 seg, u16 off, u16 value) {
    if ((u32)off + 2 > bak_seg_extent(seg))
        BAK_UNIMPLEMENTED("draw_polygon: a row past the span buffer (the original writes past "
                          "it)");
    unsigned char *p = MK_FP(seg, off);
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static void spans(u16 seg, u16 off) {
    ((FillSpansFn *)g_renderer_vtable.pfn_fill_spans_dithered_modey)(MK_FP(seg, off));
}

static void swap(u16 *a, u16 *b) {
    u16 t = *a;
    *a = *b;
    *b = t;
}

/* CWD, XOR, SUB: the magnitude of a 16-bit value (8000h stays 8000h). */
static u16 abs16(u16 v) {
    return (s16)v < 0 ? (u16) - (u16)v : v;
}

/* ---- edges into the span table --------------------------------------------------------- */

/*
 * state_machine_disp_tbl_5d80: rows of a ramp, from row di on, one row per stub of the
 * original's 400: each stores ax in the row's word at the current column, then adds
 * si:bx to ax:bp. The original enters its chain 7 * rows bytes before the end, so more
 * rows than stubs would run its own code.
 */
static void edge_rows(u16 seg, u16 ax, u16 bx, u16 cx, u16 di, u16 si, u16 bp) {
    di = (u16)((u16)(di << 2) + g_wRasterEdgeColumnOff);
    if (cx > EDGE_ROWS_MAX)
        BAK_UNIMPLEMENTED("draw_polygon: an edge of more than 400 rows (the original jumps "
                          "into its own code)");
    u16 stride = (u16)(s16)(signed char)g_bRasterSpanDiStride;
    for (; cx != 0; cx--) {
        span_put(seg, di, ax);
        di = (u16)(di + 2 + stride);
        u32 sum = (u32)bp + bx;
        bp = (u16)sum;
        ax = (u16)(ax + si + (sum >> 16));
    }
}

/* state_machine_enter_state2_a: a vertical edge, (x, cy)-(x, si), x in bp. */
static void edge_vertical(u16 seg, u16 bp, u16 cx, u16 si) {
    if (!((s16)cx > (s16)si))
        swap(&cx, &si);
    g_bRasterSpanDiStride = 2;
    edge_rows(seg, bp, 0, (u16)(cx - si + 1), si, 0, 0);
}

/* state_machine_enter_state2_b: a diagonal edge, (bx, cx)-(bp, si), a column a row. */
static void edge_diagonal(u16 seg, u16 bx, u16 bp, u16 cx, u16 si) {
    if (!((s16)cx < (s16)si)) {
        swap(&bx, &bp);
        swap(&cx, &si);
    }
    u16 step = (s16)bx < (s16)bp ? 1 : 0xffff;
    g_bRasterSpanDiStride = 2;
    edge_rows(seg, bx, 0, (u16)(si - cx + 1), cx, step, 0);
}

/*
 * rasterize_polygon_edge: an edge with more rows than columns, (bx, cx)-(bp, si), walked
 * from its left end, a row at a time up or down, with Bresenham's error term.
 */
static void edge_steep(u16 seg, u16 bx, u16 bp, u16 cx, u16 si) {
    if (!((s16)bx < (s16)bp)) {
        swap(&bx, &bp);
        swap(&cx, &si);
    }
    u16 di = (u16)((u16)(cx << 2) + g_wRasterEdgeColumnOff);
    u16 ax = (u16)(cx - si);
    u16 dx = (s16)ax < 0 ? 0xffff : 0;
    cx = (u16)((ax ^ dx) - dx);
    ax = (u16)(bp - bx);
    si = (u16)(ax << 1);
    bp = (u16)((u16)((u16)(ax - cx) << 1) ^ si);
    ax = bx;
    bx = (u16)(si - cx);
    cx++;
    u16 step = dx != 0 ? 2 : (u16)-6; /* after STOSW's 2: down a row, or up */
    for (;;) {
        span_put(seg, di, ax);
        di = (u16)(di + 2 + step);
        u16 m = (s16)bx >= 0 ? 0xffff : 0; /* CMP BH,80h / SBB DX,DX */
        ax = (u16)(ax - m);
        bx = (u16)(bx + ((m & bp) ^ si));
        if (--cx == 0)
            return;
    }
}

/*
 * rasterize_polygon_edge_steep (right, column 2) and _steep_b (left, column 0): an edge
 * with more columns than rows, (bx, cx)-(bp, si), walked from its right end leftwards or
 * its left end rightwards. Its first row takes the end's x; after it the x steps once
 * more for every row, and again while the error term is negative.
 */
static void edge_flat(u16 seg, u16 bx, u16 bp, u16 cx, u16 si, int right) {
    if (right ? !((s16)bx > (s16)bp) : !((s16)bx < (s16)bp)) {
        swap(&bx, &bp);
        swap(&cx, &si);
    }
    u16 di = (u16)((u16)(cx << 2) + (right ? 2 : 0));
    u16 dx = 2;
    if ((s16)si >= (s16)cx) { /* SUB SI,CX / JGE */
        si = (u16)(si - cx);
    } else {
        si = (u16) - (u16)(si - cx);
        dx = (u16)-6;
    }
    cx = si;
    if ((s16)bp <= (s16)bx) /* SUB BP,BX / JLE */
        bp = (u16)(bp - bx);
    else
        bp = (u16) - (u16)(bp - bx);
    u16 ax = bp;
    bp = (u16)((u16)(bp + si) << 1);
    si = (u16)(si << 1);
    ax = (u16)(ax + si);
    swap(&ax, &bx);
    u16 step = right ? 0xffff : 1;
    span_put(seg, di, ax);
    di = (u16)(di + 2 + dx);
    ax = (u16)(ax + step);
    int carry = (s16)bx < 0;
    for (;;) {
        if (carry) { /* step on while the error term is negative */
            ax = (u16)(ax + step);
            s32 sum = (s32)(s16)bx + (s16)si;
            bx = (u16)sum;
            if (sum >= 0)
                carry = 0;
            continue;
        }
        span_put(seg, di, ax);
        di = (u16)(di + 2 + dx);
        ax = (u16)(ax + step);
        if (--cx == 0)
            return;
        s32 sum = (s32)(s16)bx + (s16)bp;
        bx = (u16)sum;
        if (sum < 0)
            carry = 1;
    }
}

/*
 * draw_polygon_edges: count edges, from vertex i to i + 1 of the arrays at xs and ys
 * (context offsets), each drawn by draw_line from its far end. In the 640x200 mode the
 * y coordinates and the clip rectangle's y bounds are halved for it (the bounds with an
 * unsigned shift, restored by a left shift that loses their bit 0).
 */
static void draw_edges(u32 xs, u32 ys, u16 count) {
    ClipRect *c = &g_graphics_context.clip;
    int halve = g_graphics_context.bYResDoubled != 0;
    if (halve) {
        c->ymin = (s16)((u16)c->ymin >> 1);
        c->ymax = (s16)((u16)c->ymax >> 1);
    }
    do {
        u16 y2 = ctx_get(ys);
        u16 x2 = ctx_get(xs);
        xs += 2;
        ys += 2;
        u16 y1 = ctx_get(ys);
        u16 x1 = ctx_get(xs);
        if (halve) {
            y1 = (u16)((s16)y1 >> 1);
            y2 = (u16)((s16)y2 >> 1);
        }
        draw_line((s16)x1, (s16)y1, (s16)x2, (s16)y2);
    } while (--count != 0);
    if (halve) {
        c->ymin = (s16)(u16)((u16)c->ymin << 1);
        c->ymax = (s16)(u16)((u16)c->ymax << 1);
    }
}

void draw_polygon_edges(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: draw_polygon_edges (register arguments: draw_polygon "
                      "calls its C form)");
}

void state_machine_enter_state2_a(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: state_machine_enter_state2_a (register arguments: "
                      "draw_polygon calls its C form)");
}

void rasterize_polygon_edge(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: rasterize_polygon_edge (register arguments: draw_polygon "
                      "calls its C form)");
}

void state_machine_enter_state2_b(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: state_machine_enter_state2_b (register arguments: "
                      "draw_polygon calls its C form)");
}

void rasterize_polygon_edge_steep(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: rasterize_polygon_edge_steep (register arguments: "
                      "draw_polygon calls its C form)");
}

void rasterize_polygon_edge_steep_b(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: rasterize_polygon_edge_steep_b (register arguments: "
                      "draw_polygon calls its C form)");
}

void state_machine_disp_tbl_5d80(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: state_machine_disp_tbl_5d80 (register arguments: "
                      "draw_polygon calls its C form)");
}

/* ---- the polygon ------------------------------------------------------------------------ */

/* The edges of both chains, from the vertex list at CTX_CLIP_X/Y, into the span table. */
static void walk_chains(u16 seg) {
    g_wRasterEdgeColumnOff = 2;
    g_wDrawPolyEdgeWalkOff = 0;
    u16 ax = g_wDrawPolyChain1VertexCount;
    for (;;) {
        if (--ax == 0) {
            if (g_wRasterEdgeColumnOff == 0)
                return;
            g_wDrawPolyEdgeWalkOff += 2; /* past the first chain's last vertex */
            g_wRasterEdgeColumnOff = 0;
            ax = g_wDrawPolyChain2VertexCount;
            continue;
        }
        g_wDrawPolyChainRemaining = ax;
        u16 at = g_wDrawPolyEdgeWalkOff;
        g_wDrawPolyEdgeWalkOff = (u16)(at + 2);
        u16 bx = ctx_get(CTX_CLIP_X + (u32)at);
        u16 bp = ctx_get(CTX_CLIP_X + (u32)at + 2);
        u16 cx = ctx_get(CTX_CLIP_Y + (u32)at);
        u16 si = ctx_get(CTX_CLIP_Y + (u32)at + 2);
        u16 adx = abs16((u16)(bx - bp));
        if (adx == 0) {
            edge_vertical(seg, bp, cx, si);
        } else {
            u16 ady = abs16((u16)(cx - si));
            if (ady == 0) { /* level: the row takes the chain's end of it */
                if (!((s16)bx < (s16)bp))
                    swap(&bx, &bp);
                u16 col = g_wRasterEdgeColumnOff;
                span_put(seg, (u16)((u16)(cx << 2) + col), col == 0 ? bx : bp);
            } else if ((s16)adx < (s16)ady) {
                edge_steep(seg, bx, bp, cx, si);
            } else if ((s16)adx > (s16)ady) {
                edge_flat(seg, bx, bp, cx, si, g_wRasterEdgeColumnOff != 0);
            } else {
                edge_diagonal(seg, bx, bp, cx, si);
            }
        }
        ax = g_wDrawPolyChainRemaining;
    }
}

s16 draw_polygon(s16 count, s16 *xs, s16 *ys) {
    GraphicsContext *g = &g_graphics_context;
    u16 ax, bx, cx, dx, si, di, bp, n, old, i;

    g_wDrawPolySpanBufRetSeg = 0;
    g_bDrawPolyReplayPending = 0;
    ax = (u16)count;
    if ((s16)ax >= 0) {
        g->nPolygonClipVertexCount = (s16)ax;
        for (i = 0; i < ax; i++)
            ctx_set(CTX_POLY_X + 2u * i, (u16)xs[i]);
        for (i = 0; i < ax; i++)
            ctx_set(CTX_POLY_Y + 2u * i, (u16)ys[i]);
    }
    if ((s16)ax < 2)
        goto done;
    if (ax == 2)
        goto two_vertices;
    if (!g->bGfx_fill_enabled) { /* the outline only, closed by a copy of vertex 0 */
        n = (u16)g->nPolygonClipVertexCount;
        ctx_set(CTX_POLY_X + (u16)(n << 1), ctx_get(CTX_POLY_X));
        ctx_set(CTX_POLY_Y + (u16)(n << 1), ctx_get(CTX_POLY_Y));
        draw_edges(CTX_POLY_X, CTX_POLY_Y, n);
        goto done;
    }
    if (g->bGfx_outline_color != g->bGfx_fill_color) { /* the outline's vertices, closed */
        n = (u16)g->nPolygonClipVertexCount;
        g_wDrawPolyOutlineVertexCount = n;
        ax = ctx_get(CTX_POLY_X);
        ctx_set(CTX_SAVED_X, ax);
        ctx_copy(CTX_SAVED_X + 2, CTX_POLY_X + 2, (u16)(n - 1));
        ctx_set(CTX_SAVED_X + 2u * n, ax);
        ax = ctx_get(CTX_POLY_Y);
        ctx_set(CTX_SAVED_Y, ax);
        ctx_copy(CTX_SAVED_Y + 2, CTX_POLY_Y + 2, (u16)(n - 1));
        ctx_set(CTX_SAVED_Y + 2u * n, ax);
    }
    if (g->bClip_enabled)
        polygon_clip_viewport();
    n = (u16)g->nPolygonClipVertexCount;
    if (n == 2)
        goto two_vertices;
    if ((s16)n < 2)
        goto done;

    /* Last to first: drop repeats, keep the rest in CTX_CLIP_X/Y (reversed), and find the
     * top vertex (the rightmost of the highest) and the bottom one (the leftmost of the
     * lowest). */
    si = (u16)((u16)(n - 1) << 1);
    g_nDrawPolyLastVertexY = (s16)ctx_get(CTX_POLY_Y);
    dx = 0x7fff;
    bx = 0x8001;
    g_nDrawPolyLastVertexX = (s16)ctx_get(CTX_POLY_X);
    bp = dx;
    cx = bx;
    di = 0;
    g_wDrawPolyTopVertexOff = 0;
    g_wDrawPolyBottomVertexOff = 0;
    for (;;) {
        ax = ctx_get(CTX_POLY_Y + (u32)si);
        if (ax == (u16)g_nDrawPolyLastVertexY &&
            ctx_get(CTX_POLY_X + (u32)si) == (u16)g_nDrawPolyLastVertexX)
            goto next_vertex;
        ax = ctx_get(CTX_POLY_Y + (u32)si);
        g_nDrawPolyLastVertexY = (s16)ax;
        ctx_set(CTX_CLIP_Y + (u32)di, ax);
        if (!((s16)ax > (s16)dx) &&
            ((s16)ax < (s16)dx || (s16)ctx_get(CTX_POLY_X + (u32)si) > (s16)cx)) {
            g_wDrawPolyTopVertexOff = di;
            dx = ax;
            cx = ctx_get(CTX_POLY_X + (u32)si);
        }
        if (!((s16)ax < (s16)bx) &&
            ((s16)ax > (s16)bx || (s16)ctx_get(CTX_POLY_X + (u32)si) <= (s16)bp)) {
            g_wDrawPolyBottomVertexOff = di;
            bx = ax;
            bp = ctx_get(CTX_POLY_X + (u32)si);
        }
        ax = ctx_get(CTX_POLY_X + (u32)si);
        g_nDrawPolyLastVertexX = (s16)ax;
        ctx_set(CTX_CLIP_X + (u32)di, ax);
        di = (u16)(di + 2);
    next_vertex:
        old = si;
        si = (u16)(si - 2);
        if ((s16)old < 2) /* SUB SI,2 / JGE */
            break;
    }
    if (dx == bx)
        goto one_row;
    ax = (u16)(di >> 1);
    if (ax == 2)
        goto one_row;
    if ((s16)ax < 2)
        goto done;
    cx = di;
    g->nPolygonClipVertexCount = (s16)ax;

    /* The slopes of the edges to the next vertex (dx/bp) and the previous one (ax/bx),
     * a level edge taking the steepest slope of its direction. */
    si = g_wDrawPolyTopVertexOff;
    di = (u16)(si + 2);
    if (!(di < cx))
        di = 0;
    dx = (u16)(ctx_get(CTX_CLIP_X + (u32)di) - ctx_get(CTX_CLIP_X + (u32)si));
    bp = (u16)(ctx_get(CTX_CLIP_Y + (u32)di) - ctx_get(CTX_CLIP_Y + (u32)si));
    if (bp == 0) {
        bp = 1;
        dx = (s16)dx < 0 ? 0x8001 : 0x7fff;
    }
    di = (u16)(si - 2);
    if ((s16)si < 2) /* SUB DI,2 / JGE */
        di = (u16)(di + cx);
    ax = (u16)(ctx_get(CTX_CLIP_X + (u32)di) - ctx_get(CTX_CLIP_X + (u32)si));
    bx = (u16)(ctx_get(CTX_CLIP_Y + (u32)di) - ctx_get(CTX_CLIP_Y + (u32)si));
    if (bx == 0) {
        bx = 1;
        if ((s16)ax < 0) {
            ax = 0x8001;
            goto prev_left;
        }
        ax = 0x7fff;
    }
    if ((s16)ax < 0)
        goto prev_left;
    if ((s16)dx <= 0)
        goto reverse;
    goto compare;
prev_left:
    if ((s16)dx >= 0)
        goto forward;
    dx = (u16) - (u16)dx;
    ax = (u16) - (u16)ax;
    swap(&ax, &dx);
    swap(&bp, &bx);
compare: {
    /* Unsigned quotients, then their fractions in 16 bits. */
    u16 q1 = (u16)(ax / bx), r1 = (u16)(ax % bx);
    u16 q2 = (u16)(dx / bp), r2 = (u16)(dx % bp);
    if (q2 > q1)
        goto forward;
    if (q2 < q1)
        goto reverse;
    u16 f2 = (u16)(((u32)r2 << 16) / bp);
    u16 f1 = (u16)(((u32)r1 << 16) / bx);
    if (f2 < f1)
        goto reverse;
    if (f2 > f1)
        goto forward;
    g_bDrawPolyReplayPending = 1; /* the same slope: fill again, reversed */
    g_wDrawPolyClipReplayByteCount = cx;
    ctx_copy(CTX_SAVED_X, CTX_CLIP_X, (u16)(cx >> 1));
    ctx_copy(CTX_SAVED_Y, CTX_CLIP_Y, (u16)(cx >> 1));
}
forward:
    ctx_copy(CTX_POLY_X, CTX_CLIP_X, (u16)(cx >> 1));
    ctx_copy(CTX_POLY_Y, CTX_CLIP_Y, (u16)(cx >> 1));
    goto chains;
reverse:
    /* LODSW up the list, stores down from the end; LOOP runs 65536 times for 0 words */
    i = (u16)(cx >> 1);
    di = cx;
    si = 0;
    do {
        ax = ctx_get(CTX_CLIP_X + (u32)si);
        si = (u16)(si + 2);
        di = (u16)(di - 2);
        ctx_set(CTX_POLY_X + (u32)di, ax);
    } while (--i != 0);
    i = (u16)(cx >> 1);
    di = cx;
    si = 0;
    do {
        ax = ctx_get(CTX_CLIP_Y + (u32)si);
        si = (u16)(si + 2);
        di = (u16)(di - 2);
        ctx_set(CTX_POLY_Y + (u32)di, ax);
    } while (--i != 0);
    dx = (u16)(cx - 2);
    g_wDrawPolyTopVertexOff = (u16)(dx - g_wDrawPolyTopVertexOff);
    g_wDrawPolyBottomVertexOff = (u16)(dx - g_wDrawPolyBottomVertexOff);
chains:
    /* The first chain from the top vertex to the first at the bottom's row or below, the
     * second from the bottom vertex to the first at the top's row or above, one after the
     * other in CTX_CLIP_X/Y. */
    dx = ctx_get(CTX_POLY_Y + (u32)g_wDrawPolyBottomVertexOff);
    si = g_wDrawPolyTopVertexOff;
    di = 0;
    for (;;) {
        ctx_set(CTX_CLIP_X + (u32)di, ctx_get(CTX_POLY_X + (u32)si));
        ax = ctx_get(CTX_POLY_Y + (u32)si);
        ctx_set(CTX_CLIP_Y + (u32)di, ax);
        di = (u16)(di + 2);
        if (!((s16)ax < (s16)dx))
            break;
        si = (u16)(si + 2);
        if (!(si < cx))
            si = 0;
    }
    g_wDrawPolyChain1VertexCount = (u16)(di >> 1);
    dx = ctx_get(CTX_POLY_Y + (u32)g_wDrawPolyTopVertexOff);
    si = g_wDrawPolyBottomVertexOff;
    for (;;) {
        ctx_set(CTX_CLIP_X + (u32)di, ctx_get(CTX_POLY_X + (u32)si));
        ax = ctx_get(CTX_POLY_Y + (u32)si);
        ctx_set(CTX_CLIP_Y + (u32)di, ax);
        di = (u16)(di + 2);
        if (!((s16)ax > (s16)dx))
            break;
        si = (u16)(si + 2);
        if (!(si < cx))
            si = 0;
    }
    g_wDrawPolyChain2VertexCount = (u16)((di >> 1) - g_wDrawPolyChain1VertexCount);

    {
        u16 seg = g_graphics_context_render.wSpanTableBufSeg;
        walk_chains(seg);
        dx = ctx_get(CTX_POLY_Y + (u32)g_wDrawPolyTopVertexOff);
        si = (u16)(dx << 2);
        g_wDrawPolySpanBufRetSeg = seg;
        ax = (u16)(ctx_get(CTX_POLY_Y + (u32)g_wDrawPolyBottomVertexOff) - dx + 1);
        /* the header, from the paragraph before the buffer: no offset under 0 */
        seg = (u16)(seg - 1);
        si = (u16)(si + 12);
        span_put(seg, si, dx);
        span_put(seg, (u16)(si + 2), ax);
        spans(seg, si);
    }
    if (g->bGfx_outline_color != g->bGfx_fill_color)
        draw_edges(CTX_SAVED_X, CTX_SAVED_Y, g_wDrawPolyOutlineVertexCount);
done:
    if (g_bDrawPolyReplayPending) {
        g_bDrawPolyReplayPending = 0;
        cx = g_wDrawPolyClipReplayByteCount;
        ctx_copy(CTX_CLIP_X, CTX_SAVED_X, (u16)(cx >> 1));
        ctx_copy(CTX_CLIP_Y, CTX_SAVED_Y, (u16)(cx >> 1));
        goto reverse;
    }
    return (s16)g_wDrawPolySpanBufRetSeg;

two_vertices:
    draw_edges(CTX_POLY_X, CTX_POLY_Y, 1);
    goto done;

one_row:
    /* A span from the leftmost to the rightmost x when the colours agree, else a line
     * from the bottom vertex to the top one. */
    if (g->bGfx_fill_color == g->bGfx_outline_color && bx == dx) {
        u16 seg = g_graphics_context_render.wSpanTableBufSeg;
        span_put(seg, 0, dx);
        span_put(seg, 2, 1);
        if ((s16)bp > (s16)cx)
            swap(&bp, &cx);
        span_put(seg, 4, bp);
        span_put(seg, 6, cx);
        spans(seg, 0);
        goto done;
    }
    draw_line((s16)bp, (s16)bx, (s16)cx, (s16)dx);
    goto done;
}
