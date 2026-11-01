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
    {"g_abLightingLut", NULL, g_r3d.abLightingLut, sizeof g_r3d.abLightingLut},
    DT_END,
};
