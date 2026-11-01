/* R3D.ASM: the renderer core (port/asm/r3d.c). The routines' pointer arguments point into
 * the test's memory, where the test places them (overlapping too). */
#include <string.h>

#include "difftest.h"
#include "../../asm/r3ddata.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "r3d.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/R3D/CORE/R3D.H"
#include "SRC/R3D/SKY/SKY.H"
#include "SRC/R3D/SPRITE/WORLDRND.H"
#include "bak_game_end.h"

#define MEM 256

static _Alignas(16) unsigned char s_mem[MEM];

static void *at(DtIn *in) {
    uint8_t off = dt_u8(in);
    if (off & 1)
        dt_fail("odd offset %u", off);
    return s_mem + off;
}

static void put_result(DtOut *out, const void *returned) {
    dt_put_bytes(out, s_mem, MEM);
    dt_put_u16(out, (uint16_t)((const unsigned char *)returned - s_mem));
}

/* r3d.build <u8 angles> <u8 matrix> <MEM bytes> -> the memory, u16 the result's offset */
static void cmd_build(DtIn *in, DtOut *out) {
    s16 *angles = at(in);
    Mat3x3 *matrix = at(in);
    memcpy(s_mem, dt_bytes(in, MEM), MEM);
    put_result(out, r3d_build_rotation_matrix(angles, matrix));
}

/* r3d.xform <u8 vector> <u8 matrix> <u8 out> <MEM bytes> -> the memory, u16 the result's
 * offset */
static void cmd_xform(DtIn *in, DtOut *out) {
    s16 *vec = at(in);
    Mat3x3 *matrix = at(in);
    s16 *result = at(in);
    memcpy(s_mem, dt_bytes(in, MEM), MEM);
    put_result(out, r3d_mat3_xform_vec3_wrap(vec, matrix, result));
}

/* r3d.mul <u8 a> <u8 b> <u8 out> <MEM bytes> -> the memory, u16 the result's offset */
static void cmd_mul(DtIn *in, DtOut *out) {
    Mat3x3 *a = at(in);
    Mat3x3 *b = at(in);
    Mat3x3 *result = at(in);
    memcpy(s_mem, dt_bytes(in, MEM), MEM);
    put_result(out, r3d_matrix3x3_multiply(a, b, result));
}

/* r3d.transpose <u8 matrix> <MEM bytes> -> the memory, u16 the result's offset */
static void cmd_transpose(DtIn *in, DtOut *out) {
    Mat3x3 *matrix = at(in);
    memcpy(s_mem, dt_bytes(in, MEM), MEM);
    put_result(out, r3d_matrix3x3_transpose(matrix));
}

/* r3d.euler <u8 matrix> <u8 out> <MEM bytes> -> the memory, u16 the result's offset */
static void cmd_euler(DtIn *in, DtOut *out) {
    Mat3x3 *matrix = at(in);
    s16 *angles = at(in);
    memcpy(s_mem, dt_bytes(in, MEM), MEM);
    put_result(out, r3d_matrix_to_euler(matrix, angles));
}

/* The original's near pointers in a view become pointers into this, and back. */
static unsigned char s_nearSpace[0x10000];

/*
 * r3d.camera <s16 zoom> <s16 view rotation[3]> <s16 viewport x, y, width, height>
 *            <u16 face materials> <u16 colour remap> <s32 position[3]> <s16 orientation[3]>
 *            <19 bytes: the actor matrix before> <u8 flags before[3]: g_bCamSetupResetFlag,
 *            g_bSkipFarEdgeCull, g_bActorViewportClip>
 *   -> the camera's position and angles, the shift word, the view-plane distance, the two
 *      tables (as offsets), the clip rectangle, the viewport's centre, the three flags,
 *      g_matXIn..g_matZIn, the scratch, view and actor matrices
 */
