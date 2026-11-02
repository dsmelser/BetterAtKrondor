#ifndef BAK_ROTBLIT_H
#define BAK_ROTBLIT_H

#include "structs.h"

/* The corners of the last sprite drawn rotated, in the original's order (1.00's DGROUP
 * 2F4Ch-2F63h): the four x, the four y, then the top, bottom, next clockwise and opposite
 * clockwise corners as byte offsets into those arrays (0, 2, 4, 6). */
extern short g_anRotBlitState[12];
#define g_anRotBlitVertX (g_anRotBlitState)
#define g_anRotBlitVertY (g_anRotBlitState + 4)
#define g_wRotBlitIdxTop (*(unsigned short *)&g_anRotBlitState[8])
#define g_wRotBlitIdxBottom (*(unsigned short *)&g_anRotBlitState[9])
#define g_wRotBlitIdxNextCW (*(unsigned short *)&g_anRotBlitState[10])
#define g_wRotBlitIdxOppCW (*(unsigned short *)&g_anRotBlitState[11])

/* The source tables of STRBLIT.C's stretches and ROTBLIT's spans, in a row as in the original's
 * BSS: the X table's 640 words, then the Y table's 400. */
extern short g_aStretchSrcTables[640 + 400];
#define g_aStretchXSrcTable (g_aStretchSrcTables)
#define g_aStretchYSrcTable (g_aStretchSrcTables + 640)

/* A sprite drawn rotated by angle (a 16-bit binary angle) and scaled to scale_x by scale_y
 * pixels, from (x, y), or about its centre there when half_w and half_h are given. */
extern void jmp_via_2f48(unsigned char *sprite, s16 x, s16 y, s16 angle, s16 scale_x,
                         s16 scale_y, s16 half_w, s16 half_h);
/* The last such sprite's corners: the opposite clockwise one's x, the top's y, the next
 * clockwise one's x, the bottom's y. */
extern void read_4words_from_2tables(unsigned short *x_opp, unsigned short *y_top,
                                     unsigned short *x_next, unsigned short *y_bottom);

#endif
