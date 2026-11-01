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
#include "SRC/GEN/GFXCTX.H"
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

/* ---- arithmetic -------------------------------------------------------------------- */

s32 r3d_imul_full32(s16 a, s16 b) {
    return (s32)a * b;
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

/* Swaps the three pairs across the diagonal, each pair's lower element first. */
Mat3x3 *r3d_matrix3x3_transpose(Mat3x3 *m) {
    s16 *p = m->pM;
    s16 t;
    t = p[3];
    p[3] = p[1];
    p[1] = t;
    t = p[6];
    p[6] = p[2];
    p[2] = t;
    t = p[7];
    p[7] = p[5];
    p[5] = t;
    return m;
}

/* One element of a product: row r of a times column c of b, over the first n terms. */
static s16 dot_rc(const s16 *a, const s16 *b, int r, int c, int n) {
    u32 sum = 0;
    int k;
    for (k = 0; k < n; k++)
        sum += mul(a[3 * r + k], b[3 * k + c]);
    return q14(sum);
}

/*
 * out = a * b. Both type bytes are read first. An identity (type 0) copies the other
 * matrix, its type byte first; type 1 (a turn about Z) takes the shorter products, and any
 * other type is a full matrix: the product of two turns about Z is one (type 1), any other
 * product is type 3. The elements are stored in the original's order, the type byte last.
 */
Mat3x3 *r3d_matrix3x3_multiply(Mat3x3 *pMatA, Mat3x3 *pMatB, Mat3x3 *pOut) {
    const s16 *a = pMatA->pM;
    const s16 *b = pMatB->pM;
    s16 *o = pOut->pM;
    unsigned char typeA = pMatA->bType;
    unsigned char typeB = pMatB->bType;
    int i;

    if (typeA == MAT3T_IDENTITY || typeB == MAT3T_IDENTITY) {
        const s16 *src = typeA == MAT3T_IDENTITY ? b : a;
        pOut->bType = typeA == MAT3T_IDENTITY ? typeB : typeA;
        for (i = 0; i < 9; i++) /* REP MOVSW: a word at a time, forwards */
            o[i] = src[i];
    } else if (typeA == MAT3T_ZROT && typeB == MAT3T_ZROT) {
        o[0] = dot_rc(a, b, 0, 0, 2);
        o[1] = dot_rc(a, b, 0, 1, 2);
        o[3] = dot_rc(a, b, 1, 0, 2);
        o[4] = dot_rc(a, b, 1, 1, 2);
        o[2] = 0;
        o[5] = 0;
        o[6] = 0;
        o[7] = 0;
        o[8] = Q14_ONE;
        pOut->bType = MAT3T_ZROT;
    } else if (typeA == MAT3T_ZROT) {
        for (i = 0; i < 6; i++)
            o[i] = dot_rc(a, b, i / 3, i % 3, 2);
        o[6] = b[6];
        o[7] = b[7];
        o[8] = b[8];
        pOut->bType = MAT3T_FULL;
    } else if (typeB == MAT3T_ZROT) {
        for (i = 0; i < 9; i++)
            o[i] = i % 3 == 2 ? a[i] : dot_rc(a, b, i / 3, i % 3, 2);
        pOut->bType = MAT3T_FULL;
    } else {
        for (i = 0; i < 9; i++)
            o[i] = dot_rc(a, b, i / 3, i % 3, 3);
        pOut->bType = MAT3T_FULL;
    }
    return pOut;
}

/*
 * The three angles of a rotation matrix: the first from asin(m[5]); with it at +-90
 * degrees the other two fold into one (the second 0), else each comes from atan2 of a
 * pair. out[1] is stored before the reads for out[2].
 */
s16 *r3d_matrix_to_euler(Mat3x3 *matrix3x3, s16 *out_xyz) {
    const s16 *m = matrix3x3->pM;
    s16 angle = r3d_tbl_asin(m[5]);
    out_xyz[0] = angle;
    if (angle < 0)
        angle = (s16)-angle;
    if (angle == 0x4000) {
        out_xyz[1] = 0;
        out_xyz[2] = r3d_tbl_atan2(m[0], m[1]);
    } else {
        out_xyz[1] = r3d_tbl_atan2(m[8], (s16)-m[2]);
        out_xyz[2] = r3d_tbl_atan2(m[4], (s16)-m[3]);
    }
    return out_xyz;
}

/* ---- the camera -------------------------------------------------------------------- */

/*
 * The frame's camera from a view: its position and angles, the projection's shift, the
 * viewport as the clip rectangle and its centre, and the world-to-view matrix. A view
 * rotation other than none is composed onto the camera's angles, which then take the
 * composed matrix's angles. The matrix ends transposed.
 */
void r3d_camera_setup_view(ViewContext *widget) {
    R3dRendererState *st = &g_abR3dState;
    WorldEntity *camera = widget->camera;
    const Rect *vp = &widget->viewport;
    s16 *angles = &st->nCamViewYaw; /* with nCamViewPitch and nCamViewHeading after it */
    u16 zoom;

    st->camCurWorldPos[0] = camera->base.pos.xy.nWorld_x;
    st->camCurWorldPos[1] = camera->base.pos.xy.nWorld_y;
    st->camCurWorldPos[2] = camera->base.pos.nWorld_z;
    camera = widget->camera;
    angles[0] = camera->base.orientation.pitch;
    angles[1] = camera->base.orientation.roll;
    angles[2] = camera->base.orientation.yaw;

    /* a word store: the zoom's high byte lands in the byte after the shift. The shift's
     * count is the low byte, masked to 5 bits as a 386 does */
    zoom = (u16)widget->zoom;
    st->bScreenShift = (unsigned char)zoom;
    st->bScreenShiftHi = (unsigned char)(zoom >> 8);
    st->nActorFarClipThreshold = (s16)(u16)(1u << (zoom & 31));
    g_r3d.pFaceMaterialTable = widget->faceMaterials;
    g_pColorRemapTable = widget->colorRemap;

    g_graphics_context.clip.xmin = vp->x;
    g_graphics_context.clip.xmax = (s16)(vp->x + vp->width - 1);
    g_graphics_context.clip.ymin = vp->y;
    g_graphics_context.clip.ymax = (s16)(vp->y + vp->height - 1);
    st->nViewportCenterX = (s16)(((u16)vp->width >> 1) + vp->x);
    st->nViewportCenterY = (s16)(((u16)vp->height >> 1) + vp->y);
    st->bSkipFarEdgeCull = 0;
    st->bCamSetupResetFlag = 0;

    g_r3d.matIn[0] = widget->viewRotation.pitch;
    g_r3d.matIn[1] = widget->viewRotation.roll;
    g_r3d.matIn[2] = widget->viewRotation.yaw;
    r3d_build_rotation_matrix(g_r3d.matIn, &g_r3d.mat3x3ScratchRot);
    if (g_r3d.mat3x3ScratchRot.bType == MAT3T_IDENTITY) {
        r3d_build_rotation_matrix(angles, &st->mat3x3ViewRot);
    } else {
        r3d_build_rotation_matrix(angles, &st->mat3x3ActorRot);
        r3d_matrix3x3_multiply(&g_r3d.mat3x3ScratchRot, &st->mat3x3ActorRot, &st->mat3x3ViewRot);
        r3d_matrix_to_euler(&st->mat3x3ViewRot, angles);
    }
    r3d_matrix3x3_transpose(&st->mat3x3ViewRot);
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
