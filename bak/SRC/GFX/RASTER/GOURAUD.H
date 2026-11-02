#ifndef BAK_GOURAUD_H
#define BAK_GOURAUD_H

#include "structs.h"

/* The textured quad rasterizer for quads with slanted sides (port/asm/gouraud.c): the
   corners (xs[i], ys[i]) in order round the quad, the texture pinned to them. */
extern void gfx_gouraud_blit_quad(ImageRecord *texture, s16 *xs, s16 *ys);
/* The texture column and row at two neighbouring corners. */
extern void near gfx_gouraud_span_dispatch(s16 v0, s16 v1, ImageRecord *texture, s16 *column0,
                                           s16 *column1, s16 *row0, s16 *row1);
/* 1 or -1: the way round the quad from its leftmost corner (x0, y0), by the slopes of the
   edges to the corner before (A) and the next (B). */
extern s16 near compare_edge_slopes(s16 yA, s16 xA, s16 y0, s16 x0, s16 yB, s16 xB);
/* A 16.16 stepper over count + 1 steps, from step[0]'s high word to step[1]'s. */
extern void edge_step_init(s32 *step, s16 count);

#endif
