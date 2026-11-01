/*
 * R3D.ASM's polygon clippers in C (the rest of the module is r3d.c): Sutherland-Hodgman
 * against the near plane in camera space, then against the clip rectangle on the screen,
 * in a 16-bit and a 32-bit form.
 *
 * They work on the renderer's polygon arrays (r3ddata.h): the polygon being drawn
 * (aPolyIn*) and the scratch the rectangle clippers' first pass writes (aPolyClip*), with
 * the high words of 32-bit coordinates in the *Hi arrays. Like the originals they walk
 * the arrays with byte offsets, and index the vertex caches and abPolygonClipState by a
 * vertex's byte offset (its index times 2, as polygonVertices holds it).
 *
 * The originals have no bounds: a polygon past the arrays' 20 entries writes on into the
 * arrays after them and into the vertex count, and a vertex count of 0 sends the near
 * clippers round 32768 times. Here those stop with a message, as does a zero divisor,
 * where the original hangs in the divide-error handler; an overflowing IDIV resolves as
 * that handler resolves it (bak_idiv16).
 */
#include "r3ddata.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "r3d.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/R3D/CORE/R3D.H"
#include "bak_game_end.h"

#define CLIPST_BEHIND 3
#define POLY_BYTES (2 * R3D_POLY_MAX)
#define VERTEX_BYTES (2 * R3D_VERTEX_MAX)

/* The outcodes: bit 0 left of xmin, 1 right of xmax, 2 below ymax, 3 above ymin. */
#define OC_XMIN 1
#define OC_XMAX 2
#define OC_YMAX 4
#define OC_YMIN 8

typedef u16 EdgeFn(unsigned char side, u16 other, u16 out, u16 origin);
typedef void CrossFn(u16 other, u16 clipped);

/* ---- the arrays -------------------------------------------------------------------- */

static _Noreturn void past_arrays(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: a polygon past the 20-entry polygon arrays (the original writes "
                      "on into the arrays after them)");
}

/* The index of a polygon array's word at byte offset off. */
static u16 poly_at(u16 off) {
    if (off >= POLY_BYTES)
        past_arrays();
    return (u16)(off >> 1);
}

/* The index of a vertex cache's word at a vertex's byte offset. */
static u16 vtx_at(u16 off) {
    if (off >= VERTEX_BYTES)
        BAK_UNIMPLEMENTED("R3D.ASM: a vertex past the 255-entry vertex caches");
    return (u16)(off >> 1);
}

/* A vertex's clip state (abPolygonClipState holds one byte per vertex byte offset). */
static unsigned char clip_state(u16 vertex) {
    vtx_at(vertex);
    return g_r3d.abPolygonClipState[vertex];
}

/* The vertex count of a ring the near clippers walk from its last vertex: 1-20. */
static s16 ring_count(void) {
    s16 n = g_r3d.nMeshPolyVertexCount;
    if (n == 0)
        BAK_UNIMPLEMENTED("R3D.ASM: a near-plane clip of 0 vertices (the original goes round "
                          "32768 times)");
    if (n < 0 || n > R3D_POLY_MAX)
        past_arrays();
    return n;
}

static s32 lohi(u16 lo, u16 hi) {
    return (s32)((u32)hi << 16 | lo);
}

/* 32-bit coordinates of the polygon arrays and the vertex caches, by byte offset. */
static s32 in_x(u16 off) {
    u16 i = poly_at(off);
    return lohi((u16)g_r3d.aPolyInX[i], g_r3d.aPolyInXHi[i]);
}

static s32 in_y(u16 off) {
    u16 i = poly_at(off);
    return lohi((u16)g_r3d.aPolyInY[i], g_r3d.aPolyInYHi[i]);
}

static s32 clip_x(u16 off) {
    u16 i = poly_at(off);
    return lohi((u16)g_r3d.aPolyClipX[i], g_r3d.aPolyClipXHi[i]);
}

static s32 clip_y(u16 off) {
    u16 i = poly_at(off);
    return lohi((u16)g_r3d.aPolyClipY[i], g_r3d.aPolyClipYHi[i]);
}