static void cmd_camera(DtIn *in, DtOut *out) {
    R3dRendererState *st = &g_abR3dState;
    ViewContext view;
    WorldEntity camera;

    memset(&view, 0, sizeof view);
    memset(&camera, 0, sizeof camera);
    view.zoom = (s16)dt_u16(in);
    view.camera = &camera;
    view.viewRotation.pitch = (s16)dt_u16(in);
    view.viewRotation.roll = (s16)dt_u16(in);
    view.viewRotation.yaw = (s16)dt_u16(in);
    view.viewport.x = (s16)dt_u16(in);
    view.viewport.y = (s16)dt_u16(in);
    view.viewport.width = (s16)dt_u16(in);
    view.viewport.height = (s16)dt_u16(in);
    view.faceMaterials = s_nearSpace + dt_u16(in);
    view.colorRemap = s_nearSpace + dt_u16(in);
    camera.base.pos.xy.nWorld_x = (s32)dt_u32(in);
    camera.base.pos.xy.nWorld_y = (s32)dt_u32(in);
    camera.base.pos.nWorld_z = (s32)dt_u32(in);
    camera.base.orientation.pitch = (s16)dt_u16(in);
    camera.base.orientation.roll = (s16)dt_u16(in);
    camera.base.orientation.yaw = (s16)dt_u16(in);
    memcpy(&st->mat3x3ActorRot, dt_bytes(in, sizeof(Mat3x3)), sizeof(Mat3x3));
    st->bCamSetupResetFlag = dt_u8(in);
    st->bSkipFarEdgeCull = dt_u8(in);
    st->bActorViewportClip = dt_u8(in);

    r3d_camera_setup_view(&view);

    dt_put_bytes(out, st->camCurWorldPos, sizeof st->camCurWorldPos);
    dt_put_u16(out, (uint16_t)st->nCamViewYaw);
    dt_put_u16(out, (uint16_t)st->nCamViewPitch);
    dt_put_u16(out, (uint16_t)st->nCamViewHeading);
    dt_put_u8(out, st->bScreenShift);
    dt_put_u8(out, st->bScreenShiftHi);
    dt_put_u16(out, (uint16_t)st->nActorFarClipThreshold);
    dt_put_u16(out, (uint16_t)(g_r3d.pFaceMaterialTable - s_nearSpace));
    dt_put_u16(out, (uint16_t)(g_pColorRemapTable - s_nearSpace));
    dt_put_bytes(out, &g_graphics_context.clip, sizeof g_graphics_context.clip);
    dt_put_u16(out, (uint16_t)st->nViewportCenterX);
    dt_put_u16(out, (uint16_t)st->nViewportCenterY);
    dt_put_u8(out, st->bCamSetupResetFlag);
    dt_put_u8(out, st->bSkipFarEdgeCull);
    dt_put_u8(out, st->bActorViewportClip);
    dt_put_bytes(out, g_r3d.matIn, sizeof g_r3d.matIn);
    dt_put_bytes(out, &g_r3d.mat3x3ScratchRot, sizeof(Mat3x3));
    dt_put_bytes(out, &st->mat3x3ViewRot, sizeof(Mat3x3));
    dt_put_bytes(out, &st->mat3x3ActorRot, sizeof(Mat3x3));
}

/* r3d.light <s16 elevation> <s16 azimuth> -> g_matXIn..g_matZIn, g_mat3x3ScratchRot,
 * g_vec3LightDirWorld */
static void cmd_light(DtIn *in, DtOut *out) {
    s16 elevation = (s16)dt_u16(in);
    s16 azimuth = (s16)dt_u16(in);
    r3d_light_set_direction(elevation, azimuth);
    dt_put_bytes(out, g_r3d.matIn, sizeof g_r3d.matIn);
    dt_put_bytes(out, &g_r3d.mat3x3ScratchRot, sizeof g_r3d.mat3x3ScratchRot);
    dt_put_bytes(out, g_r3d.vec3LightDirWorld, sizeof g_r3d.vec3LightDirWorld);
}

/* r3d.ldiv <s32 a, s32 b, s32 den, s32 num>... -> s32 a, s32 b after each */
static void cmd_ldiv(DtIn *in, DtOut *out) {
    while (dt_remaining(in) != 0) {
        s32 a = (s32)dt_u32(in), b = (s32)dt_u32(in);
        s32 den = (s32)dt_u32(in), num = (s32)dt_u32(in);
        r3d_ldiv_helper(&a, &b, den, num);
        dt_put_u32(out, (uint32_t)a);
        dt_put_u32(out, (uint32_t)b);
    }
}

/* r3d.persp <u16 view distance> <u8 args> <u8 out> <MEM bytes> -> the memory, u16 the
 * result's offset */
static void cmd_persp(DtIn *in, DtOut *out) {
    g_abR3dState.nActorFarClipThreshold = (s16)dt_u16(in);
    PerspDiv2Args *args = at(in);
    PerspDiv2Result *result = at(in);
    memcpy(s_mem, dt_bytes(in, MEM), MEM);
    put_result(out, r3d_vec2_perspective_divide(args, result));
}

