/* R3D.ASM's mesh producers (port/asm/r3d.c) on a generated mesh. The polygon arrays and
 * vertex caches move as test_r3dclip.py's blocks (r3dclip.lo, r3dclip.hi), the renderer's
 * state block as test_r3d.py's; every polygon the producers submit is logged. */
#include <stddef.h>
#include <string.h>

#include "difftest.h"
#include "../../asm/r3ddata.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "r3d.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/R3D/CORE/R3D.H"
#include "SRC/R3D/SCENE/ZONE.H"
#include "SRC/R3D/SKY/SKY.H"
#include "SRC/R3D/SPRITE/WORLDRND.H"
#include "SRC/UI/UIWIDGET.H"
#include "bak_game_end.h"

/* The state block in the original's layout, as in t_r3d.c. */
#define STATE_SIZE 0xad
#define STATE_PTRS 0x85
#define STATE_REST 0x8b

static void state_get(DtIn *in) {
    const uint8_t *b = dt_bytes(in, STATE_SIZE);
    memcpy(&g_abR3dState, b, STATE_PTRS);
    memcpy(&g_abR3dState.nMeshBoundHalfExtent, b + STATE_REST, STATE_SIZE - STATE_REST);
}

/* The pointers read as zeros, and so does the vertex cache's segment (the two sides number
 * their segments differently). */
static void state_put(DtOut *out) {
    uint8_t b[STATE_SIZE];
    memcpy(b, &g_abR3dState, STATE_PTRS);
    memset(b + STATE_PTRS, 0, STATE_REST - STATE_PTRS);
    memcpy(b + STATE_REST, &g_abR3dState.nMeshBoundHalfExtent, STATE_SIZE - STATE_REST);
    b[2] = b[3] = 0;
    dt_put_bytes(out, b, sizeof b);
}

/* Each submitted polygon: which filler (1 flat, 2 textured), its first argument, the
 * context's corner count and arrays, and its fill flag and colours. */
#define LOG_ENTRY (1 + 2 + 2 + 40 + 40 + 3)
static uint8_t s_log[LOG_ENTRY * 400];
static size_t s_logLen;
static uint16_t s_logCount;

static void log_draw(uint8_t which, uint16_t first, s16 *xs, s16 *ys) {
    uint8_t *p = s_log + s_logLen;
    uint16_t count = (uint16_t)g_graphics_context.nPolygonClipVertexCount;
    if (s_logLen + LOG_ENTRY > sizeof s_log)
        dt_fail("more polygons than the log holds");
    if (xs != g_graphics_context.pPolygonX || ys != g_graphics_context.pPolygonY)
        dt_fail("a polygon not in the context's arrays");
    p[0] = which;
    memcpy(p + 1, &first, 2);
    memcpy(p + 3, &count, 2);
    memcpy(p + 5, g_graphics_context.pPolygonX, 40);
    memcpy(p + 45, g_graphics_context.pPolygonY, 40);
    p[85] = g_graphics_context.bGfx_fill_enabled;
    p[86] = g_graphics_context.bGfx_fill_color;
    p[87] = g_graphics_context.bGfx_outline_color;
    s_logLen += LOG_ENTRY;
    s_logCount++;
}

static void log_polygon(u16 vcount, s16 *xs, s16 *ys) {
    log_draw(1, vcount, xs, ys);
}

static void log_quad(s16 texture, s16 *xs, s16 *ys) {
    log_draw(2, (uint16_t)texture, xs, ys);
}

/*
 * r3dmesh.run <u8 producer: 0 LOD 0, 1 LODs 1-2, 3 LOD 3> <u8 vertex cache hit>
 *             <state> <24: g_matXIn, g_matXIn2, g_matXOut, the world's light> <12: g_matXOut32>
 *             <8: R3D's counters and flags> <u8 render flag> <s16 game mode>
 *             <u16 textured polygons> <s16 clip xmin, xmax, ymin, ymax> <u8 remap?> <256: remap>
 *             <the context's fill flag, fill, outline, corner count and arrays before>
 *             <u16 part header's offset> <u16 size> <the mesh: the shape record at 0>
 *   -> the state, the scratch, g_matXOut32, the counters and flags, the context's fill flag,
 *      fill and outline colours, corner count and arrays, u16 polygons logged, the log
 */
