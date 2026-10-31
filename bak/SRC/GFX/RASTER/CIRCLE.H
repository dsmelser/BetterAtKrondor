#ifndef BAK_CIRCLE_H
#define BAK_CIRCLE_H

#include "structs.h"

extern short g_nCircleCenterX;
extern short g_nCircleClipCenterY;
extern short g_nCircleClipDyMax;
extern short g_nCircleClipDyMin;
extern short g_nCircleClipTopY;
extern unsigned short far *g_pSpanBufCursor;
extern CirclePlotterFn *g_pfnCirclePlotter;

extern void draw_circle(s16 radius, s16 cx, s16 cy);
extern u32 near draw_circle_bresenham(s16 radius, s16 cx, s16 cy);
extern void near record_circle_span(s16 dx, s16 dy);
extern void near circle_plot_mirror_pair(s16 dx, s16 dy);

#endif
