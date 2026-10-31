#ifndef BAK_SNDDRV_H
#define BAK_SNDDRV_H

#include "structs.h"

extern void snddrv_patch_farptr(unsigned short offset, unsigned short segment);
extern unsigned short snddrv_dispatch_cached(s16 cmd, s16 data);
extern void snddrv_dispatch(s16 cmd, s16 data);

#endif
