#ifndef BAK_BLITAA_H
#define BAK_BLITAA_H

#include "structs.h"

extern u32 g_pAaPaletteBuf;

extern void blit_sprite_aa_edges(ImageRecord *sprite, s16 dst_x, s16 dst_y, unsigned char far *palette);

#endif
