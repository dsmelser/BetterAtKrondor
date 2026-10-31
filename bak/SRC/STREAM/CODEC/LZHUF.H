#ifndef BAK_LZHUF_H
#define BAK_LZHUF_H

#include "structs.h"

extern unsigned short g_lzh_bit_buf;
extern unsigned char g_lzh_bit_count;
extern unsigned short g_lzhPutBuf;
extern unsigned char g_lzhPutLen;
extern unsigned char g_lzhPLen[64];
extern unsigned char g_lzhPCode[64];
extern unsigned char g_lzhDCode[256];
extern unsigned char g_lzhDLen[256];
extern unsigned short g_wLzhDecInitialized;
extern unsigned short g_wLzhEncLastBitVal;
extern unsigned char far *g_pLzssTextBuf;
extern unsigned short far *g_pLzhParent;
extern unsigned short far *g_pLzhFreq;
extern unsigned short far *g_pLzssDad;
extern short g_nLzhEncLastBitCount;
extern unsigned short far *g_pLzhSon;
extern unsigned short g_wLzhEncInitialized;
extern short g_nLzssMatchPosition;
extern unsigned short far *g_pLzssRson;
extern unsigned short far *g_pLzssLson;
extern short g_nLzssMatchLength;
extern u32 g_dwLzhDecBytesTarget;
extern u32 g_dwLzhDecBytesOut;
extern unsigned short g_wLzhDecR;
extern short g_nLzhDecMatchIdx;
extern short g_nLzhDecMatchLen;
extern unsigned short g_wLzhDecMatchBack;
extern unsigned short g_wLzhDecPaused;
extern short g_nLzhEncLastMatchLen;
extern unsigned short g_wLzhEncS;
extern unsigned short g_wLzhEncR;
extern short g_nLzhEncLookaheadLen;
extern unsigned short g_wLzhEncCurChar;
extern short g_nLzhEncEmitIdx;
extern unsigned short g_wLzhEncPaused;

extern void near lzh_init_huffman_freq_tables(void);
extern void near lzss_insert_node(s16 r);
extern void near lzss_delete_node(s16 p);
extern s16 near lzh_get_bit(void);
extern s16 near lzh_get_byte(void);
extern void near lzh_put_bits(char nbits, u16 bits);
extern void near lzh_start_huff(void);
extern void near lzh_huff_update_leaf(s16 leaf_index);
extern void near lzh_encode_char(s16 code);
extern void near lzh_encode_position(u16 position);
extern void near lzh_encode_end(void);
extern unsigned short near lzh_decode_char(void);
extern s16 near lzh_encode(s16 flush);
extern s16 near lzh_decode(void);
extern s16 near lzh_ring_buffer_write(void);
extern s16 near lzh_decode_match_emit(void);

#endif
