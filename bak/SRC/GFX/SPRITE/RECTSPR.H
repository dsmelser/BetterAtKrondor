#ifndef BAK_RECTSPR_H
#define BAK_RECTSPR_H

#include "structs.h"

extern void draw_rect_filled(s16 x, s16 y, s16 w, s16 h);
extern void blit_sprite_flipped_clipped(void *sprite, s16 x, s16 y, s16 flags);

#endif
