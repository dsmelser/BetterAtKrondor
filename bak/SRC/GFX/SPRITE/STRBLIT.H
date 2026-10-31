#ifndef BAK_STRBLIT_H
#define BAK_STRBLIT_H

#include "structs.h"

extern s16 stretch_step_init(s16 *step, s16 dest_steps);
extern void blit_image_stretched(ImageRecord *image, s16 dst_x, s16 dst_y, u16 flags, s16 dst_w,
                                 s16 dst_h);
extern void blit_image_scaled_centered(ImageRecord *image, s16 cx, s16 cy, u32 scale_q16);

#endif
