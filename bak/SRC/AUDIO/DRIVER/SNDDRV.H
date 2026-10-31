#ifndef BAK_SNDDRV_H
#define BAK_SNDDRV_H

#include "structs.h"

extern void snddrv_patch_farptr(u16 offset, u16 segment);
extern u16 snddrv_dispatch_cached(s16 cmd, s16 data);
extern void snddrv_dispatch(s16 cmd, s16 data);

#endif