static void put_in_x(u16 off, s32 v) {
    u16 i = poly_at(off);
    g_r3d.aPolyInX[i] = (s16)v;
    g_r3d.aPolyInXHi[i] = (u16)((u32)v >> 16);
}

static void put_in_y(u16 off, s32 v) {
    u16 i = poly_at(off);
    g_r3d.aPolyInY[i] = (s16)v;
    g_r3d.aPolyInYHi[i] = (u16)((u32)v >> 16);
}

static void put_clip_x(u16 off, s32 v) {
    u16 i = poly_at(off);
    g_r3d.aPolyClipX[i] = (s16)v;
    g_r3d.aPolyClipXHi[i] = (u16)((u32)v >> 16);
}

static void put_clip_y(u16 off, s32 v) {
    u16 i = poly_at(off);
    g_r3d.aPolyClipY[i] = (s16)v;
    g_r3d.aPolyClipYHi[i] = (u16)((u32)v >> 16);
}

static s32 cam_x(u16 vertex) {
    u16 i = vtx_at(vertex);
    return lohi((u16)g_r3d.aVertexCamX[i], (u16)g_r3d.aVertexCamXHi[i]);
}

static s32 cam_y(u16 vertex) {
    u16 i = vtx_at(vertex);
    return lohi((u16)g_r3d.aVertexCamY[i], (u16)g_r3d.aVertexCamYHi[i]);
}

static s32 cam_z(u16 vertex) {
    u16 i = vtx_at(vertex);
    return lohi((u16)g_r3d.aVertexCamZ[i], (u16)g_r3d.aVertexCamZHi[i]);
}

static void put_scr_x(u16 vertex, s32 v) {
    u16 i = vtx_at(vertex);
    g_r3d.aVertexScrX[i] = (s16)v;
    g_r3d.aVertexScrXHi[i] = (s16)((u32)v >> 16);
}

static void put_scr_y(u16 vertex, s32 v) {
    u16 i = vtx_at(vertex);
    g_r3d.aVertexScrY[i] = (s16)v;
    g_r3d.aVertexScrYHi[i] = (s16)((u32)v >> 16);
}

/* IMUL of two words, then IDIV by a third, as the divide-error handler leaves it. */
static s16 muldiv16(u16 a, u16 b, u16 divisor) {
    return bak_idiv16((s32)(s16)a * (s16)b, (s16)divisor, NULL);
}

/* ---- the near plane, 16 bits ------------------------------------------------------- */

/* BX = clipped, SI = other: the clipped vertex's screen slots take the point where its
 * edge to the other vertex meets the near plane, camera X and Z there (which the plane's
 * projection leaves as they are), in 16-bit arithmetic. */
void r3d_interp_vertex_at_zclip(u16 clipped, u16 other) {
    u16 b = vtx_at(clipped), a = vtx_at(other);
    u16 den = (u16)(g_r3d.aVertexCamY[a] - g_r3d.aVertexCamY[b]);
    u16 num = (u16)(g_abR3dState.nActorFarClipThreshold - g_r3d.aVertexCamY[b]);
    s16 q = muldiv16((u16)(g_r3d.aVertexCamX[a] - g_r3d.aVertexCamX[b]), num, den);
    g_r3d.aVertexScrX[b] = (s16)(g_r3d.aVertexCamX[b] + q);
    q = muldiv16((u16)(g_r3d.aVertexCamZ[a] - g_r3d.aVertexCamZ[b]), num, den);
    g_r3d.aVertexScrY[b] = (s16)(g_r3d.aVertexCamZ[b] + q);
}

static void emit_screen16(u16 vertex, u16 *di) {
    u16 v = vtx_at(vertex);
    g_r3d.aPolyInX[poly_at(*di)] = g_r3d.aVertexScrX[v];
    g_r3d.aPolyInY[poly_at(*di)] = g_r3d.aVertexScrY[v];
    *di = (u16)(*di + 2);
}

/*
 * The ring of polygonVertices against the near plane (clip state 3: behind it), into
 * aPolyInX/Y from the vertices' screen slots: a vertex in front is kept, an edge that
 * crosses gives the clipped point first, then the vertex in front if it enters.
 */
