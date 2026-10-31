#ifndef R3D_H
#define R3D_H

#include "structs.h"

extern R3dRendererState far g_abR3dState;

#ifdef BAK_NATIVE
/*
 * In the original these names are labels on words of the renderer's state block, which
 * g_abR3dState covers too (R3DDATA.ASM). Natively they name its fields, so both reach the
 * same storage.
 */
typedef struct {
    short x;
    short y;
} ViewportCenter;

typedef struct {
    short yaw;
    short pitch;
} CamViewAngles;

#define g_mat3x3ViewRot (g_abR3dState.mat3x3ViewRot)
#define g_nViewportCenterX (g_abR3dState.nViewportCenterX)
#define g_nViewportCenterY (g_abR3dState.nViewportCenterY)
#define g_viewportCenter (*(ViewportCenter *)&g_abR3dState.nViewportCenterX)
#define g_camViewYaw (g_abR3dState.nCamViewYaw)
#define g_camViewPitch (g_abR3dState.nCamViewPitch)
#define g_camViewAngles (*(CamViewAngles *)&g_abR3dState.nCamViewYaw)
#define g_nCurModelIdx (g_abR3dState.nCurModelIdx)
#define g_wActorProjScale (g_abR3dState.wActorProjScale)
#define g_lActorRelX (g_abR3dState.nActorRelX)
#define g_lActorRelY (g_abR3dState.nActorRelY)
#define g_nActorFarClipThreshold (g_abR3dState.nActorFarClipThreshold)
#define g_nScreenShift (g_abR3dState.bScreenShift)
#define g_wMeshVertexPoolOff (g_abR3dState.wMeshVertexPoolOff)
#define g_nMeshPartVertexCount (g_abR3dState.wMeshPartVertexCount)
#define g_bCurMeshLod (g_abR3dState.bCurMeshLod)
#define g_wMeshScaleResidual (g_abR3dState.wMeshScaleResidual)
#define g_mat3x3ActorRot (g_abR3dState.mat3x3ActorRot)
#define g_nMeshOriginX (g_abR3dState.nMeshOriginX)
#define g_nMeshOriginY (g_abR3dState.nMeshOriginY)
#define g_nMeshOriginZ (g_abR3dState.nMeshOriginZ)
#define g_pCurActorPagedRec (g_abR3dState.pCurActorPagedRec)
#define g_pCurMeshPartVisTable (g_abR3dState.pCurMeshPartVisTable)
#define g_dwActorCamX (g_abR3dState.nActorCamX)
#define g_dwActorCamZ (g_abR3dState.nActorCamZ)
#define g_dwActorCamY (g_abR3dState.nActorCamY)
#define g_dwMeshBoundHalfExtent (g_abR3dState.nMeshBoundHalfExtent)
#define g_bSkipFarEdgeCull (g_abR3dState.bSkipFarEdgeCull)
#define g_bActorViewportClip (g_abR3dState.bActorViewportClip)
#define g_wMeshScaleShift (g_abR3dState.wMeshScaleShift)
#define g_bMeshRenormCount (g_abR3dState.bMeshRenormCount)
#define g_dwActorBoundExtent (g_abR3dState.nActorBoundExtent)
#define g_lActorLodZ (g_abR3dState.nActorLodZ)
#define g_lActorClampedCamZ (g_abR3dState.nActorClampedCamZ)
#define g_nMeshNormOriginX (g_abR3dState.nMeshNormOriginX)
#define g_dwMeshVertexFarPtrCache (g_abR3dState.dwMeshVertexFarPtrCache)
#define g_nMeshCamOriginX (g_abR3dState.nMeshCamOriginX)
#define g_nMeshCamOriginY (g_abR3dState.nMeshCamOriginY)
#define g_nMeshCamOriginZ (g_abR3dState.nMeshCamOriginZ)

/* Past the struct's end: storage of its own. */
extern unsigned char *far g_pColorRemapTable;
#else
extern Mat3x3 far g_mat3x3ViewRot;
extern short far g_nViewportCenterX;
extern short far g_nViewportCenterY;

typedef struct {
    short x;
    short y;
} ViewportCenter;
extern ViewportCenter far g_viewportCenter;

extern short far g_camViewYaw;
extern short far g_camViewPitch;

typedef struct {
    short yaw;
    short pitch;
} CamViewAngles;
extern CamViewAngles far g_camViewAngles;

extern s16 far g_nCurModelIdx;
extern unsigned short far g_wActorProjScale;
extern s32 far g_lActorRelX;
extern s32 far g_lActorRelY;
extern short far g_nActorFarClipThreshold;
extern unsigned char far g_nScreenShift;

extern unsigned short far g_wMeshVertexPoolOff;
extern unsigned short far g_nMeshPartVertexCount;
extern unsigned char far g_bCurMeshLod;
extern unsigned short far g_wMeshScaleResidual;
extern Mat3x3 far g_mat3x3ActorRot;
extern short far g_nMeshOriginX;
extern short far g_nMeshOriginY;
extern short far g_nMeshOriginZ;

extern void far *far g_pCurActorPagedRec;
extern unsigned char *far g_pCurMeshPartVisTable;

extern unsigned char *far g_pColorRemapTable;

extern s32 far g_dwActorCamX;
extern s32 far g_dwActorCamZ;
extern s32 far g_dwActorCamY;
extern s32 far g_dwMeshBoundHalfExtent;
extern unsigned char far g_bSkipFarEdgeCull;
extern unsigned char far g_bActorViewportClip;

extern unsigned short far g_wMeshScaleShift;
extern unsigned char far g_bMeshRenormCount;
extern s32 far g_dwActorBoundExtent;
extern s32 far g_lActorLodZ;
extern s32 far g_lActorClampedCamZ;
extern short far g_nMeshNormOriginX;
extern u32 far g_dwMeshVertexFarPtrCache;

extern short far g_nMeshCamOriginX;
extern short far g_nMeshCamOriginY;
extern short far g_nMeshCamOriginZ;
#endif

#endif
