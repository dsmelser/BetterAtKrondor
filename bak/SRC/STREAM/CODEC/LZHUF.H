#ifndef BAK_LZHUF_H
#define BAK_LZHUF_H

#include "structs.h"

extern u16 g_lzh_bit_buf;
extern unsigned char g_lzh_bit_count;
extern u16 g_lzhPutBuf;
extern unsigned char g_lzhPutLen;
extern unsigned char g_lzhPLen[64];
extern unsigned char g_lzhPCode[64];
extern unsigned char g_lzhDCode[256];
extern unsigned char g_lzhDLen[256];
extern u16 g_wLzhDecInitialized;
extern u16 g_wLzhEncLastBitVal;
extern unsigned char far *g_pLzssTextBuf;
extern u16 far *g_pLzhParent;
extern u16 far *g_pLzhFreq;
extern u16 far *g_pLzssDad;
extern s16 g_nLzhEncLastBitCount;
extern u16 far *g_pLzhSon;
extern u16 g_wLzhEncInitialized;
extern s16 g_nLzssMatchPosition;
extern u16 far *g_pLzssRson;
extern u16 far *g_pLzssLson;
extern s16 g_nLzssMatchLength;
extern u32 g_dwLzhDecBytesTarget;
extern u32 g_dwLzhDecBytesOut;
extern u16 g_wLzhDecR;
extern s16 g_nLzhDecMatchIdx;
extern s16 g_nLzhDecMatchLen;
extern u16 g_wLzhDecMatchBack;
extern u16 g_wLzhDecPaused;
extern s16 g_nLzhEncLastMatchLen;
extern u16 g_wLzhEncS;
extern u16 g_wLzhEncR;
extern s16 g_nLzhEncLookaheadLen;
extern u16 g_wLzhEncCurChar;
extern s16 g_nLzhEncEmitIdx;
extern u16 g_wLzhEncPaused;

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
extern u16 near lzh_decode_char(void);
extern s16 near lzh_encode(s16 flush);
extern s16 near lzh_decode(void);
extern s16 near lzh_ring_buffer_write(void);
extern s16 near lzh_decode_match_emit(void);

#endif
