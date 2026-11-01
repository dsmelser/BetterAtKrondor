/*
 * POLYCLIP.ASM in C: polygon_clip_viewport, draw_polygon's clipper (Sutherland-Hodgman).
 *
 * The polygon in the graphics context's vertex arrays is clipped against x first, from
 * pPolygonX/Y into pPolygonXClipped/YClipped, then against y, back into pPolygonX/Y.
 * Each pass walks the edges from the last vertex to the first one: an edge inside is
 * kept by its end, one wholly past the same bound is dropped, and one that crosses a
 * bound gives the point where it does, xmin/ymin tested before xmax/ymax. That point
 * is a 16-bit IMUL and IDIV from the end outside the bound (through the divide-error
 * handler's rule when the quotient overflows). The tests are signed. With fewer than
 * two vertices left after the x pass, they are copied back and the y pass is skipped.
 *
 * The arrays are addressed by byte offsets, as in the original: a pass that makes more
 * vertices than an array holds writes on into the next one.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GFX/RASTER/POLYCLIP.H"
#include "bak_game_end.h"

#define CTX_POLY_X 0x0ac
#define CTX_POLY_Y 0x0d4
#define CTX_CLIP_X 0x0fc
#define CTX_CLIP_Y 0x124

#define OUT_XMIN 1
#define OUT_XMAX 2
#define OUT_YMAX 4
#define OUT_YMIN 8

static unsigned char *at(u32 off) {
    if (off + 2 > sizeof g_gfxContextBlock)
        BAK_UNIMPLEMENTED("polygon_clip_viewport: a vertex past the graphics context (the "
                          "original reaches other DGROUP data)");
    return (unsigned char *)&g_gfxContextBlock + off;
}

static u16 get(u32 off) {
    const unsigned char *p = at(off);
    return (u16)(p[0] | p[1] << 8);
}

static void set(u32 off, u16 value) {
    unsigned char *p = at(off);
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static unsigned char code_x(u16 x) {
    const ClipRect *c = &g_graphics_context.clip;
    return (unsigned char)(((s16)x < c->xmin ? OUT_XMIN : 0) | ((s16)x > c->xmax ? OUT_XMAX : 0));
}

static unsigned char code_y(u16 y) {
    const ClipRect *c = &g_graphics_context.clip;
    return (unsigned char)(((s16)y > c->ymax ? OUT_YMAX : 0) | ((s16)y < c->ymin ? OUT_YMIN : 0));
}

/*
 * Where the edge from vertex o to vertex p crosses the bound b, the vertices at the offsets
 * o and p of the input arrays: inCut for the coordinate the bound is on, inOther for the
 * other. Written at the offset di of the output arrays, the bound first.
 */
static void cut(u32 inCut, u32 inOther, u32 outCut, u32 outOther, u16 di, u16 b, u16 o, u16 p) {
    set(outCut + di, b);
    u16 bp = (u16)(b - get(inCut + o));
    u16 ax = (u16)(get(inOther + p) - get(inOther + o));
    s32 product = (s32)(s16)ax * (s16)bp;
    bp = (u16)(get(inCut + p) - get(inCut + o));
    ax = (u16)bak_idiv16(product, (s16)bp, NULL);
    set(outOther + di, (u16)(ax + get(inOther + o)));
}

