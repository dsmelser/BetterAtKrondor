#ifndef BAK_DECOMP_H
#define BAK_DECOMP_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern void decomp_lzss(unsigned char far *dst, s16 remaining, void *inbuf, s16 buf_size, ResFile *fp);
extern void decomp_rle(unsigned char far *dst, s16 refill_count, s16 buf_ptr, s16 buf_size, ResFile *fp);

#endif
