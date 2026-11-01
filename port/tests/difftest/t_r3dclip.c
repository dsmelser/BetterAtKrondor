/* R3D.ASM's polygon clippers (port/asm/r3dclip.c). The test moves the renderer's polygon
 * arrays and vertex caches as two blocks laid out as in the original's segment: the low
 * words from aPolyInX to the end of aVertexScrY, the high words from aPolyInXHi to the end
 * of aVertexScrYHi. */
#include <stddef.h>

#include "difftest.h"
#include "../../asm/r3ddata.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "r3d.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/R3D/CORE/R3D.H"
#include "bak_game_end.h"

#define LO_SIZE (offsetof(R3dWork, mat3x3ScratchRot) - offsetof(R3dWork, aPolyInX))
#define HI_SIZE (offsetof(R3dWork, abLightingLut) - offsetof(R3dWork, aPolyInXHi))

/* r3dclip.set <s16 clip xmin, xmax, ymin, ymax> <s16 view-plane distance>
 *             <s16 viewport centre x, y> */
static void cmd_set(DtIn *in, DtOut *out) {
    (void)out;
    g_graphics_context.clip.xmin = (s16)dt_u16(in);
    g_graphics_context.clip.xmax = (s16)dt_u16(in);
    g_graphics_context.clip.ymin = (s16)dt_u16(in);
    g_graphics_context.clip.ymax = (s16)dt_u16(in);
    g_abR3dState.nActorFarClipThreshold = (s16)dt_u16(in);
    g_abR3dState.nViewportCenterX = (s16)dt_u16(in);
    g_abR3dState.nViewportCenterY = (s16)dt_u16(in);
}

/* r3dclip.call <u8 routine> <u16 CL BX SI DI BP> -> u16 DI after (the edge routines), else 0 */
static void cmd_call(DtIn *in, DtOut *out) {
    uint8_t routine = dt_u8(in);
    unsigned char cl = (unsigned char)dt_u16(in);
    u16 bx = dt_u16(in), si = dt_u16(in), di = dt_u16(in), bp = dt_u16(in);
    u16 result = 0;
    switch (routine) {
    case 0:
        r3d_polygon_scissor_2d();
        break;
    case 1:
        r3d_interp_vertex_at_zclip(bx, si);
        break;
    case 2:
        r3d_polygon_clip_near_z();
        break;
    case 3:
        r3d_apply_viewport_offset();
        break;
    case 4:
        result = r3d_clip_interp_x_edge(cl, si, di, bp);
        break;
    case 5:
        result = r3d_clip_interp_y_edge(cl, si, di, bp);
        break;
    case 6:
        r3d_clip_polygon_to_viewport();
        break;
    case 7:
        r3d_interp_vertex_at_zclip_long(si, bp);
        break;
    case 8:
        r3d_polygon_clip_against_zplane();
        break;
    case 9:
        result = r3d_polygon_clip_edge_intersect(cl, si, di, bp);
        break;
    case 10:
        result = r3d_clip_polygon_edge_y(cl, si, di, bp);
        break;
    case 11:
        r3d_polygon_clip_rect_2d();
        break;
    case 12:
        r3d_poly_clip_calc_isect(si, bp);
        break;
    case 13:
        r3d_polygon_clip_near_z_lod2();
        break;
    case 14:
        r3d_apply_viewport_offset_long();
        break;
    default:
        dt_fail("no clipper %u", routine);
    }
    dt_put_u16(out, result);
}

const DtEntry dt_r3dclip_entries[] = {
    DT_COMMAND("r3dclip.set", cmd_set),
    DT_COMMAND("r3dclip.call", cmd_call),
    {"r3dclip.lo", NULL, g_r3d.aPolyInX, LO_SIZE},
    {"r3dclip.hi", NULL, g_r3d.aPolyInXHi, HI_SIZE},
    DT_END,
};
