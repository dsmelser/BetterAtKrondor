/* R3D.ASM: the renderer core (port/asm/r3d.c). The routines' pointer arguments point into
 * the test's memory, where the test places them (overlapping too). */
#include <string.h>

#include "difftest.h"
#include "../../asm/r3ddata.h"

#include "bak_game_begin.h"
#include "structs.h"
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
    {"g_abLightingLut", NULL, g_r3d.abLightingLut, sizeof g_r3d.abLightingLut},
    DT_END,
};