/* r3d.projscale <u16 view distance> <u8 args> <MEM bytes> -> u32 the scale */
static void cmd_projscale(DtIn *in, DtOut *out) {
    g_abR3dState.nActorFarClipThreshold = (s16)dt_u16(in);
    PerspDiv2Args *args = at(in);
    memcpy(s_mem, dt_bytes(in, MEM), MEM);
    dt_put_u32(out, (uint32_t)r3d_actor_proj_scale_compute(args));
}

/* r3d.yflip <s16 centre x> <s16 centre y> <u8 point> <MEM bytes> -> the memory, u16 the
 * result's offset */
static void cmd_yflip(DtIn *in, DtOut *out) {
    g_abR3dState.nViewportCenterX = (s16)dt_u16(in);
    g_abR3dState.nViewportCenterY = (s16)dt_u16(in);
    s32 *pt = at(in);
    memcpy(s_mem, dt_bytes(in, MEM), MEM);
    put_result(out, r3d_vec2_long_translate_yflip(pt));
}

/*
 * The renderer's state block in the original's layout (3778h..3824h): the same as the
 * struct's up to its two pointers, a far one at 85h and a near one at 89h, which natively
 * are wider; they read as zeros here and are set apart.
 */
#define STATE_SIZE 0xad
#define STATE_PTRS 0x85
#define STATE_REST 0x8b
_Static_assert(offsetof(R3dRendererState, pCurActorPagedRec) == STATE_PTRS, "state layout");

static void state_get(DtIn *in) {
    const uint8_t *b = dt_bytes(in, STATE_SIZE);
    memcpy(&g_abR3dState, b, STATE_PTRS);
    memcpy(&g_abR3dState.nMeshBoundHalfExtent, b + STATE_REST, STATE_SIZE - STATE_REST);
}

static void state_put(DtOut *out) {
    static const uint8_t zeros[STATE_REST - STATE_PTRS];
    dt_put_bytes(out, &g_abR3dState, STATE_PTRS);
    dt_put_bytes(out, zeros, sizeof zeros);
    dt_put_bytes(out, &g_abR3dState.nMeshBoundHalfExtent, STATE_SIZE - STATE_REST);
}

/* g_matXIn, g_matXIn2, g_matXOut and the world's light vector */
static void scratch_get(DtIn *in) {
    memcpy(g_r3d.matIn, dt_bytes(in, 6), 6);
    memcpy(g_r3d.matIn2, dt_bytes(in, 6), 6);
    memcpy(g_r3d.matOut, dt_bytes(in, 6), 6);
    memcpy(g_r3d.vec3LightDirWorld, dt_bytes(in, 6), 6);
}

static void scratch_put(DtOut *out) {
    dt_put_bytes(out, g_r3d.matIn, 6);
    dt_put_bytes(out, g_r3d.matIn2, 6);
    dt_put_bytes(out, g_r3d.matOut, 6);
    dt_put_bytes(out, g_r3d.vec3LightDirWorld, 6);
}

/*
 * r3d.actor <u8 step> <state> <scratch> <22 bytes: the WorldObject> <26 bytes: the shape
 * record> -> the state and the scratch after the step: 0 r3d_setup_actor_in_camera_space,
 * 1 r3d_compose_actor_camera_matrix, 2 r3d_actor_compute_lod_z (the record is the actor's)
 */
static void cmd_actor(DtIn *in, DtOut *out) {
    static unsigned char record[26];
    WorldObject entry;
    uint8_t step = dt_u8(in);
    state_get(in);
    scratch_get(in);
    memset(&entry, 0, sizeof entry); /* the original's 22 bytes, field by field */
    memcpy(&entry.shapeId, dt_bytes(in, 2), 2);
    memcpy(&entry.pos, dt_bytes(in, 12), 12);
    memcpy(&entry.orientation, dt_bytes(in, 6), 6);
    memcpy(&entry.state.stateBits, dt_bytes(in, 2), 2);
    memcpy(record, dt_bytes(in, sizeof record), sizeof record);
    g_abR3dState.pCurActorPagedRec = record;
    g_abR3dState.pCurMeshPartVisTable = NULL;
    if (step == 0)
        r3d_setup_actor_in_camera_space(&entry);
    else if (step == 1)
        r3d_compose_actor_camera_matrix();
    else
        r3d_actor_compute_lod_z();
    state_put(out);
    scratch_put(out);
}

/* r3d.dot3 <s16 a[3], s16 b[3]>... -> s32 each (r3d_dot3_long_far) */
static void cmd_dot3(DtIn *in, DtOut *out) {
    while (dt_remaining(in) != 0) {
        s16 v[6];
        memcpy(v, dt_bytes(in, sizeof v), sizeof v);
        dt_put_u32(out, (uint32_t)r3d_dot3_long_far(v, v + 3));
    }
}

