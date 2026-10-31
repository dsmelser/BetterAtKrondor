#ifndef BAK_CODEC_H
#define BAK_CODEC_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern unsigned char huge *g_pCodecScratchFp;
extern CodecVtable g_codec_vtable[4];
extern unsigned char g_abLowBitMaskByWidth[9];
extern StreamDesc *g_pCurStreamDesc;
extern unsigned char *g_pCurStreamRingBuf;
extern unsigned char g_bCurStreamCodecId;
extern ResFile *g_pCurStreamFp;
extern unsigned char huge *g_pStreamReadDst;
extern unsigned char huge *g_pCurCodecScratch;
extern StreamDesc *g_apStreamSlots[100];
extern s16 g_nStreamBytesWritten;
extern unsigned short g_wCodecBytesRemaining;
extern unsigned char g_bCurStreamOpMode;
extern unsigned char g_bCurStreamDescFlags;
extern char huge *g_pCurStreamSrcCursor;
extern unsigned char g_codecRawTransitBuf[50];
extern unsigned char g_bssgap_5de5;
extern IffResReader g_chunkSeekSaveIffReader;
extern IffResReader g_iff_reader_pool[4];

extern s16 near codec_passthrough_bytes(void);
extern s16 near codec_rle_read(void);
extern s16 near codec_flush_ring(void);
extern s16 near codec_raw_read(unsigned char huge *dst, u16 count);
extern s16 near stream_getc(void);
extern s16 near codec_raw_block_read(void *dest, u16 count);
extern s16 near codec_rle_literal_run(u16 param_1);
extern s16 near codec_rle_repeat_byte(s16 value, s16 count);
extern s16 near codec_emit_byte(s16 byte_val);
extern s16 near stream_putc(s16 c);
extern s16 near stream_select(s16 stream_id);
extern s16 near mode_has_read(char *mode);
extern void near free_if_not_null(void *ptr);
extern s16 near stream_release_slot(s16 slot);
extern s16 near stream_alloc_slot(char *mode);
extern s16 near codec_init(s16 codec_id, char *mode);
extern void near stream_drain_chunk_window(void);
extern u16 near stream_consume(s16 stream_id, u16 count);

#endif