void r3d_polygon_clip_near_z(void) {
    s16 n = ring_count();
    u16 bx = g_r3d.polygonVertices[poly_at((u16)((n - 1) << 1))];
    u16 cx = 0, di = 0;
    do {
        u16 si = g_r3d.polygonVertices[poly_at(cx)];
        unsigned char prev = clip_state(bx), cur = clip_state(si);
        if (cur == CLIPST_BEHIND) {
            if (prev != CLIPST_BEHIND) {
                r3d_interp_vertex_at_zclip(si, bx);
                emit_screen16(si, &di);
            }
        } else if (prev == CLIPST_BEHIND) {
            r3d_interp_vertex_at_zclip(bx, si);
            emit_screen16(bx, &di);
            emit_screen16(si, &di);
        } else {
            emit_screen16(si, &di);
        }
        bx = si;
        cx = (u16)(cx + 2);
    } while ((u16)(cx >> 1) != (u16)g_r3d.nMeshPolyVertexCount);
    g_r3d.nMeshPolyVertexCount = (s16)(di >> 1);
}

/* ---- the near plane, 32 bits ------------------------------------------------------- */

/* SI = other, BP = clipped: r3d_interp_vertex_at_zclip over the 32-bit caches, with the
 * differences cut to 16 bits for the IMUL and IDIV. */
void r3d_interp_vertex_at_zclip_long(u16 other, u16 clipped) {
    u16 num = (u16)((s32)g_abR3dState.nActorFarClipThreshold - cam_y(clipped));
    u16 den = (u16)(cam_y(other) - cam_y(clipped));
    s16 q = muldiv16((u16)(cam_x(other) - cam_x(clipped)), num, den);
    put_scr_x(clipped, (s32)q + cam_x(clipped));
    q = muldiv16((u16)(cam_z(other) - cam_z(clipped)), num, den);
    put_scr_y(clipped, (s32)q + cam_z(clipped));
}

/* SI = other, BP = clipped: the same through r3d_ldiv_helper, which scales the X and Z
 * deltas by the 32-bit fraction of the depth span that reaches the plane. */
void r3d_poly_clip_calc_isect(u16 other, u16 clipped) {
    s32 num = (s32)g_abR3dState.nActorFarClipThreshold - cam_y(clipped);
    s32 den = cam_y(other) - cam_y(clipped);
    s32 b = cam_z(other) - cam_z(clipped);
    s32 a = cam_x(other) - cam_x(clipped);
    r3d_ldiv_helper(&a, &b, den, num);
    put_scr_x(clipped, a + cam_x(clipped));
    put_scr_y(clipped, b + cam_z(clipped));
}

/* COPYVTX2IN: a vertex's 32-bit screen X and Y into aPolyIn at *di. */
static void emit_screen32(u16 vertex, u16 *di) {
    u16 v = vtx_at(vertex), o = poly_at(*di);
    g_r3d.aPolyInX[o] = g_r3d.aVertexScrX[v];
    g_r3d.aPolyInXHi[o] = (u16)g_r3d.aVertexScrXHi[v];
    g_r3d.aPolyInY[o] = g_r3d.aVertexScrY[v];
    g_r3d.aPolyInYHi[o] = (u16)g_r3d.aVertexScrYHi[v];
    *di = (u16)(*di + 2);
}

/* r3d_polygon_clip_near_z over the 32-bit caches, the crossing from `cross`. */
static void clip_near32(CrossFn *cross) {
    s16 n = ring_count();
    u16 bp = g_r3d.polygonVertices[poly_at((u16)((n - 1) << 1))];
    u16 cx = 0, di = 0;
    do {
        u16 si = g_r3d.polygonVertices[poly_at(cx)];
        unsigned char prev = clip_state(bp), cur = clip_state(si);
        if (cur == CLIPST_BEHIND) {
            if (prev != CLIPST_BEHIND) {
                cross(bp, si);
                emit_screen32(si, &di);
            }
        } else if (prev == CLIPST_BEHIND) {
            cross(si, bp);
            emit_screen32(bp, &di);
            emit_screen32(si, &di);
        } else {
            emit_screen32(si, &di);
        }
        bp = si;
        cx = (u16)(cx + 2);
    } while ((u16)(cx >> 1) != (u16)g_r3d.nMeshPolyVertexCount);
    g_r3d.nMeshPolyVertexCount = (s16)(di >> 1);
}

