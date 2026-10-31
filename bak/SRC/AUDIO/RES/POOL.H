#ifndef BAK_POOL_H
#define BAK_POOL_H

#include "structs.h"

extern u32 g_pool_base;
extern u32 g_pool_top;
extern u32 g_nPoolSize;

extern u32 pool_get_size(void);
extern void pool_compute_size_from_music(void);
extern void pool_init(void);
extern void pool_reset(void);
extern void far *pool_alloc(u32 size);
extern s16 pool_is_managed_ptr(u32 ptr);
extern void far *pool_acquire_buffer(u32 size, s16 kind);

#endif
