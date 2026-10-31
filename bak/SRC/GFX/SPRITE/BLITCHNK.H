#ifndef BAK_BLITCHNK_H
#define BAK_BLITCHNK_H

#include "structs.h"

extern void blit_chunky(unsigned char far *src, s16 dst_x, s16 dst_y, s16 width_bytes, s16 height);
extern void blit_chunky_planar_4bit();

#endif