void r3d_polygon_clip_against_zplane(void) {
    clip_near32(r3d_interp_vertex_at_zclip_long);
}

void r3d_polygon_clip_near_z_lod2(void) {
    clip_near32(r3d_poly_clip_calc_isect);
}

/* ---- the viewport's centre --------------------------------------------------------- */

/* The count is taken unsigned, as LOOP counts. */
static u16 offset_count(void) {
    u16 n = (u16)g_r3d.nMeshPolyVertexCount;
    if (n > R3D_POLY_MAX)
        past_arrays();
    return n;
}

void r3d_apply_viewport_offset(void) {
    u16 n = offset_count();
    s16 cx = g_abR3dState.nViewportCenterX, cy = g_abR3dState.nViewportCenterY;
    for (u16 i = 0; i < n; i++) {
        g_r3d.aPolyInX[i] = (s16)(cx + g_r3d.aPolyInX[i]);
        g_r3d.aPolyInY[i] = (s16)(cy - g_r3d.aPolyInY[i]);
    }
}

void r3d_apply_viewport_offset_long(void) {
    u16 n = offset_count();
    for (u16 i = 0; i < n; i++)
        put_in_x((u16)(2 * i), (s32)g_abR3dState.nViewportCenterX + in_x((u16)(2 * i)));
    for (u16 i = 0; i < n; i++)
        put_in_y((u16)(2 * i), (s32)g_abR3dState.nViewportCenterY - in_y((u16)(2 * i)));
}

/* ---- the clip rectangle, 16 bits --------------------------------------------------- */

/* Signed compares. */
static unsigned char outcode_x16(s16 x) {
    unsigned char code = 0;
    if (x < g_graphics_context.clip.xmin)
        code |= OC_XMIN;
    if (x > g_graphics_context.clip.xmax)
        code |= OC_XMAX;
    return code;
}

static unsigned char outcode_y16(s16 y) {
    unsigned char code = 0;
    if (y > g_graphics_context.clip.ymax)
        code |= OC_YMAX;
    if (y < g_graphics_context.clip.ymin)
        code |= OC_YMIN;
    return code;
}

/* The crossing of x = clip on the edge from `from` to `to` (aPolyIn), into aPolyClip at
 * *di: x first, then y = from.y + (to.y - from.y) * (clip - from.x) / (to.x - from.x),
 * all in words. */
static void cross_x16(s16 clip, u16 from, u16 to, u16 *di) {
    g_r3d.aPolyClipX[poly_at(*di)] = clip;
    u16 num = (u16)(clip - g_r3d.aPolyInX[poly_at(from)]);
    u16 dy = (u16)(g_r3d.aPolyInY[poly_at(to)] - g_r3d.aPolyInY[poly_at(from)]);
    u16 den = (u16)(g_r3d.aPolyInX[poly_at(to)] - g_r3d.aPolyInX[poly_at(from)]);
    s16 q = muldiv16(dy, num, den);
    g_r3d.aPolyClipY[poly_at(*di)] = (s16)(q + g_r3d.aPolyInY[poly_at(from)]);
    *di = (u16)(*di + 2);
}

/* The same for y = clip, from aPolyClip into aPolyIn. */
static void cross_y16(s16 clip, u16 from, u16 to, u16 *di) {
    g_r3d.aPolyInY[poly_at(*di)] = clip;
    u16 num = (u16)(clip - g_r3d.aPolyClipY[poly_at(from)]);
    u16 dx = (u16)(g_r3d.aPolyClipX[poly_at(to)] - g_r3d.aPolyClipX[poly_at(from)]);
    u16 den = (u16)(g_r3d.aPolyClipY[poly_at(to)] - g_r3d.aPolyClipY[poly_at(from)]);
    s16 q = muldiv16(dx, num, den);
    g_r3d.aPolyInX[poly_at(*di)] = (s16)(q + g_r3d.aPolyClipX[poly_at(from)]);
    *di = (u16)(*di + 2);
}

