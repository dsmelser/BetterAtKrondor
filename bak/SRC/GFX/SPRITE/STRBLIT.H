#ifndef BAK_STRBLIT_H
#define BAK_STRBLIT_H

#include "structs.h"

/* A stretch's walk along one axis of the source, in 16.16: where it is (pos, whose high word is
 * the source pixel) and how far each destination pixel moves it (step). The original kept them
 * as four words, the caller putting the first source pixel in pos's high word and the last in
 * step's before stretch_step_init. */
typedef union StretchStep {
    s16 w[4]; /* pos's low and high words, step's low and high words */
    s32 l[2]; /* pos, step */
} StretchStep;

extern s16 stretch_step_init(StretchStep *step, s16 dest_steps);
extern void blit_image_stretched(ImageRecord *image, s16 dst_x, s16 dst_y, u16 flags, s16 dst_w,
                                 s16 dst_h);
extern void blit_image_scaled_centered(ImageRecord *image, s16 cx, s16 cy, u32 scale_q16);

#endif
