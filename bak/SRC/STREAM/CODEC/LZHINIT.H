#ifndef BAK_LZHINIT_H
#define BAK_LZHINIT_H

#include "structs.h"

extern u16 g_wLzwCodeBits;
extern u16 g_wLzwMaxCode;
extern u16 g_wLzwNextCode;
extern u16 g_wLzwResetWidthPending;
extern u16 g_wLzwLastByte;
extern u16 g_wLzwPrevCode;
extern u16 g_wLzwNewCode;
extern unsigned char far *g_pLzwScratchTail;
extern u16 g_wLzwInBitPos;
extern u16 g_wLzwInBufBitsTotal;
extern unsigned char g_bLzwNeedSeedCode;
extern unsigned char g_bLzwOutputSuspended;

extern void near lzh_decoder_init(void);
extern void near lzh_module_unused_stub_a(void);
extern void near lzh_module_unused_stub_b(void);

#endif