/* The crossing of the edge `from` - `to` with the side a vertex's outcode names (xmin
 * before xmax), if any. */
static void cross_x_side(unsigned char code, u16 from, u16 to, u16 *di) {
    if (code & OC_XMIN)
        cross_x16(g_graphics_context.clip.xmin, from, to, di);
    else if (code & OC_XMAX)
        cross_x16(g_graphics_context.clip.xmax, from, to, di);
}

static void cross_y_side(unsigned char code, u16 from, u16 to, u16 *di) {
    if (code & OC_YMIN)
        cross_y16(g_graphics_context.clip.ymin, from, to, di);
    else if (code & OC_YMAX)
        cross_y16(g_graphics_context.clip.ymax, from, to, di);
}

/*
 * aPolyInX/Y against the clip rectangle, in words: x then y, through aPolyClip and back.
 * An edge with both ends out on one side gives nothing; one end out gives its crossing,
 * and the end inside if it enters; both out on opposite sides gives two crossings. When
 * fewer than 2 vertices are left after x, they are copied back and y is not clipped; as
 * a ring has as many entries as exits, a pass never leaves just 1, so that copies none.
 */
void r3d_polygon_scissor_2d(void) {
    s16 n = g_r3d.nMeshPolyVertexCount;
    u16 si, bx, di;
    unsigned char cl, ch;

    if (n < 2)
        return;
    if (n > R3D_POLY_MAX)
        past_arrays();
    di = 0;
    bx = (u16)((n - 1) << 1);
    cl = outcode_x16(g_r3d.aPolyInX[poly_at(bx)]);
    si = 0;
    do {
        u16 cur = (u16)(si << 1);
        ch = outcode_x16(g_r3d.aPolyInX[poly_at(cur)]);
        if ((cl | ch) == 0) {
            g_r3d.aPolyClipX[poly_at(di)] = g_r3d.aPolyInX[poly_at(cur)];
            g_r3d.aPolyClipY[poly_at(di)] = g_r3d.aPolyInY[poly_at(cur)];
            di = (u16)(di + 2);
        } else if ((cl & ch) == 0) {
            if (cl == 0) {
                cross_x_side(ch, cur, bx, &di);
            } else if (ch == 0) {
                cross_x_side(cl, bx, cur, &di);
                g_r3d.aPolyClipX[poly_at(di)] = g_r3d.aPolyInX[poly_at(cur)];
                g_r3d.aPolyClipY[poly_at(di)] = g_r3d.aPolyInY[poly_at(cur)];
                di = (u16)(di + 2);
            } else {
                cross_x_side(cl, bx, cur, &di);
                cross_x_side(ch, cur, bx, &di);
            }
        }
        bx = cur;
        cl = ch;
        si = (u16)((cur >> 1) + 1);
    } while (si != (u16)g_r3d.nMeshPolyVertexCount);

    n = (s16)(di >> 1);
    g_r3d.nMeshPolyVertexCount = n;
    if (n < 2) {
        for (s16 i = 0; i < n; i++)
            g_r3d.aPolyInX[i] = g_r3d.aPolyClipX[i];
        for (s16 i = 0; i < n; i++)
            g_r3d.aPolyInY[i] = g_r3d.aPolyClipY[i];
        return;
    }

    di = 0;
    bx = (u16)((n - 1) << 1);
    cl = outcode_y16(g_r3d.aPolyClipY[poly_at(bx)]);
    si = 0;
    do {
        u16 cur = (u16)(si << 1);
        ch = outcode_y16(g_r3d.aPolyClipY[poly_at(cur)]);
        if ((cl | ch) == 0) {
            g_r3d.aPolyInX[poly_at(di)] = g_r3d.aPolyClipX[poly_at(cur)];
            g_r3d.aPolyInY[poly_at(di)] = g_r3d.aPolyClipY[poly_at(cur)];
            di = (u16)(di + 2);
        } else if ((cl & ch) == 0) {
            if (cl == 0) {
                cross_y_side(ch, cur, bx, &di);
            } else if (ch == 0) {
                cross_y_side(cl, bx, cur, &di);
                g_r3d.aPolyInX[poly_at(di)] = g_r3d.aPolyClipX[poly_at(cur)];
                g_r3d.aPolyInY[poly_at(di)] = g_r3d.aPolyClipY[poly_at(cur)];
                di = (u16)(di + 2);
            } else {
                cross_y_side(cl, bx, cur, &di);
                cross_y_side(ch, cur, bx, &di);
            }
        }
        bx = cur;
        cl = ch;
        si = (u16)((cur >> 1) + 1);
    } while (si != (u16)g_r3d.nMeshPolyVertexCount);
    g_r3d.nMeshPolyVertexCount = (s16)(di >> 1);
}

