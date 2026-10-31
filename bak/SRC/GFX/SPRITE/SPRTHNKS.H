#ifndef BAK_SPRTHNKS_H
#define BAK_SPRTHNKS_H

#include "structs.h"

extern void blit_sprite(void);
extern void blit_sprite_indirect(u16 sprite_desc_offset, s16 x, s16 y, u16 flip_flags);
extern void gfx_putsprite_fp(ImageRecord *sprite, s16 x, s16 y);
extern void blit_stretched_dispatch(void);

#endif
