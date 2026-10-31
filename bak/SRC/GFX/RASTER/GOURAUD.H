#ifndef BAK_GOURAUD_H
#define BAK_GOURAUD_H

#include "structs.h"

extern void gfx_gouraud_blit_quad(u16 *quad, s16 *vy_array, s16 *vx_array);
extern void near gfx_gouraud_span_dispatch(s16 v0, s16 v1, u16 *quad, s16 *out_a, s16 *out_b,
                                           s16 *out_c, s16 *out_d);
extern s16 near compare_edge_slopes(s16 x1, s16 x2, s16 y_base, s16 y1, s16 x3, s16 x4);
extern void edge_step_init(s32 *step, s16 count);

#endif
