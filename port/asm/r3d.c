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
#include <string.h>

#include "r3ddata.h"

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

/* NEG32ADC: the 32-bit negation of hi:lo, in place. */
static void neg32(u16 *lo, u16 *hi) {
    u32 v = 0u - ((u32)*hi << 16 | *lo);
    *lo = (u16)v;
    *hi = (u16)(v >> 16);
}

/*
 * One scaled value of r3d_ldiv_helper: |v| times the fraction q1:q2 (0.32 fixed point),
 * keeping what the original's partial products keep, then v's sign. A zero high word of
 * |v| or of the fraction drops the products below the result's integer part.
 */
static s32 ldiv_scale(s32 v, u16 q1, u16 q2, u16 sign) {
    u16 lo = (u16)v, hi = (u16)((u32)v >> 16);
    u16 vhi = hi;
    u32 r = 0;
    if (hi != 0 || lo != 0) {
        if ((s16)hi < 0)
            neg32(&lo, &hi);
        if (q1 != 0 && hi != 0) {
            u32 p1 = (u32)lo * q1, p2 = (u32)hi * q2;
            u32 carry = ((p1 & 0xffff) + (p2 & 0xffff)) >> 16;
            r = (u32)hi * q1 + ((p1 >> 16) + (p2 >> 16) + carry);
        } else if (hi != 0) {
            r = ((u32)hi * q2) >> 16;
        } else {
            r = ((u32)q1 * lo) >> 16;
        }
    }
    if ((s16)(vhi ^ sign) < 0)
        r = 0u - r;
    return (s32)r;
}

/*
 * *a and *b times num / den. The magnitudes are shifted left together until den's top bit
 * is set (from a den under 64K: its low word is the divisor and num's halves swap, right
 * only for a num under 64K; a byte shift first keeps the old low bytes instead of zeros),
 * and num over den's high word gives the fraction q1:q2. A num at or above den (by the
 * high words) leaves both values as they are. The ratio's sign is lost: the sign word is
 * the XOR of the magnitudes' high words, set only when one of them is 80000000h.
 */
void r3d_ldiv_helper(s32 *pScaledA, s32 *pScaledB, s32 den, s32 num) {
    u16 ax = (u16)den, dx = (u16)((u32)den >> 16);
    u16 bx = (u16)num, cx = (u16)((u32)num >> 16);
    u16 sign, t;
    if ((s16)dx < 0)
        neg32(&ax, &dx);
    if ((s16)cx < 0)
        neg32(&bx, &cx);
    sign = (u16)(dx ^ cx);
    if (ax == bx && cx == dx)
        return; /* the sign word is 0 then: the negating path after it is dead */
    if (dx == 0) {
        t = ax, ax = dx, dx = t; /* XCHG AX,DX; XCHG BX,CX */
        t = bx, bx = cx, cx = t;
    } else {
        if ((dx >> 8) == 0) {
            cx = (u16)(cx << 8 | bx >> 8);
            bx = (u16)(bx << 8 | (bx & 0xff));
            dx = (u16)(dx << 8 | ax >> 8);
            ax = (u16)(ax << 8 | (ax & 0xff));
        }
        while ((s16)dx >= 0) {
            cx = (u16)(cx << 1 | bx >> 15);
            bx = (u16)(bx << 1);
            dx = (u16)(dx << 1 | ax >> 15);
            ax = (u16)(ax << 1);
        }
    }
    /* MOV AX,BX; XCHG DX,CX: num's words over the divisor */
    if (dx <= cx)
        return;
    u32 n = (u32)cx << 16 | bx;
    u16 q1 = (u16)(n / dx);
    u16 q2 = (u16)(((n % dx) << 16) / dx);
    *pScaledA = ldiv_scale(*pScaledA, q1, q2, sign);
    *pScaledB = ldiv_scale(*pScaledB, q1, q2, sign);
}

/* ---- projection -------------------------------------------------------------------- */

/* The view-plane distance as the original pushes it: a word, zero-extended. */
static s32 view_dist(void) {
    return (s32)(u16)g_abR3dState.nActorFarClipThreshold;
}

/* (x, y) at depth nDenom onto the screen: each times the view distance over the depth. */
PerspDiv2Result *r3d_vec2_perspective_divide(PerspDiv2Args *args, PerspDiv2Result *out) {
    s32 x = args->nNumX, y = args->nNumY;
    r3d_ldiv_helper(&x, &y, args->nDenom, view_dist());
    out->nProjX = x;
    out->nProjY = y;
    return out;
}

/* nNumX times the view distance over the depth (the actor's projected size). */
s32 r3d_actor_proj_scale_compute(PerspDiv2Args *args) {
    s32 x = args->nNumX, unused = 0;
    r3d_ldiv_helper(&x, &unused, args->nDenom, view_dist());
    return x;
}

