/*
 * R3D.ASM in C, routine by routine as the game reaches them; the others trap. The state is
 * R3DDATA.ASM's (r3ddata.h). Matrices are Q14 (4000h = 1.0) and angles 16-bit binary. A
 * product of two Q14 values is renormalized as the original's Q14NORM does, keeping bits
 * 14-29 of the 32-bit product or of a sum of products, which wraps as its 32-bit adds do.
 *
 * The original reads its inputs through their pointers again after each store, so the
 * translations keep its order of reads and stores: overlapping inputs and outputs come
 * out the same.
 */
#include "r3ddata.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "r3d.h"
#include "SRC/R3D/CORE/R3D.H"
#include "bak_game_end.h"

#define Q14_ONE 0x4000

#define MAT3T_IDENTITY 0
#define MAT3T_ZROT 1
#define MAT3T_FULL 3

/* Q14NORM: bits 14-29 of a 32-bit product, or of a sum of them. */
static s16 q14(u32 v) {
    return (s16)(u16)(v >> 14);
}

static u32 mul(s16 a, s16 b) {
    return (u32)((s32)a * b);
}

/* FMUL: a Q14 product. */
static s16 fmul(s16 a, s16 b) {
    return q14(mul(a, b));
}

/* Half a cosine or sine (the table read, then SAR 1): the matrix builder adds them in
 * pairs. The sine is the cosine a quarter turn on (angle + C000h). */
static s16 hcos(int angle) {
    return (s16)(r3d_tbl_cos((s16)angle) >> 1);
}

static s16 hsin(int angle) {
    return (s16)(r3d_tbl_sin((s16)angle) >> 1);
}

/* ---- matrices ---------------------------------------------------------------------- */

/*
 * The rotation for three angles. The type says what the matrix holds: 3 when the first
 * angle is set, 2 when only the second and third may be, 1 for the third alone (a turn
 * about Z), 0 for none (the identity). Types 2 and 3 take the full path.
 */
Mat3x3 *r3d_build_rotation_matrix(s16 *angles, Mat3x3 *matrix) {
    s16 *a = angles;
    s16 *m = matrix->pM;
    s16 cz, p, q;

    matrix->bType = a[0] != 0 ? MAT3T_FULL : a[1] != 0 ? 2 : a[2] != 0 ? MAT3T_ZROT : 0;
    if (matrix->bType == MAT3T_IDENTITY) {
        m[0] = m[4] = m[8] = Q14_ONE;
        m[1] = m[2] = m[3] = m[5] = m[6] = m[7] = 0;
    } else if (matrix->bType == MAT3T_ZROT) {
        m[0] = r3d_tbl_cos(a[2]);
        m[4] = m[0];
        m[1] = r3d_tbl_sin(a[2]);
        m[3] = (s16)-m[1];
        m[2] = m[5] = m[6] = m[7] = 0;
        m[8] = Q14_ONE;
    } else {
        cz = r3d_tbl_sin(a[0]);
        m[5] = cz;
        p = (s16)(hcos(a[1] - a[2]) + hcos(a[1] + a[2]));
        q = (s16)(hcos(a[1] - a[2]) - hcos(a[1] + a[2]));
        m[0] = (s16)(p - fmul(cz, q));
        m[7] = (s16)(q - fmul(cz, p));
        p = (s16)(hsin(a[2] + a[1]) + hsin(a[2] - a[1]));
        q = (s16)(hsin(a[1] + a[2]) + hsin(a[1] - a[2]));
        m[1] = (s16)(fmul(cz, q) + p);
        m[6] = (s16)(fmul(cz, p) + q);
        q = hcos(a[0] + a[2]);
        m[4] = (s16)(hcos(a[0] - a[2]) + q);
        q = hcos(a[0] + a[1]);
        m[8] = (s16)(hcos(a[0] - a[1]) + q);
        q = hsin(a[1] - a[0]);
        m[2] = (s16) - (hsin(a[1] + a[0]) + q);
        q = hsin(a[2] - a[0]);
        m[3] = (s16) - (hsin(a[2] + a[0]) + q);
    }
    return matrix;
}

/*
 * r3d_matrix3x3_transform_vec3 (AX = v, BX = m, CX = out): out = v through m's columns.
 * Type 1 turns X and Y and keeps Z; a type above 1 takes all three rows, any other is
 * the identity. The test is a signed compare: a type byte from 80h up is the identity.
 */
static void transform_vec3(const s16 *v, const Mat3x3 *mat, s16 *out) {
    const s16 *m = mat->pM;
    signed char type = (signed char)mat->bType;
    if (type > MAT3T_ZROT) {
        out[0] = q14(mul(m[0], v[0]) + mul(m[3], v[1]) + mul(m[6], v[2]));
        out[1] = q14(mul(m[1], v[0]) + mul(m[4], v[1]) + mul(m[7], v[2]));
        out[2] = q14(mul(m[2], v[0]) + mul(m[5], v[1]) + mul(m[8], v[2]));
    } else if (type == MAT3T_ZROT) {
        out[0] = q14(mul(m[0], v[0]) + mul(m[3], v[1]));
        out[1] = q14(mul(m[1], v[0]) + mul(m[4], v[1]));
        out[2] = v[2];
    } else {
        out[0] = v[0];
        out[1] = v[1];
        out[2] = v[2];
    }
}

