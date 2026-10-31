#ifndef BAK_VIDINIT_H
#define BAK_VIDINIT_H

#include "structs.h"

extern unsigned char g_bSavedBiosVideoMode;
extern unsigned char g_bRequestedVideoMode;
extern void far *g_pfnVideoDriverInit;

extern void cga_rect_paste_from_buffer(unsigned char far *pDest, s16 x, s16 row, s16 width, s16 height);
extern s16 video_init(s16 mode, s16 b, char *driver_name);

#endif
