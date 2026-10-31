#ifndef BAK_SKYREND_H
#define BAK_SKYREND_H

#include "structs.h"

extern s16 g_nHorizonScratchW;
extern unsigned char g_bCurSkyColorR;
extern unsigned char g_bCurSkyColorG;
extern unsigned char g_bCurSkyColorB;
extern unsigned char g_bSkyBandSelectB;
extern unsigned char g_bSkyBandSelectA;
extern u16 g_wHorizonCamYaw;
extern u16 g_wHorizonCamPitch;
extern s16 g_nClipLineX0;
extern s16 g_nClipLineY0;
extern s16 g_nClipLineX1;
extern s16 g_nClipLineY1;
extern s16 g_nHorizonRowY;

extern void skyrender_sky_and_ground(u16 sky_r, u16 sky_g, u16 sky_b, u16 cam_yaw,
                                     u16 cam_pitch);

#endif
