#ifndef BAK_VIDDRV_H
#define BAK_VIDDRV_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern void huge *g_pVideoDriverTemplate;
extern char g_szDrvSuffix_BAD[3];
extern NearPtr g_apVideoDriverChunkNames[13];
extern char g_szVideoChunkName[10];
extern char g_szDrvSuffix_CGA[5];
extern char g_szDrvSuffix_EGA[5];
extern char g_szDrvSuffix_TAN[5];
extern char g_szDrvSuffix_HER[5];
extern char g_szDrvSuffix_MCG[5];
extern char g_szDrvSuffix_EVA[5];
extern char g_szDrvSuffix_VGA[5];
extern char g_szDrvSuffix_EVG[5];
extern char g_szDrvSuffix_HVG[5];
extern char g_szDrvSuffix_HEG[5];
extern char g_szDrvSuffix_NEW[5];

extern u32 far *video_driver_load(s16 mode, FileRef *file);

#endif