/* ---- the clip rectangle, 32 bits --------------------------------------------------- */

/* A 32-bit coordinate against two bounds: by the high word's sign, then with the low
 * word compared unsigned (a bound below 0 counts as above 32767). */
static unsigned char outcode32(s32 v, s16 lowBound, s16 highBound, unsigned char lowBit,
                               unsigned char highBit) {
    s16 hi = (s16)((u32)v >> 16);
    u16 lo = (u16)v;
    unsigned char code = 0;
    if (hi < 0 || (hi == 0 && lo < (u16)lowBound))
        code |= lowBit;
    if (hi > 0 || (hi == 0 && lo > (u16)highBound))
        code |= highBit;
    return code;
}

static unsigned char outcode_x32(s32 x) {
    return outcode32(x, g_graphics_context.clip.xmin, g_graphics_context.clip.xmax, OC_XMIN,
                     OC_XMAX);
}

static unsigned char outcode_y32(s32 y) {
    return outcode32(y, g_graphics_context.clip.ymin, g_graphics_context.clip.ymax, OC_YMIN,
                     OC_YMAX);
}

/* The crossing of x = clip (xmin for side bit 0, else xmax for bit 1, else none) on the
 * edge from origin (BP) to other (SI) of aPolyIn, into aPolyClip at out (DI); returns the
 * next output offset. The y step is a word IMUL and IDIV of the differences' low words. */
u16 r3d_clip_interp_x_edge(unsigned char side, u16 other, u16 out, u16 origin) {
    s16 clip;
    if (side & OC_XMIN)
        clip = g_graphics_context.clip.xmin;
    else if (side & OC_XMAX)
        clip = g_graphics_context.clip.xmax;
    else
        return out;
    put_clip_x(out, clip);
    u16 num = (u16)((s32)clip - in_x(origin));
    u16 den = (u16)(in_x(other) - in_x(origin));
    u16 dy = (u16)(in_y(other) - in_y(origin));
    put_clip_y(out, (s32)muldiv16(dy, num, den) + in_y(origin));
    return (u16)(out + 2);
}

/* The same for y = clip (ymin for bit 3, else ymax for bit 2), from aPolyClip into aPolyIn. */
u16 r3d_clip_interp_y_edge(unsigned char side, u16 other, u16 out, u16 origin) {
    s16 clip;
    if (side & OC_YMIN)
        clip = g_graphics_context.clip.ymin;
    else if (side & OC_YMAX)
        clip = g_graphics_context.clip.ymax;
    else
        return out;
    put_in_y(out, clip);
    u16 num = (u16)((s32)clip - clip_y(origin));
    u16 den = (u16)(clip_y(other) - clip_y(origin));
    u16 dx = (u16)(clip_x(other) - clip_x(origin));
    put_in_x(out, (s32)muldiv16(dx, num, den) + clip_x(origin));
    return (u16)(out + 2);
}

/* r3d_clip_interp_x_edge's crossing in 32 bits: r3d_ldiv_helper scales the y difference. */
u16 r3d_polygon_clip_edge_intersect(unsigned char side, u16 other, u16 out, u16 origin) {
    s16 clip;
    if (side & OC_XMIN)
        clip = g_graphics_context.clip.xmin;
    else if (side & OC_XMAX)
        clip = g_graphics_context.clip.xmax;
    else
        return out;
    put_clip_x(out, clip);
    s32 num = (s32)clip - in_x(origin);
    s32 den = in_x(other) - in_x(origin);
    s32 b = 0;
    s32 a = in_y(other) - in_y(origin);
    r3d_ldiv_helper(&a, &b, den, num);
    put_clip_y(out, a + in_y(origin));
    return (u16)(out + 2);
}

