#ifndef BAK_SCROLL_H
#define BAK_SCROLL_H

#include "structs.h"

extern short g_nVgaScrollPageCount;
extern short g_nVgaScrollPanStep;
extern unsigned char far *g_pVgaScrollScratchBuf;

extern void scrolltrans_pageflip_pan(s16 x_off, s16 line, s16 screen_width, s16 set_split);
extern void scrolltrans_planar_blit_pageflip(s16 direction, s16 x_off, s16 y, s16 screen_width);
extern void scrolltrans_scroll(s16 direction, s16 duration, s16 distance);
extern void scrolltrans_scroll_fast(s16 direction, s16 speed, s16 distance);

#endif
