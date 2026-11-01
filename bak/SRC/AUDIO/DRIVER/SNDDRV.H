#ifndef BAK_SNDDRV_H
#define BAK_SNDDRV_H

#include "structs.h"

extern void snddrv_patch_farptr(unsigned short offset, unsigned short segment);
/* MIDI.ASM, the only caller, passes a near pointer to an argument block on its stack. */
extern unsigned short snddrv_dispatch_cached(s16 cmd, void *data);
extern void snddrv_dispatch(s16 cmd, s16 data);

#endif