/* r3d.xlong <u16 shift> <s16 vec[3]> <19 bytes: the matrix> -> s32 out[3], g_matXOut.. */
static void cmd_xlong(DtIn *in, DtOut *out) {
    s32 result[3];
    g_abR3dState.wMeshScaleShift = dt_u16(in);
    memcpy(g_r3d.matIn, dt_bytes(in, 6), 6);
    memcpy(&g_abR3dState.mat3x3ActorRot, dt_bytes(in, sizeof(Mat3x3)), sizeof(Mat3x3));
    r3d_mat3_xform_vec3_long(g_r3d.matIn, &g_abR3dState.mat3x3ActorRot, result);
    dt_put_bytes(out, result, sizeof result);
    dt_put_bytes(out, g_r3d.matOut, 6);
}

/* The vertex pool the face tests read: 256 rows. */
static unsigned char s_pool[256 * 6];

/* r3d.backface <s16 residual> <s16 camera origin[3]> <u8 normal's vertex> <u8 first vertex>
 * <pool> -> s32 the dot product, g_matXIn2.., g_matXOut.. */
static void cmd_backface(DtIn *in, DtOut *out) {
    g_abR3dState.wMeshScaleResidual = dt_u16(in);
    memcpy(&g_abR3dState.nMeshCamOriginX, dt_bytes(in, 6), 6);
    uint8_t nrm = dt_u8(in), first = dt_u8(in);
    memcpy(s_pool, dt_bytes(in, sizeof s_pool), sizeof s_pool);
    dt_put_u32(out, (uint32_t)r3d_test_backface_dot(s_pool, nrm, first));
    dt_put_bytes(out, g_r3d.matIn2, 6);
    dt_put_bytes(out, g_r3d.matOut, 6);
}

/* r3d.shade <u8 day> <u8 flipped> <s16 light[3]> <u8 material table?> <u8 remap table?>
 * <256: material table> <256: remap table> <8: face> <pool> -> fill enabled, outline,
 * fill, g_matXIn2.. */
static void cmd_shade(DtIn *in, DtOut *out) {
    static unsigned char materials[256], remap[256], face[8];
    g_graphics_context.bGfxRenderStateFlag = dt_u8(in);
    g_abR3dState.bPolyNormalFlipped = dt_u8(in);
    memcpy(g_abR3dState.vec3LightDirObj, dt_bytes(in, 6), 6);
    uint8_t hasMaterials = dt_u8(in), hasRemap = dt_u8(in);
    memcpy(materials, dt_bytes(in, 256), 256);
    memcpy(remap, dt_bytes(in, 256), 256);
    memcpy(face, dt_bytes(in, 8), 8);
    memcpy(s_pool, dt_bytes(in, sizeof s_pool), sizeof s_pool);
    g_r3d.pFaceMaterialTable = hasMaterials ? materials : NULL;
    g_pColorRemapTable = hasRemap ? remap : NULL;
    memset(g_r3d.matIn2, 0, sizeof g_r3d.matIn2);
    r3d_test_resolve_shade_color(face, s_pool);
    g_r3d.pFaceMaterialTable = NULL;
    g_pColorRemapTable = NULL;
    dt_put_u8(out, g_graphics_context.bGfx_fill_enabled);
    dt_put_u8(out, g_graphics_context.bGfx_outline_color);
    dt_put_u8(out, g_graphics_context.bGfx_fill_color);
    dt_put_bytes(out, g_r3d.matIn2, 6);
}

/* What reached the polygon dispatchers: which, its first argument, and whether the corner
 * arrays were the context's. */
static struct {
    uint16_t which, first, ours;
} s_drawn;

static void record_polygon(u16 vcount, s16 *xs, s16 *ys) {
    s_drawn.which = 1;
    s_drawn.first = vcount;
    s_drawn.ours = xs == g_graphics_context.pPolygonX && ys == g_graphics_context.pPolygonY;
}

static void record_quad(s16 texture, s16 *xs, s16 *ys) {
    s_drawn.which = 2;
    s_drawn.first = (uint16_t)texture;
    s_drawn.ours = xs == g_graphics_context.pPolygonX && ys == g_graphics_context.pPolygonY;
}

/* r3d.submit <u8 textured> <u8 day> <u16 corners> <s16 x[20]> <s16 y[20]> <8: face>
 *            <the context's corner count, pPolygonX[20] and pPolygonY[20] before>
 *   -> the context's corner count, pPolygonX[20], pPolygonY[20], u16 which (0 none, 1 the
 *      flat filler, 2 the textured quad), u16 its first argument, u16 1 if the arrays were
 *      the context's */