void polygon_clip_viewport(void) {
    GraphicsContext *g = &g_graphics_context;
    const ClipRect *c = &g->clip;
    u16 ax = (u16)g->nPolygonClipVertexCount;
    u16 di = 0, si, bx;
    unsigned char cl, ch;

    if (!((s16)ax > 1))
        return;

    /* x: pPolygonX/Y -> pPolygonXClipped/YClipped (the bound first, then the other) */
    bx = (u16)((u16)(ax - 1) << 1);
    cl = code_x(get(CTX_POLY_X + (u32)bx));
    si = 0;
    for (;;) {
        si = (u16)(si << 1);
        ch = code_x(get(CTX_POLY_X + (u32)si));
        if ((cl | ch) == 0) {
            set(CTX_CLIP_X + (u32)di, get(CTX_POLY_X + (u32)si));
            set(CTX_CLIP_Y + (u32)di, get(CTX_POLY_Y + (u32)si));
            di = (u16)(di + 2);
        } else if ((cl & ch) == 0) {
            if (cl != 0) { /* the edge enters: where, from its start */
                if (cl & OUT_XMIN) {
                    cut(CTX_POLY_X, CTX_POLY_Y, CTX_CLIP_X, CTX_CLIP_Y, di, (u16)c->xmin, bx, si);
                    di = (u16)(di + 2);
                } else if (cl & OUT_XMAX) {
                    cut(CTX_POLY_X, CTX_POLY_Y, CTX_CLIP_X, CTX_CLIP_Y, di, (u16)c->xmax, bx, si);
                    di = (u16)(di + 2);
                }
                if (ch == 0) {
                    set(CTX_CLIP_X + (u32)di, get(CTX_POLY_X + (u32)si));
                    set(CTX_CLIP_Y + (u32)di, get(CTX_POLY_Y + (u32)si));
                    di = (u16)(di + 2);
                }
            }
            if (ch != 0) { /* the edge leaves: where, from its end */
                if (ch & OUT_XMIN) {
                    cut(CTX_POLY_X, CTX_POLY_Y, CTX_CLIP_X, CTX_CLIP_Y, di, (u16)c->xmin, si, bx);
                    di = (u16)(di + 2);
                } else if (ch & OUT_XMAX) {
                    cut(CTX_POLY_X, CTX_POLY_Y, CTX_CLIP_X, CTX_CLIP_Y, di, (u16)c->xmax, si, bx);
                    di = (u16)(di + 2);
                }
            }
        }
        bx = si;
        cl = ch;
        si = (u16)((si >> 1) + 1);
        if (si == (u16)g->nPolygonClipVertexCount)
            break;
    }
    ax = (u16)(di >> 1);
    g->nPolygonClipVertexCount = (s16)ax;
    if (!((s16)ax > 1)) { /* REP MOVSW of what is left */
        for (u16 i = 0; i < ax; i++)
            set(CTX_POLY_X + 2u * i, get(CTX_CLIP_X + 2u * i));
        for (u16 i = 0; i < ax; i++)
            set(CTX_POLY_Y + 2u * i, get(CTX_CLIP_Y + 2u * i));
        return;
    }

    /* y: pPolygonXClipped/YClipped -> pPolygonX/Y (again the bound first: y here) */
    bx = (u16)((u16)(ax - 1) << 1);
    cl = code_y(get(CTX_CLIP_Y + (u32)bx));
    di = 0;
    si = 0;
    for (;;) {
        si = (u16)(si << 1);
        ch = code_y(get(CTX_CLIP_Y + (u32)si));
        if ((cl | ch) == 0) {
            set(CTX_POLY_X + (u32)di, get(CTX_CLIP_X + (u32)si));
            set(CTX_POLY_Y + (u32)di, get(CTX_CLIP_Y + (u32)si));
            di = (u16)(di + 2);
        } else if ((cl & ch) == 0) {
            if (cl != 0) {
                if (cl & OUT_YMIN) {
                    cut(CTX_CLIP_Y, CTX_CLIP_X, CTX_POLY_Y, CTX_POLY_X, di, (u16)c->ymin, bx, si);
                    di = (u16)(di + 2);
                } else if (cl & OUT_YMAX) {
                    cut(CTX_CLIP_Y, CTX_CLIP_X, CTX_POLY_Y, CTX_POLY_X, di, (u16)c->ymax, bx, si);
                    di = (u16)(di + 2);
                }
                if (ch == 0) {
                    set(CTX_POLY_X + (u32)di, get(CTX_CLIP_X + (u32)si));
                    set(CTX_POLY_Y + (u32)di, get(CTX_CLIP_Y + (u32)si));
                    di = (u16)(di + 2);
                }
            }
            if (ch != 0) {
                if (ch & OUT_YMIN) {
                    cut(CTX_CLIP_Y, CTX_CLIP_X, CTX_POLY_Y, CTX_POLY_X, di, (u16)c->ymin, si, bx);
                    di = (u16)(di + 2);
                } else if (ch & OUT_YMAX) {
                    cut(CTX_CLIP_Y, CTX_CLIP_X, CTX_POLY_Y, CTX_POLY_X, di, (u16)c->ymax, si, bx);
                    di = (u16)(di + 2);
                }
            }
        }
        bx = si;
        cl = ch;
        si = (u16)((si >> 1) + 1);
        if (si == (u16)g->nPolygonClipVertexCount)
            break;
    }
    g->nPolygonClipVertexCount = (s16)(di >> 1);
}
