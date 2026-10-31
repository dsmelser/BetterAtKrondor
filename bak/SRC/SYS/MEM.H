#ifndef BAK_MEM_H
#define BAK_MEM_H

#include "structs.h"

extern void *galloc_safe_zcalloc(u16 size);
extern void galloc_zfree(void *ptr);
extern unsigned char far *galloc_calloc_far(u32 size, s16 flags);
extern void galloc_safe_farfree(void huge *ptr);

#endif