static void cmd_submit(DtIn *in, DtOut *out) {
    static unsigned char face[8];
    uint8_t textured = dt_u8(in);
    g_graphics_context.bGfxRenderStateFlag = dt_u8(in);
    g_r3d.nMeshPolyVertexCount = (s16)dt_u16(in);
    memcpy(g_r3d.aPolyInX, dt_bytes(in, sizeof g_r3d.aPolyInX), sizeof g_r3d.aPolyInX);
    memcpy(g_r3d.aPolyInY, dt_bytes(in, sizeof g_r3d.aPolyInY), sizeof g_r3d.aPolyInY);
    memcpy(face, dt_bytes(in, 8), 8);
    memcpy(&g_graphics_context.nPolygonClipVertexCount, dt_bytes(in, 2), 2);
    memcpy(g_graphics_context.pPolygonX, dt_bytes(in, 40), 40);
    memcpy(g_graphics_context.pPolygonY, dt_bytes(in, 40), 40);
    memset(&s_drawn, 0, sizeof s_drawn);
    g_r3dDrawPolygon = record_polygon;
    g_r3dDrawTexturedQuad = record_quad;
    if (textured)
        r3d_test_submit_textured_quad(face);
    else
        r3d_test_submit_to_rasterizer();
    g_r3dDrawPolygon = sky_polygon_draw_dispatch;
    g_r3dDrawTexturedQuad = worldrender_textured_quad_lod;
    dt_put_u16(out, (uint16_t)g_graphics_context.nPolygonClipVertexCount);
    dt_put_bytes(out, g_graphics_context.pPolygonX, sizeof g_graphics_context.pPolygonX);
    dt_put_bytes(out, g_graphics_context.pPolygonY, sizeof g_graphics_context.pPolygonY);
    dt_put_u16(out, s_drawn.which);
    dt_put_u16(out, s_drawn.first);
    dt_put_u16(out, s_drawn.ours);
}

/* r3d.ctxoffs -> u16 offsets in the graphics context of bGfxRenderStateFlag, pPolygonX,
 * pPolygonY, nPolygonClipVertexCount */
static void cmd_ctxoffs(DtIn *in, DtOut *out) {
    (void)in;
    dt_put_u16(out, (uint16_t)offsetof(GraphicsContext, bGfxRenderStateFlag));
    dt_put_u16(out, (uint16_t)offsetof(GraphicsContext, pPolygonX));
    dt_put_u16(out, (uint16_t)offsetof(GraphicsContext, pPolygonY));
    dt_put_u16(out, (uint16_t)offsetof(GraphicsContext, nPolygonClipVertexCount));
}

/* r3d.imul <s16 a, s16 b>... -> u32 each */
static void cmd_imul(DtIn *in, DtOut *out) {
    while (dt_remaining(in) != 0) {
        s16 a = (s16)dt_u16(in);
        s16 b = (s16)dt_u16(in);
        dt_put_u32(out, (uint32_t)r3d_imul_full32(a, b));
    }
}

const DtEntry dt_r3d_entries[] = {
    DT_COMMAND("r3d.imul", cmd_imul),
    DT_COMMAND("r3d.build", cmd_build),
    DT_COMMAND("r3d.xform", cmd_xform),
    DT_COMMAND("r3d.light", cmd_light),
    DT_COMMAND("r3d.mul", cmd_mul),
    DT_COMMAND("r3d.transpose", cmd_transpose),
    DT_COMMAND("r3d.euler", cmd_euler),
    DT_COMMAND("r3d.camera", cmd_camera),
    DT_COMMAND("r3d.ldiv", cmd_ldiv),
    DT_COMMAND("r3d.persp", cmd_persp),
    DT_COMMAND("r3d.projscale", cmd_projscale),
    DT_COMMAND("r3d.yflip", cmd_yflip),
    DT_COMMAND("r3d.actor", cmd_actor),
    DT_COMMAND("r3d.dot3", cmd_dot3),
    DT_COMMAND("r3d.xlong", cmd_xlong),
    DT_COMMAND("r3d.backface", cmd_backface),
    DT_COMMAND("r3d.shade", cmd_shade),
    DT_COMMAND("r3d.submit", cmd_submit),
    DT_COMMAND("r3d.ctxoffs", cmd_ctxoffs),
    {"g_abLightingLut", NULL, g_r3d.abLightingLut, sizeof g_r3d.abLightingLut},
    DT_END,
};