u16 r3d_clip_polygon_edge_y(unsigned char side, u16 other, u16 out, u16 origin) {
    s16 clip;
    if (side & OC_YMIN)
        clip = g_graphics_context.clip.ymin;
    else if (side & OC_YMAX)
        clip = g_graphics_context.clip.ymax;
    else
        return out;
    put_in_y(out, clip);
    s32 num = (s32)clip - clip_y(origin);
    s32 den = clip_y(other) - clip_y(origin);
    s32 b = 0;
    s32 a = clip_x(other) - clip_x(origin);
    r3d_ldiv_helper(&a, &b, den, num);
    put_in_x(out, a + clip_x(origin));
    return (u16)(out + 2);
}

/*
 * The 32-bit polygon in aPolyIn against the clip rectangle, as r3d_polygon_scissor_2d
 * does it, the crossings from xEdge and yEdge, each called with the vertex outside as its
 * origin. Its copy back of fewer than 2 vertices after x (which copies none, see above)
 * would take only their low words.
 */
static void clip_rect32(EdgeFn *xEdge, EdgeFn *yEdge) {
    s16 n = g_r3d.nMeshPolyVertexCount;
    u16 si, bp, di;
    unsigned char cl, ch;

    if (n < 2)
        return;
    if (n > R3D_POLY_MAX)
        past_arrays();
    di = 0;
    bp = (u16)((n - 1) << 1);
    cl = outcode_x32(in_x(bp));
    si = 0;
    do {
        ch = outcode_x32(in_x(si));
        if ((cl | ch) == 0) {
            put_clip_x(di, in_x(si));
            put_clip_y(di, in_y(si));
            di = (u16)(di + 2);
        } else if ((cl & ch) == 0) {
            if (cl == 0) {
                di = xEdge(ch, bp, di, si);
            } else if (ch == 0) {
                di = xEdge(cl, si, di, bp);
                put_clip_x(di, in_x(si));
                put_clip_y(di, in_y(si));
                di = (u16)(di + 2);
            } else {
                di = xEdge(cl, si, di, bp);
                di = xEdge(ch, bp, di, si);
            }
        }
        bp = si;
        cl = ch;
        si = (u16)(si + 2);
    } while ((u16)(si >> 1) != (u16)g_r3d.nMeshPolyVertexCount);

    n = (s16)(di >> 1);
    g_r3d.nMeshPolyVertexCount = n;
    if (n < 2) {
        for (s16 i = 0; i < n; i++)
            g_r3d.aPolyInX[i] = g_r3d.aPolyClipX[i];
        for (s16 i = 0; i < n; i++)
            g_r3d.aPolyInY[i] = g_r3d.aPolyClipY[i];
        return;
    }

    di = 0;
    bp = (u16)((n - 1) << 1);
    cl = outcode_y32(clip_y(bp));
    si = 0;
    do {
        ch = outcode_y32(clip_y(si));
        if ((cl | ch) == 0) {
            put_in_x(di, clip_x(si));
            put_in_y(di, clip_y(si));
            di = (u16)(di + 2);
        } else if ((cl & ch) == 0) {
            if (cl == 0) {
                di = yEdge(ch, bp, di, si);
            } else if (ch == 0) {
                di = yEdge(cl, si, di, bp);
                put_in_x(di, clip_x(si));
                put_in_y(di, clip_y(si));
                di = (u16)(di + 2);
            } else {
                di = yEdge(cl, si, di, bp);
                di = yEdge(ch, bp, di, si);
            }
        }
        bp = si;
        cl = ch;
        si = (u16)(si + 2);
    } while ((u16)(si >> 1) != (u16)g_r3d.nMeshPolyVertexCount);
    g_r3d.nMeshPolyVertexCount = (s16)(di >> 1);
}

void r3d_clip_polygon_to_viewport(void) {
    clip_rect32(r3d_clip_interp_x_edge, r3d_clip_interp_y_edge);
}

void r3d_polygon_clip_rect_2d(void) {
    clip_rect32(r3d_polygon_clip_edge_intersect, r3d_clip_polygon_edge_y);
}