void r3d_matrix3x3_transform_vec3(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_matrix3x3_transform_vec3 (register arguments: R3D's own "
                      "routines call its C form)");
}

s16 *r3d_mat3_xform_vec3_wrap(s16 *vec, Mat3x3 *matrix, s16 *out_vec) {
    transform_vec3(vec, matrix, out_vec);
    return out_vec;
}

/* ---- lighting ---------------------------------------------------------------------- */

/* The world's light: +Y (200h, Q9) turned by the elevation about X and the azimuth about Z. */
void r3d_light_set_direction(short elevation, short azimuth) {
    s16 *in = g_r3d.matIn;
    in[0] = elevation;
    in[1] = 0;
    in[2] = azimuth;
    r3d_build_rotation_matrix(in, &g_r3d.mat3x3ScratchRot);
    in[0] = 0;
    in[2] = 0;
    in[1] = 0x200;
    transform_vec3(in, &g_r3d.mat3x3ScratchRot, g_r3d.vec3LightDirWorld);
}

/* ---- not translated yet ------------------------------------------------------------ */

void r3d_setup_actor_in_camera_space(WorldObject *entry, unsigned short entry_seg) {
    (void)entry;
    (void)entry_seg;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_setup_actor_in_camera_space");
}

void r3d_compose_actor_camera_matrix(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_compose_actor_camera_matrix");
}

void r3d_actor_compute_lod_z(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_actor_compute_lod_z");
}

s32 r3d_backface_dot_indexed(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_backface_dot_indexed");
}

void r3d_mesh_part_polygons(unsigned char *part_header) {
    (void)part_header;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_mesh_part_polygons");
}

void r3d_poly_emit_screen_or_world(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_poly_emit_screen_or_world");
}

void r3d_camera_setup_view(ViewContext *widget) {
    (void)widget;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_camera_setup_view");
}

s16 *r3d_matrix_to_euler(Mat3x3 *matrix3x3, s16 *out_xyz) {
    (void)matrix3x3;
    (void)out_xyz;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_matrix_to_euler");
}

Mat3x3 *r3d_matrix3x3_transpose(Mat3x3 *m) {
    (void)m;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_matrix3x3_transpose");
}

Mat3x3 *r3d_matrix3x3_multiply(Mat3x3 *pMatA, Mat3x3 *pMatB, Mat3x3 *pOut) {
    (void)pMatA;
    (void)pMatB;
    (void)pOut;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_matrix3x3_multiply");
}

void r3d_polygon_resolve_shade_color(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_resolve_shade_color");
}

void r3d_polygon_resolve_shade_color_lit(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_resolve_shade_color_lit");
}

void r3d_polygon_submit_to_rasterizer(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_submit_to_rasterizer");
}

void r3d_polygon_submit_textured_quad(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_submit_textured_quad");
}

void r3d_ldiv_helper(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_ldiv_helper");
}

void r3d_mat3_xform_vec3_long(s16 *vec, Mat3x3 *matrix, s32 *out) {
    (void)vec;
    (void)matrix;
    (void)out;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_mat3_xform_vec3_long");
}

void r3d_mesh_polygons(unsigned char *part_header) {
    (void)part_header;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_mesh_polygons");
}

void r3d_mesh_part_polygons_lod3(unsigned short *part_header) {
    (void)part_header;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_mesh_part_polygons_lod3");
}

s32 *r3d_vec2_long_translate_yflip(s32 *pt) {
    (void)pt;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_vec2_long_translate_yflip");
}

PerspDiv2Result *r3d_vec2_perspective_divide(PerspDiv2Args *args, PerspDiv2Result *out) {
    (void)args;
    (void)out;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_vec2_perspective_divide");
}

s32 r3d_actor_proj_scale_compute(PerspDiv2Args *args) {
    (void)args;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_actor_proj_scale_compute");
}

s32 r3d_imul_full32(s16 a, s16 b) {
    (void)a;
    (void)b;
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_imul_full32");
}

s32 r3d_dot3_coeff16_vec32(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_dot3_coeff16_vec32");
}

void r3d_mat3_xform_vec3_ll(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_mat3_xform_vec3_ll");
}

void r3d_polygon_scissor_2d(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_scissor_2d");
}

void r3d_interp_vertex_at_zclip(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_interp_vertex_at_zclip");
}

void r3d_polygon_clip_near_z(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_clip_near_z");
}

void r3d_apply_viewport_offset(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_apply_viewport_offset");
}

void r3d_clip_interp_x_edge(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_clip_interp_x_edge");
}

void r3d_clip_interp_y_edge(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_clip_interp_y_edge");
}

void r3d_clip_polygon_to_viewport(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_clip_polygon_to_viewport");
}

void r3d_interp_vertex_at_zclip_long(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_interp_vertex_at_zclip_long");
}

void r3d_polygon_clip_against_zplane(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_clip_against_zplane");
}

void r3d_polygon_clip_edge_intersect(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_clip_edge_intersect");
}

void r3d_clip_polygon_edge_y(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_clip_polygon_edge_y");
}

void r3d_polygon_clip_rect_2d(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_clip_rect_2d");
}

void r3d_poly_clip_calc_isect(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_poly_clip_calc_isect");
}

void r3d_polygon_clip_near_z_lod2(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_clip_near_z_lod2");
}

void r3d_apply_viewport_offset_long(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_apply_viewport_offset_long");
}
