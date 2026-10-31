#ifndef BAK_LZW_H
#define BAK_LZW_H

#include "structs.h"

extern unsigned short g_nLzwTableSize;
extern unsigned short g_nLzwNextFreeCode;
extern unsigned short g_nLzwMode;
extern unsigned short g_bLzwClearPending;
extern s32 g_lLzwBestRatio;
extern s32 g_lLzwRatioThresh;
extern s32 g_lLzwInputCount;
extern s32 g_lLzwOutputCount;
extern unsigned char g_lzwHiMask[9];
extern unsigned char g_lzwLoMask[9];
extern unsigned short g_wLzwEncCodeBits;
extern unsigned short g_wLzwEncMaxCode;
extern unsigned short g_wLzwOutBitPos;
extern s32 g_lLzwOutByteCount;
extern unsigned char g_bLzwEncSeedPending;
extern unsigned short g_wLzwEncCurCode;
extern unsigned char g_lzwEncOutBuf[12];

extern s16 near lzw_reset_state(void);
extern s16 near lzw_decompressor_init(void);
extern s16 near lzw_compress_input(s16 flush);
extern void near lzw_emit_code(s16 code);
extern s32 thunk_LDIV_at(s32 numer, s32 denom);
extern void near lzw_check_ratio(void);
extern void near lzw_clear_code_table(s32 count);

#endif
