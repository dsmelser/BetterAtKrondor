#ifndef BAK_SPRTHNKS_H
#define BAK_SPRTHNKS_H

#include "structs.h"

extern void blit_sprite(NearPtr sprite_desc_offset, s16 x, s16 y);
extern void blit_sprite_indirect(NearPtr sprite_desc_offset, short x, short y, unsigned short flip_flags);
extern void gfx_putsprite_fp(ImageRecord *sprite, s16 x, s16 y);
/* Slot 37, one row of a sprite stretched (STRBLIT.C, which called the slot itself): count
 * pixels from x on the row at byte offset dst_off of the page dst_seg, pixel i from the source
 * row at the column columns[i] names, a zero pixel left out. */
extern void blit_stretched_dispatch(u16 dst_seg, u16 dst_off, const unsigned char *src_row,
                                    const s16 *columns, u16 count, u16 x);

#endif
