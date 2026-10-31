#ifndef BAK_FARPTR_H
#define BAK_FARPTR_H

#include "structs.h"

extern u32 near normalize_far_ptr(u16 offset, u16 seg);
extern void near normalize_far_ptr_flatten();
extern u32 near far_ptr_add_offset(u16 ptr_lo, u16 addend, u16 rotate, u16 ptr_hi);
extern void near far_ptr_sub_offset(void);
extern void near memcpy_far_backward(void);
extern void near memcpy_far_forward(void);
extern void fmemmove(unsigned char far *dst, unsigned char far *src, u32 n);
extern void fmemcpy_far(u32 dst, u32 src, u16 count);
extern void memset_far(void far *dst, s16 value, u32 count);
extern s16 far_ptr_cmp(void far *a, void far *b);
extern unsigned char far *normalize_far_ptr_thunk(unsigned char far *p);

#endif
