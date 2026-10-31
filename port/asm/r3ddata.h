/*
 * R3DDATA.ASM natively, with the few words R3D.ASM keeps between its routines.
 *
 * The renderer keeps its state in its own code segment. The block's first part is also
 * the game's g_abR3dState (R3dRendererState), and the names r3d.h gives its words are
 * that struct's fields natively. The rest belongs to R3D.ASM's routines: g_r3d, in the
 * original's order (the comments give the original's offsets). Where the original holds
 * a near pointer the field is a pointer, and the cosine table is R3DTRIG's (r3dtrig.c).
 */
#ifndef BAK_ASM_R3DDATA_H
#define BAK_ASM_R3DDATA_H

#include "bak_game_begin.h"
#include "structs.h"
#include "bak_game_end.h"

#define R3D_POLY_MAX 20
#define R3D_VERTEX_MAX 255

typedef struct R3dWork {
    /* 3831h: a polygon's corners, 32 bits split in low and high words (the high words
     * from 4544h on): the input, and the clippers' scratch they swap with pass to pass */
    s16 aPolyInX[R3D_POLY_MAX];
    s16 aPolyInY[R3D_POLY_MAX];
    s16 aPolyClipX[R3D_POLY_MAX];
    s16 aPolyClipY[R3D_POLY_MAX];
    s16 nMeshPolyVertexCount;              /* 38D1h */
    u16 polygonVertices[R3D_POLY_MAX];     /* 38D3h */
    unsigned char abPolygonClipState[510]; /* 38FBh */
    /* 3AF9h: per mesh vertex, camera space and screen, low words */
    s16 aVertexCamX[R3D_VERTEX_MAX];
    s16 aVertexCamY[R3D_VERTEX_MAX];
    s16 aVertexCamZ[R3D_VERTEX_MAX];
    s16 aVertexScrX[R3D_VERTEX_MAX];
    s16 aVertexScrY[R3D_VERTEX_MAX];
    Mat3x3 mat3x3ScratchRot;           /* 44EFh */
    s16 matIn[3];                      /* 4502h: g_matXIn, g_matYIn, g_matZIn */
    s16 matIn2[3];                     /* 4508h */
    s16 matOut[3];                     /* 450Eh */
    unsigned char matScratch[24];      /* 4514h */
    s32 matOut32[3];                   /* 452Ch: X, Z, Y */
    unsigned char *pFaceMaterialTable; /* 4538h; at 453Ah g_pColorRemapTable */
    unsigned char state453C[8];        /* 453Ch */
    u16 aPolyInXHi[R3D_POLY_MAX];      /* 4544h */
    u16 aPolyInYHi[R3D_POLY_MAX];
    u16 aPolyClipXHi[R3D_POLY_MAX];
    u16 aPolyClipYHi[R3D_POLY_MAX];
    s16 aVertexCamXHi[R3D_VERTEX_MAX]; /* 45E4h */
    s16 aVertexCamYHi[R3D_VERTEX_MAX];
    s16 aVertexCamZHi[R3D_VERTEX_MAX];
    s16 aVertexScrXHi[R3D_VERTEX_MAX];
    s16 aVertexScrYHi[R3D_VERTEX_MAX];
    unsigned char abLightingLut[256]; /* 4FDAh */

    /* R3D.ASM's own words, between its routines */
    s16 vec3LightDirWorld[3]; /* 063Eh: set once by r3d_light_set_direction */
    u16 nMeshPartPolygonsRemaining;
    unsigned char bNearClipNeeded;
    u16 nMeshPolyRemaining;
    unsigned char bMeshPolyHasZClipVertex;
    u16 nPolyListRemaining;
} R3dWork;

extern R3dWork g_r3d;

#endif