/* A projected point to the screen: x from the viewport's centre, y up from it. */
s32 *r3d_vec2_long_translate_yflip(s32 *pt) {
    pt[0] = (s32)((u32)pt[0] + (u32)(s32)g_abR3dState.nViewportCenterX);
    pt[1] = (s32)((u32)(s32)g_abR3dState.nViewportCenterY - (u32)pt[1]);
    return pt;
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
    if (bak_scene_capturing()) { /* a pass of the renderer begins (bak_port.h) */
        BakSceneCamera c;
        for (int i = 0; i < 3; i++) {
            c.pos[i] = st->camCurWorldPos[i];
            c.angles[i] = angles[i];
        }
        c.clip[0] = g_graphics_context.clip.xmin;
        c.clip[1] = g_graphics_context.clip.ymin;
        c.clip[2] = g_graphics_context.clip.xmax;
        c.clip[3] = g_graphics_context.clip.ymax;
        c.centerX = st->nViewportCenterX;
        c.centerY = st->nViewportCenterY;
        c.screenShift = st->bScreenShift;
        memcpy(c.view, st->mat3x3ViewRot.pM, sizeof c.view);
        bak_scene_begin(&c);
    }
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

/* ---- the actor ------------------------------------------------------------------- */

#define MASK48 0xffffffffffffull
#define MESH_FLAG_SIMPLE 0x20

/* SAR by 1, n times: from 32 on, the sign alone. */
static s32 sar32(s32 v, unsigned n) {
    return n >= 32 ? (v < 0 ? -1 : 0) : v >> n;
}

static s32 abs32(s32 v) {
    return v < 0 ? (s32)(0u - (u32)v) : v;
}

/* A 16-bit value times a 32-bit one, in 48 bits (the original multiplies the magnitudes and
 * negates all 48 bits: the exact product). */
static uint64_t mul48(s16 v, s32 c) {
    return (uint64_t)((int64_t)v * c) & MASK48;
}

/* The running sum in g_matXOut..g_matZOut: the high word first. */
static void put48(uint64_t v) {
    g_r3d.matOut[0] = (s16)(u16)(v >> 32);
    g_r3d.matOut[1] = (s16)(u16)(v >> 16);
    g_r3d.matOut[2] = (s16)(u16)v;
}

/*
 * r3d_dot3_coeff16_vec32 (AX = x, CX = y, DX = z, BX = c): x * c[0] + y * c[1] + z * c[2],
 * 48 bits (DX:AX:CX). x, y and z stay in g_matXIn2..; the running sum in g_matXOut.. holds
 * the first product, then, unless z is 0 (whose product is skipped), the first two's sum.
 */
static uint64_t dot3_coeff16_vec32(s16 x, s16 y, s16 z, const s32 *c) {
    uint64_t sum;
    g_r3d.matIn2[0] = x;
    g_r3d.matIn2[1] = y;
    g_r3d.matIn2[2] = z;
    sum = mul48(x, c[0]);
    put48(sum);
    sum = (sum + mul48(y, c[1])) & MASK48;
    if (z == 0)
        return sum;
    put48(sum);
    return (sum + mul48(z, c[2])) & MASK48;
}

s32 r3d_dot3_coeff16_vec32(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_dot3_coeff16_vec32 (register arguments: R3D's own routines "
                      "call its C form)");
}

/* Q14NORM48C: bits 14-45 of a 48-bit sum. */
static s32 q14_48(uint64_t v) {
    return (s32)(u32)(v >> 14);
}

/*
 * r3d_mat3_xform_vec3_ll (AX = in, BX = m, CX = out): three longs through m's columns. The
 * identity copies the six words; type 1 turns X and Y and keeps Z; any other type takes all
 * three rows (unlike the short transform, whose test is signed). Each output is stored
 * before the next one's inputs are read.
 */
static void mat3_xform_vec3_ll(const s32 *in, const Mat3x3 *mat, s32 *out) {
    const s16 *m = mat->pM;
    unsigned char type = mat->bType;
    if (type == MAT3T_IDENTITY) {
        const u16 *s = (const u16 *)in;
        u16 *d = (u16 *)out;
        for (int i = 0; i < 6; i++) /* REP MOVSW */
            d[i] = s[i];
        return;
    }
    out[0] = q14_48(dot3_coeff16_vec32(m[0], m[3], m[6], in));
    out[1] = q14_48(dot3_coeff16_vec32(m[1], m[4], m[7], in));
    if (type == MAT3T_ZROT)
        out[2] = in[2];
    else
        out[2] = q14_48(dot3_coeff16_vec32(m[2], m[5], m[8], in));
}

void r3d_mat3_xform_vec3_ll(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_mat3_xform_vec3_ll (register arguments: R3D's own routines "
                      "call its C form)");
}

/*
 * An actor into the camera's space: its angles go to g_matXIn for the matrix, its position
 * less the camera's through the view matrix, and its depth, no nearer than the view plane,
 * to g_lActorClampedCamZ. The original takes the entry as a far pointer; natively the
 * pointer is the whole address.
 */
void r3d_setup_actor_in_camera_space(WorldObject *entry) {
    R3dRendererState *st = &g_abR3dState;
    s32 z, plane;
    g_r3d.matIn[0] = entry->orientation.pitch;
    g_r3d.matIn[1] = entry->orientation.roll;
    g_r3d.matIn[2] = entry->orientation.yaw;
    st->nActorRelX = entry->pos.xy.nWorld_x;
    st->nActorRelY = entry->pos.xy.nWorld_y;
    st->nActorRelZ = entry->pos.nWorld_z;
    st->nActorRelX = (s32)((u32)st->nActorRelX - (u32)st->camCurWorldPos[0]);
    st->nActorRelY = (s32)((u32)st->nActorRelY - (u32)st->camCurWorldPos[1]);
    st->nActorRelZ = (s32)((u32)st->nActorRelZ - (u32)st->camCurWorldPos[2]);
    mat3_xform_vec3_ll(&st->nActorRelX, &st->mat3x3ViewRot, &st->nActorCamX);
    z = st->nActorCamZ;
    plane = view_dist();
    st->nActorClampedCamZ = z < plane ? plane : z;
}

/*
 * The actor's matrix (its own angles, then the view's), and the scale its mesh is transformed
 * at: the shift that brings the camera-space extent plus the mesh's half extent under 3FACh,
 * counted from the mesh's own shift, applied to the actor's camera-space and relative
 * positions. The camera origin and the light go into the actor's frame through its
 * transposed matrix.
 */
void r3d_compose_actor_camera_matrix(void) {
    R3dRendererState *st = &g_abR3dState;
    s32 *cam = &st->nActorCamX; /* X, Z, Y */
    s32 extent, a;
    u32 u;
    u16 count;

    r3d_build_rotation_matrix(g_r3d.matIn, &st->mat3x3TempOut);
    r3d_matrix3x3_multiply(&st->mat3x3TempOut, &st->mat3x3ViewRot, &st->mat3x3ActorRot);
    r3d_matrix3x3_transpose(&st->mat3x3TempOut);

    extent = abs32(cam[0]);
    a = abs32(cam[1]);
    if (extent < a)
        extent = a;
    a = abs32(cam[2]);
    if (extent < a)
        extent = a;
    st->nActorBoundExtent = extent;

    a = sar32((s32)((u32)st->nMeshBoundHalfExtent + (u32)extent), st->wMeshScaleShift);
    count = st->wMeshScaleShift;
    for (u = (u32)a; u >= 0x3fac; u >>= 1) /* SHR: a negative sum takes 32 shifts */
        count++;
    /* a word store: the count's high byte goes to the byte after it */
    st->bMeshRenormCount = (unsigned char)count;
    st->bMeshRenormCountHi = (unsigned char)(count >> 8);
    st->wMeshScaleResidual = (u16)(st->wMeshScaleShift - count);

    st->nMeshNormOriginX = (s16)sar32(st->nActorClampedCamZ, count);
    st->nMeshOriginX = (s16)sar32(cam[0], count);
    st->nMeshOriginY = (s16)sar32(cam[1], count);
    st->nMeshOriginZ = (s16)sar32(cam[2], count);
    g_r3d.matIn[0] = (s16)sar32(st->nActorRelX, count);
    g_r3d.matIn[1] = (s16)sar32(st->nActorRelY, count);
    g_r3d.matIn[2] = (s16)sar32(st->nActorRelZ, count);
    transform_vec3(g_r3d.matIn, &st->mat3x3TempOut, &st->nMeshCamOriginX);
    transform_vec3(g_r3d.vec3LightDirWorld, &st->mat3x3TempOut, st->vec3LightDirObj);
}

static s16 rd16(const unsigned char *p) {
    return (s16)(u16)(p[0] | p[1] << 8);
}

/* The nearer of a box's two faces along the view axis: each face's coordinate (record words
 * at a and b) times the matrix element, Q14; none for an element of 0. */
static s16 nearer_face(const unsigned char *rec, int a, int b, s16 r) {
    s16 fa, fb;
    if (r == 0)
        return 0;
    fa = fmul(rd16(rec + a), r);
    fb = fmul(rd16(rec + b), r);
    return fa <= fb ? fa : fb;
}

/*
 * The actor's depth for its level of detail: its camera depth less the mesh's half extent
 * for a simple mesh (flag 20h), else plus its bounding box's nearest corner along the view
 * axis (the box at 0Eh and 14h of the shape record), scaled by the mesh's shift.
 */
void r3d_actor_compute_lod_z(void) {
    R3dRendererState *st = &g_abR3dState;
    const unsigned char *rec = st->pCurActorPagedRec;
    const s16 *rot = st->mat3x3ActorRot.pM;
    u32 z;

    if (rec[0] & MESH_FLAG_SIMPLE) {
        st->nActorLodZ = (s32)((u32)st->nActorCamZ - (u32)st->nMeshBoundHalfExtent);
        return;
    }
    st->nActorLodZ = nearer_face(rec, 0x0e, 0x14, rot[1]);
    st->nActorLodZ = (s32)((u32)st->nActorLodZ + (u32)(s32)nearer_face(rec, 0x10, 0x16, rot[4]));
    st->nActorLodZ = (s32)((u32)st->nActorLodZ + (u32)(s32)nearer_face(rec, 0x12, 0x18, rot[7]));
    z = (u32)st->nActorLodZ;
    z = st->wMeshScaleShift >= 32 ? 0 : z << st->wMeshScaleShift;
    st->nActorLodZ = (s32)(z + (u32)st->nActorCamZ);
}

/* ---- the faces -------------------------------------------------------------------- */

/*
 * A mesh's records count their offsets from the shape record's paragraph, the segment the
 * original holds in ES while it walks them; a face is 8 bytes (flags, day outline and fill,
 * night outline and fill, the normal's vertex, the offset of its 0FFh-ended vertex list).
 */
enum { F_FLAGS, F_DAY_OUTLINE, F_DAY_FILL, F_NIGHT_OUTLINE, F_NIGHT_FILL, F_NRM_VERT, F_VERT_LIST };
#define FACE_SIZE 8
#define FACE_FILLED 0x80
#define FACE_TEXTURED 0x10

/* A row of a vertex or normal table: three shorts at index * 6. */
static const unsigned char *vrow(const unsigned char *pool, unsigned idx) {
    return pool + idx * 6u;
}

/*
 * r3d_backface_dot_indexed (AL = the normal's vertex, AH = the face's first vertex, ES:BP =
 * the vertex pool): the face normal's row into g_matXIn2 as it is, the first vertex's into
 * g_matXOut scaled down by the mesh's residual shift and moved by the camera origin, and
 * their dot product. The shift is a SAR by the residual's negation, its low 5 bits.
 */
static s32 backface_dot(const unsigned char *pool, unsigned char nrm, unsigned char first) {
    const unsigned char *a = vrow(pool, nrm), *b = vrow(pool, first);
    const R3dRendererState *st = &g_abR3dState;
    unsigned sh = (unsigned)(u16)(0u - st->wMeshScaleResidual) & 31;
    g_r3d.matIn2[0] = rd16(a);
    g_r3d.matIn2[1] = rd16(a + 2);
    g_r3d.matIn2[2] = rd16(a + 4);
    g_r3d.matOut[0] = (s16)((rd16(b) >> sh) + st->nMeshCamOriginX);
    g_r3d.matOut[1] = (s16)((rd16(b + 2) >> sh) + st->nMeshCamOriginY);
    g_r3d.matOut[2] = (s16)((rd16(b + 4) >> sh) + st->nMeshCamOriginZ);
    return r3d_dot3_long(g_r3d.matIn2, g_r3d.matOut);
}

s32 r3d_backface_dot_indexed(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_backface_dot_indexed (register arguments: R3D's own "
                      "routines call its C form)");
}

/*
 * r3d_polygon_resolve_shade_color and its fall-through _lit (ES:DI = the face, ES:BP = the
 * pool): the day or night colours by the render-state flag, filled by the face's bit 7.
 * A filled face of a simple mesh (20h) in daylight with a material table takes its material
 * from the table by its fill colour; materials 1-8 shade it by its normal against the
 * light, from the lighting ramp. The colour remap table, if any, applies last. (The shipped
 * game sets no material table.)
 */
static void resolve_shade_color(const unsigned char *face, const unsigned char *pool) {
    const R3dRendererState *st = &g_abR3dState;
    unsigned char day = g_graphics_context.bGfxRenderStateFlag;
    unsigned char outline = day ? face[F_DAY_OUTLINE] : face[F_NIGHT_OUTLINE];
    unsigned char fill = day ? face[F_DAY_FILL] : face[F_NIGHT_FILL];
    unsigned char flags = face[F_FLAGS];
    unsigned material;

    g_graphics_context.bGfx_fill_enabled = (flags & FACE_FILLED) ? 1 : 0;
    if ((flags & FACE_FILLED) && (flags & MESH_FLAG_SIMPLE) && day &&
        g_r3d.pFaceMaterialTable != NULL) {
        material = g_r3d.pFaceMaterialTable[fill];
        if (material != 0 && material <= 8) {
            const unsigned char *n = vrow(pool, face[F_NRM_VERT]);
            s16 shade;
            unsigned at;
            g_r3d.matIn2[0] = rd16(n);
            g_r3d.matIn2[1] = rd16(n + 2);
            g_r3d.matIn2[2] = rd16(n + 4);
            if (st->bPolyNormalFlipped) {
                g_r3d.matIn2[0] = (s16)-g_r3d.matIn2[0];
                g_r3d.matIn2[1] = (s16)-g_r3d.matIn2[1];
                g_r3d.matIn2[2] = (s16)-g_r3d.matIn2[2];
            }
            shade = (s16)((u32)r3d_dot3_long(g_r3d.matIn2, (s16 *)st->vec3LightDirObj) >> 16);
            if (shade == 16)
                shade = 15;
            at = (u16)(((material - 1) << 5) + (u16)(shade + 16));
            if (at >= sizeof g_r3d.abLightingLut)
                BAK_UNIMPLEMENTED("r3d_polygon_resolve_shade_color_lit past the lighting ramp "
                                  "(the original reads on into its segment)");
            unsigned char lit = (unsigned char)(g_r3d.abLightingLut[at] + fill);
            if (outline == fill)
                outline = lit;
            fill = lit;
        }
    }
    if (g_pColorRemapTable != NULL) {
        outline = g_pColorRemapTable[outline];
        fill = g_pColorRemapTable[fill];
    }
    g_graphics_context.bGfx_outline_color = outline;
    g_graphics_context.bGfx_fill_color = fill;
}

void r3d_polygon_resolve_shade_color(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_resolve_shade_color (register arguments: R3D's own "
                      "routines call its C form)");
}

void r3d_polygon_resolve_shade_color_lit(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_resolve_shade_color_lit (the tail of "
                      "r3d_polygon_resolve_shade_color, never called)");
}

/* Where submitted polygons go: the game's dispatchers (the tests point them elsewhere). */
R3dDrawPolygonFn *g_r3dDrawPolygon = sky_polygon_draw_dispatch;
R3dDrawTexturedQuadFn *g_r3dDrawTexturedQuad = worldrender_textured_quad_lod;

/* The polygon's corners into the graphics context's arrays (REP MOVSW from 20-entry arrays
 * into 20-entry ones: more corners overrun both). */
static void copy_corners(u16 n) {
    if (n > R3D_POLY_MAX)
        BAK_UNIMPLEMENTED("R3D polygon submission with more than 20 corners (the original "
                          "copies past its arrays)");
    memcpy(g_graphics_context.pPolygonX, g_r3d.aPolyInX, n * sizeof(s16));
    memcpy(g_graphics_context.pPolygonY, g_r3d.aPolyInY, n * sizeof(s16));
}

/* r3d_polygon_submit_to_rasterizer: the polygon, if it has corners, to the flat filler. */
static void submit_to_rasterizer(void) {
    u16 n = (u16)g_r3d.nMeshPolyVertexCount;
    if (n == 0)
        return;
    g_graphics_context.nPolygonClipVertexCount = (s16)n;
    copy_corners(n);
    bak_scene_polygon(n, g_graphics_context.pPolygonX, g_graphics_context.pPolygonY, 0,
                      g_graphics_context.bGfx_fill_enabled, g_graphics_context.bGfx_fill_color,
                      g_graphics_context.bGfx_outline_color);
    g_r3dDrawPolygon((u16)g_graphics_context.nPolygonClipVertexCount,
                     g_graphics_context.pPolygonX, g_graphics_context.pPolygonY);
    bak_scene_drawn();
}

void r3d_polygon_submit_to_rasterizer(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_submit_to_rasterizer (register arguments: R3D's own "
                      "routines call its C form)");
}

/* r3d_polygon_submit_textured_quad (ES:DI = the face): a quadrilateral to the textured
 * filler, its texture the day or night colour pair as a word; any other polygon is
 * dropped. */
static void submit_textured_quad(const unsigned char *face) {
    u16 n = (u16)g_r3d.nMeshPolyVertexCount;
    s16 texture;
    if (n != 4)
        return;
    g_graphics_context.nPolygonClipVertexCount = (s16)n;
    texture = g_graphics_context.bGfxRenderStateFlag ? rd16(face + F_DAY_OUTLINE)
                                                     : rd16(face + F_NIGHT_OUTLINE);
    copy_corners(n);
    bak_scene_polygon(n, g_graphics_context.pPolygonX, g_graphics_context.pPolygonY, 1, 0, 0, 0);
    g_r3dDrawTexturedQuad(texture, g_graphics_context.pPolygonX, g_graphics_context.pPolygonY);
    bak_scene_drawn();
}

void r3d_polygon_submit_textured_quad(void) {
    BAK_UNIMPLEMENTED("R3D.ASM: r3d_polygon_submit_textured_quad (register arguments: R3D's own "
                      "routines call its C form)");
}

/* A 32-bit sum of products as 48 bits (sign-extended), shifted left 2 + count: the integer
 * part (bits 16-47) out, the fraction (bits 0-15) in *frac. */
static s32 scaled48(u32 sum, unsigned count, s16 *frac) {
    uint64_t v = 0;
    if (2 + count < 48)
        v = ((uint64_t)(int64_t)(s32)sum << (2 + count)) & MASK48;
    *frac = (s16)(u16)v;
    return (s32)(u32)(v >> 16);
}

/* A short widened and shifted left count times (32 bits). */
static s32 widened(s16 v, unsigned count) {
    return count >= 32 ? 0 : (s32)((u32)(s32)v << count);
}

/*
 * A mesh vertex into the actor's frame in 48 bits, scaled by the mesh's shift: the integer
 * parts to out (X, then the matrix's second row, then its third), the fractions to
 * g_matXOut... The identity widens the vector; type 1 turns X and Y and widens Z; any other
 * type takes all three rows. Each part is stored before the next one's inputs are read.
 */
void r3d_mat3_xform_vec3_long(s16 *vec, Mat3x3 *matrix, s32 *out) {
    const s16 *m = matrix->pM;
    unsigned char type = matrix->bType;
    unsigned count = g_abR3dState.wMeshScaleShift;
    if (type == MAT3T_IDENTITY) {
        out[0] = widened(vec[0], count);
        g_r3d.matOut[0] = 0;
        out[1] = widened(vec[1], count);
        g_r3d.matOut[1] = 0;
        out[2] = widened(vec[2], count);
        g_r3d.matOut[2] = 0;
    } else if (type == MAT3T_ZROT) {
        out[0] = scaled48(mul(vec[0], m[0]) + mul(vec[1], m[3]), count, &g_r3d.matOut[0]);
        out[1] = scaled48(mul(vec[0], m[1]) + mul(vec[1], m[4]), count, &g_r3d.matOut[1]);
        out[2] = widened(vec[2], count);
        g_r3d.matOut[2] = 0;
    } else {
        out[0] = scaled48(mul(vec[0], m[0]) + mul(vec[1], m[3]) + mul(vec[2], m[6]), count,
                          &g_r3d.matOut[0]);
        out[1] = scaled48(mul(vec[0], m[1]) + mul(vec[1], m[4]) + mul(vec[2], m[7]), count,
                          &g_r3d.matOut[1]);
        out[2] = scaled48(mul(vec[0], m[2]) + mul(vec[1], m[5]) + mul(vec[2], m[8]), count,
                          &g_r3d.matOut[2]);
    }
}

/* ---- the mesh producers ----------------------------------------------------------- */

/*
 * The three producers walk a mesh part's faces (part header: the face count, then the
 * faces' offset), cull or flip them by the back-face dot, colour them, transform and
 * project each vertex once (its state in g_abPolygonClipState at twice its index:
 * untouched, pending, projected, behind the view plane), clip, and submit. The vertex
 * caches are kept until the part's vertex pool changes (its segment and offset).
 * g_polygon_vertices holds each corner as its vertex's byte offset in the word arrays.
 */
enum { CLIP_UNTOUCHED, CLIP_PENDING, CLIP_PROJECTED, CLIP_BEHIND };

/* The face just walked, for the port's scene capture (bak_port.h): its corners in camera space
 * as the producer holds them (16 bits, or 32 with the high words), before any clip, with its
 * colours or texture and the projection's parameters. */
static void capture_face(const unsigned char *face, int wide, int nearClipped) {
    const R3dRendererState *st = &g_abR3dState;
    BakSceneFace f;
    u16 n = (u16)g_r3d.nMeshPolyVertexCount;
    if (!bak_scene_capturing() || n == 0 || n > BAK_SCENE_CORNERS)
        return;
    f.corners = (uint8_t)n;
    f.frontMask = 0;
    for (u16 i = 0; i < n; i++) {
        unsigned v = g_r3d.polygonVertices[i] / 2;
        if (g_r3d.abPolygonClipState[2 * v] == CLIP_PROJECTED)
            f.frontMask |= 1u << i;
        if (wide) {
            f.cam[i][0] = (s32)((u32)(u16)g_r3d.aVertexCamXHi[v] << 16 | (u16)g_r3d.aVertexCamX[v]);
            f.cam[i][1] = (s32)((u32)(u16)g_r3d.aVertexCamYHi[v] << 16 | (u16)g_r3d.aVertexCamY[v]);
            f.cam[i][2] = (s32)((u32)(u16)g_r3d.aVertexCamZHi[v] << 16 | (u16)g_r3d.aVertexCamZ[v]);
        } else {
            f.cam[i][0] = g_r3d.aVertexCamX[v];
            f.cam[i][1] = g_r3d.aVertexCamY[v];
            f.cam[i][2] = g_r3d.aVertexCamZ[v];
        }
    }
    f.textured = (face[F_FLAGS] & FACE_TEXTURED) != 0;
    f.nearClipped = (uint8_t)nearClipped;
    f.viewportClipped = st->bActorViewportClip != 0;
    f.filled = g_graphics_context.bGfx_fill_enabled;
    f.fill = g_graphics_context.bGfx_fill_color;
    f.outline = g_graphics_context.bGfx_outline_color;
    f.texture = g_graphics_context.bGfxRenderStateFlag ? rd16(face + F_DAY_OUTLINE)
                                                       : rd16(face + F_NIGHT_OUTLINE);
    f.clip[0] = g_graphics_context.clip.xmin;
    f.clip[1] = g_graphics_context.clip.ymin;
    f.clip[2] = g_graphics_context.clip.xmax;
    f.clip[3] = g_graphics_context.clip.ymax;
    f.centerX = st->nViewportCenterX;
    f.centerY = st->nViewportCenterY;
    f.screenShift = st->bScreenShift;
    f.lod = st->bCurMeshLod;
    f.flags = face[F_FLAGS];
    f.id = face;
    bak_scene_face(&f);
}

/* The segment the original walks the mesh in (ES): the shape record's paragraph, which
 * actorrender_mesh_parts built the part header's far pointer from. */
static u16 mesh_seg(void) {
    return FP_SEG_NORM(g_abR3dState.pCurActorPagedRec);
}

static const unsigned char *at_seg(u16 seg, u16 off) {
    return MK_FP(seg, off);
}

/* A new vertex pool clears the vertex states: the part's vertex count in words. */
static void mesh_cache(u16 seg) {
    R3dRendererState *st = &g_abR3dState;
    u32 key = (u32)seg << 16 | st->wMeshVertexPoolOff;
    if (st->dwMeshVertexFarPtrCache == key)
        return;
    st->dwMeshVertexFarPtrCache = key;
    if (st->wMeshPartVertexCount > R3D_VERTEX_MAX)
        BAK_UNIMPLEMENTED("R3D mesh part with more than 255 vertices (the original clears past "
                          "its vertex states)");
    memset(g_r3d.abPolygonClipState, 0, 2u * st->wMeshPartVertexCount);
}

/* DEC then JGE: on while the count was at least 1 (the true difference is not negative). */
static int next_face(u16 *remaining) {
    s16 old = (s16)*remaining;
    *remaining = (u16)(old - 1);
    return old >= 1;
}

/* The back-face test of a face: 1 to draw it (with g_bPolyNormalFlipped set as it is
 * drawn), 0 to skip it. Kind 0 is drawn either way, flipped from behind when a simple
 * mesh's (20h); kinds 1 and 3 are culled from behind. */
static int face_facing(u16 seg, const unsigned char *face, const unsigned char *pool) {
    unsigned char first = *at_seg(seg, rd16(face + F_VERT_LIST));
    unsigned kind = face[F_FLAGS] & 3;
    g_abR3dState.bPolyNormalFlipped = 0;
    if (kind == 0) {
        if ((face[F_FLAGS] & MESH_FLAG_SIMPLE) && backface_dot(pool, face[F_NRM_VERT], first) >= 0)
            g_abR3dState.bPolyNormalFlipped = 1;
        return 1;
    }
    return backface_dot(pool, face[F_NRM_VERT], first) < 0;
}

static void append_corner(u16 slot) {
    if ((u16)g_r3d.nMeshPolyVertexCount >= R3D_POLY_MAX)
        BAK_UNIMPLEMENTED("R3D face with more than 20 vertices (the original writes past its "
                          "corner list)");
    g_r3d.polygonVertices[g_r3d.nMeshPolyVertexCount++] = slot;
}

/*
 * DX:AX for the 16-bit projection: v widened, shifted left by the screen shift's word (its
 * high byte included). From a low byte of 8 up (a signed compare) a byte goes at once and
 * the low byte loses 8; the rest is a LOOP on the word, 65536 times from 0.
 */
static s32 proj_shift(s16 v) {
    u16 cx = (u16)(g_abR3dState.bScreenShift | g_abR3dState.bScreenShiftHi << 8);
    u32 dxax = (u32)(s32)v;
    u32 count;
    if ((signed char)(cx & 0xff) >= 8) {
        dxax <<= 8;
        cx = (u16)((cx & 0xff00) | (unsigned char)((cx & 0xff) - 8));
        if ((cx & 0xff) == 0)
            return (s32)dxax;
    }
    count = cx == 0 ? 0x10000u : cx;
    return count >= 32 ? 0 : (s32)(dxax << count);
}

/* The 16-bit producers' projection: x and z (the Y and Z arrays hold depth and height) over
 * the depth, IDIV as the divide-error handler resolves it. */
static void project16(unsigned v) {
    s16 depth = g_r3d.aVertexCamY[v];
    s16 rem;
    g_r3d.aVertexScrX[v] = bak_idiv16(proj_shift(g_r3d.aVertexCamX[v]), depth, &rem);
    g_r3d.aVertexScrY[v] = bak_idiv16(proj_shift(g_r3d.aVertexCamZ[v]), depth, &rem);
}

/* A vertex's row of the pool through the actor's matrix (16 bits) into g_matXOut... */
static void transform16(const unsigned char *pool, unsigned v) {
    const unsigned char *row = vrow(pool, v);
    g_r3d.matIn[0] = rd16(row);
    g_r3d.matIn[1] = rd16(row + 2);
    g_r3d.matIn[2] = rd16(row + 4);
    transform_vec3(g_r3d.matIn, &g_abR3dState.mat3x3ActorRot, g_r3d.matOut);
}

/* The projected corners into the polygon arrays (low words; the 32-bit producer adds the
 * high ones). */
static void copy_projected(int wide) {
    for (u16 i = 0; i < (u16)g_r3d.nMeshPolyVertexCount; i++) {
        unsigned v = g_r3d.polygonVertices[i] / 2;
        g_r3d.aPolyInX[i] = g_r3d.aVertexScrX[v];
        if (wide)
            g_r3d.aPolyInXHi[i] = (u16)g_r3d.aVertexScrXHi[v];
        g_r3d.aPolyInY[i] = g_r3d.aVertexScrY[v];
        if (wide)
            g_r3d.aPolyInYHi[i] = (u16)g_r3d.aVertexScrYHi[v];
    }
}

/* A textured face whose normal row is (0, 0, z != 0): a floor tile seen from above or below. */
static int floor_tile(const unsigned char *face, const unsigned char *pool) {
    const unsigned char *n = vrow(pool, face[F_NRM_VERT]);
    return rd16(n) == 0 && rd16(n + 2) == 0 && rd16(n + 4) != 0;
}

/*
 * After the near clip of a floor tile in the world: each corner behind the view plane gives
 * its camera x and z as they are (kept if its depth is not negative), the others their
 * screen position; no corner kept drops the polygon.
 */
void r3d_poly_emit_screen_or_world(void) {
    u16 n = (u16)g_r3d.nMeshPolyVertexCount, kept = 0;
    if (n == 0)
        return;
    for (u16 i = 0; i < n; i++) {
        u16 slot = g_r3d.polygonVertices[i];
        unsigned v = slot / 2;
        if (g_r3d.abPolygonClipState[slot] == CLIP_BEHIND) {
            g_r3d.aPolyInX[i] = g_r3d.aVertexCamX[v];
            g_r3d.aPolyInY[i] = g_r3d.aVertexCamZ[v];
            if (g_r3d.aVertexCamY[v] >= 0)
                kept++;
        } else {
            kept++;
            g_r3d.aPolyInX[i] = g_r3d.aVertexScrX[v];
            g_r3d.aPolyInY[i] = g_r3d.aVertexScrY[v];
        }
    }
    if (kept == 0)
        g_r3d.nMeshPolyVertexCount = 0;
}

/*
 * LOD 0: 16-bit camera space. A vertex goes behind the view plane when its depth is under
 * the plane's distance (a signed compare); a polygon with one is clipped there, except a
 * floor tile in the world, whose world corners go on as they are. A textured face goes to
 * the textured filler, but a floor tile in the world with textures off is filled flat in
 * colour 8 (9 seen from below).
 */
void r3d_mesh_part_polygons(unsigned char *part_header) {
    R3dRendererState *st = &g_abR3dState;
    u16 seg = mesh_seg();
    const unsigned char *pool = at_seg(seg, st->wMeshVertexPoolOff);
    u16 face_off;

    mesh_cache(seg);
    g_r3d.nMeshPartPolygonsRemaining = (u16)rd16(part_header);
    face_off = (u16)rd16(part_header + 2);
    for (; next_face(&g_r3d.nMeshPartPolygonsRemaining); face_off = (u16)(face_off + FACE_SIZE)) {
        const unsigned char *face = at_seg(seg, face_off);
        u16 list;
        if ((face[F_FLAGS] & 3) == 2 || !face_facing(seg, face, pool))
            continue;
        resolve_shade_color(face, pool);
        g_r3d.bNearClipNeeded = 0;
        g_r3d.nMeshPolyVertexCount = 0;
        for (list = (u16)rd16(face + F_VERT_LIST);; list++) {
            unsigned v = *at_seg(seg, list);
            unsigned char *state;
            if (v == 0xff)
                break;
            state = &g_r3d.abPolygonClipState[2 * v];
            if (*state == CLIP_UNTOUCHED) {
                *state = CLIP_PENDING;
                transform16(pool, v);
                g_r3d.aVertexCamX[v] = (s16)(g_r3d.matOut[0] + (u16)st->nActorCamX);
                g_r3d.aVertexCamY[v] = (s16)(g_r3d.matOut[1] + (u16)st->nActorCamZ);
                g_r3d.aVertexCamZ[v] = (s16)(g_r3d.matOut[2] + (u16)st->nActorCamY);
            } else if (*state == CLIP_PROJECTED) {
                append_corner((u16)(2 * v));
                continue;
            }
            if (g_r3d.aVertexCamY[v] < st->nActorFarClipThreshold) {
                *state = CLIP_BEHIND;
                g_r3d.bNearClipNeeded = 1;
            } else {
                *state = CLIP_PROJECTED;
                project16(v);
            }
            append_corner((u16)(2 * v));
        }
        capture_face(face, 0, g_r3d.bNearClipNeeded);
        if (g_r3d.bNearClipNeeded) {
            if (g_game_mode == 2 && (face[F_FLAGS] & FACE_TEXTURED) && floor_tile(face, pool))
                r3d_poly_emit_screen_or_world();
            else
                r3d_polygon_clip_near_z();
        } else {
            if (g_r3d.nMeshPolyVertexCount == 0)
                continue;
            copy_projected(0);
        }
        r3d_apply_viewport_offset();
        if (face[F_FLAGS] & FACE_TEXTURED) {
            if (g_game_mode != 2 || g_bTexturedPolyEnabled != 0 || !floor_tile(face, pool)) {
                submit_textured_quad(face);
                continue;
            }
            unsigned char color = rd16(vrow(pool, face[F_NRM_VERT]) + 4) < 0 ? 9 : 8;
            g_graphics_context.bGfx_outline_color = color;
            g_graphics_context.bGfx_fill_color = color;
            g_graphics_context.bGfx_fill_enabled = 1;
        }
        if (st->bActorViewportClip)
            r3d_polygon_scissor_2d();
        submit_to_rasterizer();
    }
}

/*
 * LODs 1 and 2: 32-bit camera space, from the 48-bit vertex transform. A vertex goes behind
 * the view plane when its depth is under the plane's distance; the screen position comes
 * from the long scaler. LOD 1 clips against the plane and the viewport with the 32-bit
 * clippers, LOD 2 with the others.
 */
void r3d_mesh_polygons(unsigned char *part_header) {
    R3dRendererState *st = &g_abR3dState;
    u16 seg = mesh_seg();
    const unsigned char *pool = at_seg(seg, st->wMeshVertexPoolOff);
    u16 face_off;

    mesh_cache(seg);
    g_r3d.nMeshPolyRemaining = (u16)rd16(part_header);
    face_off = (u16)rd16(part_header + 2);
    for (; next_face(&g_r3d.nMeshPolyRemaining); face_off = (u16)(face_off + FACE_SIZE)) {
        const unsigned char *face = at_seg(seg, face_off);
        u16 list;
        if ((face[F_FLAGS] & 3) == 2 || !face_facing(seg, face, pool))
            continue;
        resolve_shade_color(face, pool);
        g_r3d.bMeshPolyHasZClipVertex = 0;
        g_r3d.nMeshPolyVertexCount = 0;
        for (list = (u16)rd16(face + F_VERT_LIST);; list++) {
            unsigned v = *at_seg(seg, list);
            unsigned char *state;
            s32 x, y, z;
            if (v == 0xff)
                break;
            state = &g_r3d.abPolygonClipState[2 * v];
            if (*state == CLIP_UNTOUCHED) {
                const unsigned char *row = vrow(pool, v);
                *state = CLIP_PENDING;
                g_r3d.matIn[0] = rd16(row);
                g_r3d.matIn[1] = rd16(row + 2);
                g_r3d.matIn[2] = rd16(row + 4);
                r3d_mat3_xform_vec3_long(g_r3d.matIn, &st->mat3x3ActorRot, g_r3d.matOut32);
                x = (s32)((u32)g_r3d.matOut32[0] + (u32)st->nActorCamX);
                y = (s32)((u32)g_r3d.matOut32[1] + (u32)st->nActorCamZ);
                z = (s32)((u32)g_r3d.matOut32[2] + (u32)st->nActorCamY);
                g_r3d.aVertexCamX[v] = (s16)x;
                g_r3d.aVertexCamXHi[v] = (s16)(x >> 16);
                g_r3d.aVertexCamY[v] = (s16)y;
                g_r3d.aVertexCamYHi[v] = (s16)(y >> 16);
                g_r3d.aVertexCamZ[v] = (s16)z;
                g_r3d.aVertexCamZHi[v] = (s16)(z >> 16);
            } else if (*state == CLIP_PROJECTED) {
                append_corner((u16)(2 * v));
                continue;
            }
            x = (s32)((u32)(u16)g_r3d.aVertexCamXHi[v] << 16 | (u16)g_r3d.aVertexCamX[v]);
            y = (s32)((u32)(u16)g_r3d.aVertexCamYHi[v] << 16 | (u16)g_r3d.aVertexCamY[v]);
            z = (s32)((u32)(u16)g_r3d.aVertexCamZHi[v] << 16 | (u16)g_r3d.aVertexCamZ[v]);
            if (y < view_dist()) {
                *state = CLIP_BEHIND;
                g_r3d.bMeshPolyHasZClipVertex = 1;
            } else {
                *state = CLIP_PROJECTED;
                r3d_ldiv_helper(&x, &z, y, view_dist());
                g_r3d.aVertexScrX[v] = (s16)x;
                g_r3d.aVertexScrXHi[v] = (s16)(x >> 16);
                g_r3d.aVertexScrY[v] = (s16)z;
                g_r3d.aVertexScrYHi[v] = (s16)(z >> 16);
            }
            append_corner((u16)(2 * v));
        }
        capture_face(face, 1, g_r3d.bMeshPolyHasZClipVertex);
        if (g_r3d.bMeshPolyHasZClipVertex) {
            if (st->bCurMeshLod == 1)
                r3d_polygon_clip_against_zplane();
            else
                r3d_polygon_clip_near_z_lod2();
        } else {
            if (g_r3d.nMeshPolyVertexCount == 0)
                continue;
            copy_projected(1);
        }
        r3d_apply_viewport_offset_long();
        if (st->bActorViewportClip) {
            if (st->bCurMeshLod == 1)
                r3d_clip_polygon_to_viewport();
            else
                r3d_polygon_clip_rect_2d();
        }
        submit_to_rasterizer();
    }
}

/* The mesh's scale on a 16-bit coordinate: SHL by the shift's low byte when it is positive
 * (a signed byte), else SAR by its negation (the CPU takes 5 bits of either). */
static s16 lod3_scale(s16 v) {
    signed char cl = (signed char)(g_abR3dState.wMeshScaleShift & 0xff);
    if (cl > 0)
        return (s16)(u16)((u16)v << (cl & 31));
    return (s16)(v >> ((unsigned char)-cl & 31));
}

/*
 * LOD 3: 16-bit camera space at the mesh's own scale, every vertex projected (no near
 * clip, so nothing tests the depth before the division).
 */
void r3d_mesh_part_polygons_lod3(unsigned short *part_header) {
    R3dRendererState *st = &g_abR3dState;
    u16 seg = mesh_seg();
    const unsigned char *pool = at_seg(seg, st->wMeshVertexPoolOff);
    const unsigned char *header = (const unsigned char *)part_header;
    u16 face_off;

    mesh_cache(seg);
    g_r3d.nPolyListRemaining = (u16)rd16(header);
    face_off = (u16)rd16(header + 2);
    for (; next_face(&g_r3d.nPolyListRemaining); face_off = (u16)(face_off + FACE_SIZE)) {
        const unsigned char *face = at_seg(seg, face_off);
        u16 list;
        if ((face[F_FLAGS] & 3) == 2 || !face_facing(seg, face, pool))
            continue;
        resolve_shade_color(face, pool);
        g_r3d.nMeshPolyVertexCount = 0;
        for (list = (u16)rd16(face + F_VERT_LIST);; list++) {
            unsigned v = *at_seg(seg, list);
            unsigned char *state;
            if (v == 0xff)
                break;
            state = &g_r3d.abPolygonClipState[2 * v];
            if (*state == CLIP_UNTOUCHED) {
                *state = CLIP_PENDING;
                transform16(pool, v);
                g_r3d.aVertexCamX[v] = (s16)(lod3_scale(g_r3d.matOut[0]) + (u16)st->nActorCamX);
                g_r3d.aVertexCamY[v] = (s16)(lod3_scale(g_r3d.matOut[1]) + (u16)st->nActorCamZ);
                g_r3d.aVertexCamZ[v] = (s16)(lod3_scale(g_r3d.matOut[2]) + (u16)st->nActorCamY);
            } else if (*state == CLIP_PROJECTED) {
                append_corner((u16)(2 * v));
                continue;
            }
            *state = CLIP_PROJECTED;
            project16(v);
            append_corner((u16)(2 * v));
        }
        capture_face(face, 0, 0);
        if (g_r3d.nMeshPolyVertexCount == 0)
            continue;
        copy_projected(0);
        r3d_apply_viewport_offset();
        if (face[F_FLAGS] & FACE_TEXTURED) {
            submit_textured_quad(face);
        } else {
            if (st->bActorViewportClip)
                r3d_polygon_scissor_2d();
            submit_to_rasterizer();
        }
    }
}

/* The register-convention routines' C forms, for the differential tests. */
s32 r3d_test_backface_dot(const unsigned char *pool, unsigned char nrm, unsigned char first) {
    return backface_dot(pool, nrm, first);
}

void r3d_test_resolve_shade_color(const unsigned char *face, const unsigned char *pool) {
    resolve_shade_color(face, pool);
}

void r3d_test_submit_to_rasterizer(void) {
    submit_to_rasterizer();
}

void r3d_test_submit_textured_quad(const unsigned char *face) {
    submit_textured_quad(face);
}
