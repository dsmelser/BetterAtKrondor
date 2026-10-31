#ifndef R3D_H
#define R3D_H

#include "structs.h"

extern R3dRendererState far g_abR3dState;

extern Mat3x3 far g_mat3x3ViewRot;
extern s16 far g_nViewportCenterX;
extern s16 far g_nViewportCenterY;

typedef struct {
    s16 x;
    s16 y;
} ViewportCenter;
extern ViewportCenter far g_viewportCenter;

extern s16 far g_camViewYaw;
extern s16 far g_camViewPitch;

typedef struct {
    s16 yaw;
    s16 pitch;
} CamViewAngles;
extern CamViewAngles far g_camViewAngles;

extern s16 far g_nCurModelIdx;
extern u16 far g_wActorProjScale;
extern s32 far g_lActorRelX;
extern s32 far g_lActorRelY;
extern s16 far g_nActorFarClipThreshold;
extern unsigned char far g_nScreenShift;

extern u16 far g_wMeshVertexPoolOff;
extern u16 far g_nMeshPartVertexCount;
extern unsigned char far g_bCurMeshLod;
extern u16 far g_wMeshScaleResidual;
extern Mat3x3 far g_mat3x3ActorRot;
extern s16 far g_nMeshOriginX;
extern s16 far g_nMeshOriginY;
extern s16 far g_nMeshOriginZ;

extern void far *far g_pCurActorPagedRec;
extern unsigned char *far g_pCurMeshPartVisTable;

extern unsigned char *far g_pColorRemapTable;

extern s32 far g_dwActorCamX;
extern s32 far g_dwActorCamZ;
extern s32 far g_dwActorCamY;
extern s32 far g_dwMeshBoundHalfExtent;
extern unsigned char far g_bSkipFarEdgeCull;
extern unsigned char far g_bActorViewportClip;

extern u16 far g_wMeshScaleShift;
extern unsigned char far g_bMeshRenormCount;
extern s32 far g_dwActorBoundExtent;
extern s32 far g_lActorLodZ;
extern s32 far g_lActorClampedCamZ;
extern s16 far g_nMeshNormOriginX;
extern u32 far g_dwMeshVertexFarPtrCache;

extern s16 far g_nMeshCamOriginX;
extern s16 far g_nMeshCamOriginY;
extern s16 far g_nMeshCamOriginZ;

#endif
