#ifndef BAK_POLYRAST_H
#define BAK_POLYRAST_H

#include "structs.h"

extern void polyrast_edge_init_step(s32 *edge, u16 steps);
extern void polyrast_gouraud_blit_clipped(u16 *page_ptr, s16 *xs, s16 *ys);
extern void polyrast_spr_scaled_blit_planar(void *sprite, s16 x, s16 y, u16 flags, s16 w, s16 h,
                                            s16 colour);

#endif
