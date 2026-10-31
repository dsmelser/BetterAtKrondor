#ifndef BAK_ROTBLIT_H
#define BAK_ROTBLIT_H

#include "structs.h"

extern void far *g_pRotBlitDispatchFp;
extern s16 g_anRotBlitVertX[4];
extern s16 g_anRotBlitVertY[4];
extern u16 g_wRotBlitIdxTop;
extern u16 g_wRotBlitIdxBottom;
extern u16 g_wRotBlitIdxNextCW;
extern u16 g_wRotBlitIdxOppCW;
extern void *g_pRotBlitStep1Fp;
extern void *g_apRotBlitStep1FpTable[4];
extern void *g_pRotBlitStep2Fp;
extern void *g_apRotBlitStep2FpTable[4];
extern void *g_pRotBlitStep3Fp;
extern void *g_apRotBlitStep3FpTable[4];
extern void *g_pRotBlitStep4Fp;
extern void *g_apRotBlitStep4FpTable[4];
extern s16 g_aStretchYSrcTable[400];
extern s16 g_aStretchXSrcTable[640];

extern void near stretch_walk_x_clipped(s16 *out_xs, s16 x_start, s16 count, s16 x_step, s16 span);
extern void vga_reset_gc_default_thunk(void);
extern void jmp_via_2f48(unsigned char *sprite, s16 x, s16 y, s16 angle, s16 dx, s16 dy, s16 flag1,
                         s16 flag2);
extern void read_4words_from_2tables(u16 *a, u16 *b, u16 *c, u16 *d);

#endif