static void cmd_run(DtIn *in, DtOut *out) {
    static unsigned char remap[256];
    uint8_t producer = dt_u8(in), hit = dt_u8(in);
    state_get(in);
    memcpy(g_r3d.matIn, dt_bytes(in, 6), 6);
    memcpy(g_r3d.matIn2, dt_bytes(in, 6), 6);
    memcpy(g_r3d.matOut, dt_bytes(in, 6), 6);
    memcpy(g_r3d.vec3LightDirWorld, dt_bytes(in, 6), 6);
    memcpy(g_r3d.matOut32, dt_bytes(in, 12), 12);
    g_r3d.nMeshPartPolygonsRemaining = dt_u16(in);
    g_r3d.bNearClipNeeded = dt_u8(in);
    g_r3d.nMeshPolyRemaining = dt_u16(in);
    g_r3d.bMeshPolyHasZClipVertex = dt_u8(in);
    g_r3d.nPolyListRemaining = dt_u16(in);
    g_graphics_context.bGfxRenderStateFlag = dt_u8(in);
    g_game_mode = (s16)dt_u16(in);
    g_bTexturedPolyEnabled = dt_u16(in);
    g_graphics_context.clip.xmin = (s16)dt_u16(in);
    g_graphics_context.clip.xmax = (s16)dt_u16(in);
    g_graphics_context.clip.ymin = (s16)dt_u16(in);
    g_graphics_context.clip.ymax = (s16)dt_u16(in);
    uint8_t hasRemap = dt_u8(in);
    memcpy(remap, dt_bytes(in, 256), 256);
    g_graphics_context.bGfx_fill_enabled = dt_u8(in);
    g_graphics_context.bGfx_fill_color = dt_u8(in);
    g_graphics_context.bGfx_outline_color = dt_u8(in);
    g_graphics_context.nPolygonClipVertexCount = (s16)dt_u16(in);
    memcpy(g_graphics_context.pPolygonX, dt_bytes(in, 40), 40);
    memcpy(g_graphics_context.pPolygonY, dt_bytes(in, 40), 40);
    uint16_t header = dt_u16(in), size = dt_u16(in);
    unsigned char *mesh = bak_dos_alloc(size, 1);
    memcpy(mesh, dt_bytes(in, size), size);

    g_abR3dState.pCurActorPagedRec = mesh;
    g_abR3dState.pCurMeshPartVisTable = NULL;
    g_r3d.pFaceMaterialTable = NULL;
    g_pColorRemapTable = hasRemap ? remap : NULL;
    if (hit)
        g_abR3dState.dwMeshVertexFarPtrCache =
            (u32)FP_SEG_NORM(mesh) << 16 | g_abR3dState.wMeshVertexPoolOff;
    s_logLen = 0;
    s_logCount = 0;
    g_r3dDrawPolygon = log_polygon;
    g_r3dDrawTexturedQuad = log_quad;
    if (producer == 0)
        r3d_mesh_part_polygons(mesh + header);
    else if (producer == 1)
        r3d_mesh_polygons(mesh + header);
    else
        r3d_mesh_part_polygons_lod3((unsigned short *)(mesh + header));
    g_r3dDrawPolygon = sky_polygon_draw_dispatch;
    g_r3dDrawTexturedQuad = worldrender_textured_quad_lod;
    g_pColorRemapTable = NULL;
    g_abR3dState.pCurActorPagedRec = NULL;
    bak_dos_free(mesh);

    state_put(out);
    dt_put_bytes(out, g_r3d.matIn, 6);
    dt_put_bytes(out, g_r3d.matIn2, 6);
    dt_put_bytes(out, g_r3d.matOut, 6);
    dt_put_bytes(out, g_r3d.vec3LightDirWorld, 6);
    dt_put_bytes(out, g_r3d.matOut32, 12);
    dt_put_u16(out, g_r3d.nMeshPartPolygonsRemaining);
    dt_put_u8(out, g_r3d.bNearClipNeeded);
    dt_put_u16(out, g_r3d.nMeshPolyRemaining);
    dt_put_u8(out, g_r3d.bMeshPolyHasZClipVertex);
    dt_put_u16(out, g_r3d.nPolyListRemaining);
    dt_put_u8(out, g_graphics_context.bGfx_fill_enabled);
    dt_put_u8(out, g_graphics_context.bGfx_fill_color);
    dt_put_u8(out, g_graphics_context.bGfx_outline_color);
    dt_put_u16(out, (uint16_t)g_graphics_context.nPolygonClipVertexCount);
    dt_put_bytes(out, g_graphics_context.pPolygonX, 40);
    dt_put_bytes(out, g_graphics_context.pPolygonY, 40);
    dt_put_u16(out, s_logCount);
    dt_put_bytes(out, s_log, s_logLen);
}

/* r3dmesh.emit: r3d_poly_emit_screen_or_world on the blocks r3dclip.lo and .hi hold */
static void cmd_emit(DtIn *in, DtOut *out) {
    (void)in;
    (void)out;
    r3d_poly_emit_screen_or_world();
}

const DtEntry dt_r3dmesh_entries[] = {
    DT_COMMAND("r3dmesh.run", cmd_run),
    DT_COMMAND("r3dmesh.emit", cmd_emit),
    DT_END,
};
