#ifndef BAK_DECOMP_H
#define BAK_DECOMP_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern void decomp_lzss(unsigned char far *dst, s16 remaining, void *inbuf, s16 buf_size, ResFile *fp);
extern void decomp_rle(unsigned char far *dst, s16 refill_count, NearPtr buf_ptr, s16 buf_size, ResFile *fp);
/* The same into a block of plain memory, from its start: the block stands for the segment the
 * output's offset wraps in. */
extern void decomp_lzss_into(unsigned char *block, u32 size, s16 remaining, void *inbuf, s16 buf_size,
                             ResFile *fp);
extern void decomp_rle_into(unsigned char *block, u32 size, s16 refill_count, NearPtr buf_ptr,
                            s16 buf_size, ResFile *fp);

#endif
