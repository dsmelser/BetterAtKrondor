#ifndef BAK_EMSIMG_H
#define BAK_EMSIMG_H

#include "structs.h"

extern void emsimg_free_paged(void *pImgRecord);
extern void emsimg_polygon_quad_canonicalize();
extern void emsimg_gouraud_blit_paged(ImageRecord *texture, s16 *xs, s16 *ys);
extern void emsimg_sprite_blit_scaled_paged(ImageRecord *sprite, s16 dst_x, s16 dst_y, u16 frame,
                                            s16 width, s16 height);
extern void emsimg_putsprite_ems_swap(ImageRecord *sprite, s16 x, s16 y);
extern void emsimg_map_then_call_180c(ImageRecord *sprite, s16 x, s16 y, u16 flip_flags);

#endif
