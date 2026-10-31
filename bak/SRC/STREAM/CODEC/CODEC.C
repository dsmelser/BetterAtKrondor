#include "structs.h"
#include "SRC/STREAM/CODEC/CODEC.H"

#define fmemcpy_far fmemcpy_far__proto
#include "SRC/IO/RESOURCE.H"
#include "SRC/SYS/FARTHUNK.H"
#include "SRC/SYS/DOSMEM.H"
#include "SRC/SYS/FARPTR.H"
#include "SRC/STREAM/CODEC/BLITRLE.H"
#include "SRC/STREAM/CODEC/RLEWRITE.H"
#include "SRC/STREAM/CODEC/LZWDEC.H"
#include "SRC/STREAM/CODEC/LZW.H"
#include "SRC/STREAM/CODEC/LZHINIT.H"
#include "SRC/STREAM/CODEC/LZHUF.H"
#include "SRC/STREAM/CODEC/LZHOPEN.H"

IffResReader g_iff_reader_pool[4];
IffResReader g_chunkSeekSaveIffReader;
unsigned char g_bssgap_5de5;
unsigned char g_codecRawTransitBuf[50];
char huge *g_pCurStreamSrcCursor;
unsigned char g_bCurStreamDescFlags;
unsigned char g_bCurStreamOpMode;
u16 g_wCodecBytesRemaining;
s16 g_nStreamBytesWritten;
StreamDesc *g_apStreamSlots[100];
unsigned char huge *g_pCurCodecScratch;
unsigned char huge *g_pStreamReadDst;
ResFile *g_pCurStreamFp;
unsigned char g_bCurStreamCodecId;
unsigned char *g_pCurStreamRingBuf;
StreamDesc *g_pCurStreamDesc;

unsigned char huge *g_pCodecScratchFp = {0};
CodecVtable g_codec_vtable[4] = {
    {0x0080, 0x0000, 0x0000, (void *)codec_passthrough_bytes, (void *)codec_flush_ring, 0, 0},
    {0x0080, 0x0000, 0x0000, (void *)codec_rle_read, (void *)codec_rle_emit_run, 0, 0},
    {0x0080, 0x3ab3, 0x7566, (void *)lzw_decode_step, (void *)lzw_compress_input,
     (void *)lzw_decompressor_init, (void *)lzh_decoder_init},
    {0x0080, 0x2163, 0x2163, (void *)lzh_decode, (void *)lzh_encode,
     (void *)lzss_alloc_codec_buffers, (void *)lzh_stream_open}};
unsigned char g_abLowBitMaskByWidth[9] = {0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};
#undef fmemcpy_far
extern void fmemcpy_far();

s16 near codec_passthrough_bytes(void) {
    register s16 result = 1;
    register s16 byte_val;

    while (result && (byte_val = stream_getc()) != -1)
        result = codec_emit_byte(byte_val);
    return 0;
}

s16 near codec_rle_read(void) {
    register s16 token;
    register s16 done = 1;

    if (!(g_bCurStreamOpMode & 0x20))
        return blit_rle_sprite();
    while (done && (token = stream_getc()) != -1)
        done = (token & 0x80) ? codec_rle_repeat_byte(stream_getc(), token & 0x7f)
                              : codec_rle_literal_run(token & 0x7f);
    return 0;
}

s16 near codec_flush_ring(void) {
    register unsigned char *ringBuf;
    u16 tail;

    tail = (u16)g_pCurStreamDesc->bRingTail;
    ringBuf = g_pCurStreamRingBuf;
    while (g_pCurStreamDesc->bRingHead != tail) {
        stream_putc(ringBuf[tail++]);
        tail &= 0x7f;
    }
    g_pCurStreamDesc->bRingTail = (unsigned char)tail;
    return 0;
}

s16 near codec_raw_read(unsigned char huge *dst, u16 count) {
    s16 chunk;
    s16 nread;

    nread = 1;
    while (count > 0 && nread > 0) {
        chunk = count > 0x32 ? 0x32 : count;
        count -= (nread = res_fread(g_codecRawTransitBuf, 1, chunk, g_pCurStreamFp));
        fmemcpy_far((unsigned char far *)dst, (unsigned char far *)g_codecRawTransitBuf, nread);
        dst += nread;
    }
    return 0;
}

s16 near stream_getc(void) {
    if (g_pCurStreamDesc->dwInPos == g_pCurStreamDesc->dwInSize)
        return -1;
    g_pCurStreamDesc->dwInPos += 1;
    if (g_bCurStreamDescFlags & 0x20)
        return res_fgetc(g_pCurStreamFp);
    return *g_pCurStreamSrcCursor++ & 0xff;
}

s16 near codec_raw_block_read(register void *dest, register u16 count) {
    s32 remaining;

    if ((remaining = g_pCurStreamDesc->dwInSize - g_pCurStreamDesc->dwInPos) == 0)
        return 0;
    remaining = count > remaining ? remaining : count;
    g_pCurStreamDesc->dwInPos += remaining;
    if (g_bCurStreamDescFlags & 0x20)
        return res_fread(dest, 1, (u16)remaining, g_pCurStreamFp);
    fmemcpy_far((unsigned char far *)dest, (unsigned char far *)g_pCurStreamSrcCursor, (u16)remaining);
    g_pCurStreamSrcCursor += remaining;
    return (s16)remaining;
}

s16 near codec_rle_literal_run(u16 count) {
    g_pCurStreamDesc->dwInPos += count;
    if (g_wCodecBytesRemaining >= count) {
        if (g_bCurStreamOpMode & 0x40)
            codec_raw_read(g_pStreamReadDst, count);
        else
            res_fseek(g_pCurStreamFp, (s32)count, SEEK_CUR);
        g_wCodecBytesRemaining -= count;
        g_pStreamReadDst += count;
        return 1;
    } else {
        g_pCurStreamDesc->bRingHead += count;
        codec_raw_read(g_pCurStreamRingBuf, count);
        return 0;
    }
}

s16 near codec_rle_repeat_byte(register s16 value, s16 count) {
    if (g_wCodecBytesRemaining >= count) {
        if (g_bCurStreamOpMode & 0x40)
            memset_far(g_pStreamReadDst, value, count);
        g_wCodecBytesRemaining -= count;
        g_pStreamReadDst += count;
        return 1;
    } else {
        memset_far(g_pCurStreamRingBuf + g_pCurStreamDesc->bRingHead, value, count);
        g_pCurStreamDesc->bRingHead += count;
        return 0;
    }
}

s16 near codec_emit_byte(s16 byte_val) {
    if (g_wCodecBytesRemaining >= 1) {
        if (g_bCurStreamOpMode & 0x40)
            *g_pStreamReadDst = (unsigned char)byte_val;
        g_pStreamReadDst += 1;
        g_wCodecBytesRemaining--;
        return 1;
    } else {
        g_pCurStreamRingBuf[g_pCurStreamDesc->bRingHead++] = (unsigned char)byte_val;
        return 0;
    }
}

s16 near stream_putc(s16 c) {
    g_nStreamBytesWritten++;
    if (g_bCurStreamDescFlags & 0x20)
        return res_putc(c, g_pCurStreamFp);
    else
        return (char)(g_pCurStreamDesc->src.pBufBase[g_pCurStreamDesc->dwInPos++] = (char)c);
}

s16 near stream_select(s16 stream_id) {
    if (stream_id < 0 || stream_id >= 100 || (g_pCurStreamDesc = g_apStreamSlots[stream_id]) == 0)
        return 0;
    g_pCurCodecScratch = g_pCurStreamDesc->pScratch;
    g_pCurStreamRingBuf = g_pCurStreamDesc->pBuf;
    g_bCurStreamCodecId = (g_bCurStreamDescFlags = g_pCurStreamDesc->bFlags) & 0x1f;
    if (g_bCurStreamDescFlags & 0x20) {
        g_pCurStreamFp = g_pCurStreamDesc->src.pFile;
        g_bCurStreamOpMode = 0x20;
    } else {
        g_bCurStreamOpMode = 0;
        g_pCurStreamSrcCursor = (char huge *)normalize_far_ptr_thunk(
            g_pCurStreamDesc->src.pBufBase + g_pCurStreamDesc->dwInPos);
    }
    return 1;
}

s16 near mode_has_read(char *mode) {
    while (*mode != '\0') {
        if (*mode++ == 'r') {
            return 1;
        }
    }
    return 0;
}

void near free_if_not_null(void *ptr) {
    if (ptr != (void *)0)
        my_free(ptr);
}

s16 near stream_release_slot(s16 slot) {
    if ((g_pCurStreamDesc = g_apStreamSlots[slot]) != 0) {
        free_if_not_null(g_pCurStreamDesc->pBuf);
        if (g_pCurStreamDesc->pScratch != 0 && *(s32 *)&g_pCodecScratchFp == 0)
            _freemem(g_pCurStreamDesc->pScratch);
    }
    free_if_not_null(g_pCurStreamDesc);
    g_apStreamSlots[slot] = 0;
    return -1;
}

s16 near stream_alloc_slot(char *mode) {
    s16 slot;
    for (slot = 0; slot < 100; slot++) {
        if (g_apStreamSlots[slot] == 0)
            break;
    }
    if (slot == 100)
        return -1;
    if ((g_pCurStreamDesc = my_calloc(1, sizeof(StreamDesc))) == 0)
        return -1;
    g_apStreamSlots[slot] = g_pCurStreamDesc;
    return slot;
}

s16 near codec_init(s16 codec_id, char *mode) {
    s16 buf_size;
    CodecVtable *pv;
    u16 scratch_size;

    if (codec_id > 3)
        return -1;
    pv = &g_codec_vtable[codec_id];
    buf_size = 0x80;
    if (mode_has_read(mode)) {
        buf_size = pv->wBuf_size;
        scratch_size = pv->wScratch_size_read;
    } else {
        scratch_size = pv->wScratch_size_write;
    }
    if ((g_pCurStreamDesc->pBuf = (unsigned char *)my_calloc(1, buf_size)) == 0)
        return -1;
    if (scratch_size != 0) {
        if (g_pCodecScratchFp != 0) {
            g_pCurCodecScratch = g_pCurStreamDesc->pScratch = g_pCodecScratchFp;
        } else {
            g_pCurCodecScratch = g_pCurStreamDesc->pScratch =
                (unsigned char huge *)alloc_far(scratch_size, 0L);
        }
        if ((unsigned char far *)g_pCurStreamDesc->pScratch == 0)
            return -1;
    }
    g_pCurStreamDesc->bFlags = (unsigned char)codec_id;
    return 0;
}

void near stream_drain_chunk_window(void) {
    u16 count;
    u16 tail;

    tail = g_pCurStreamDesc->bRingTail;
    count = (u16)g_pCurStreamDesc->bRingHead - tail;
    if (count > g_wCodecBytesRemaining) {
        g_pCurStreamDesc->bRingTail += (count = g_wCodecBytesRemaining);
    } else {
        g_pCurStreamDesc->bRingTail = g_pCurStreamDesc->bRingHead = 0;
    }
    if (count != 0) {
        if ((g_bCurStreamOpMode & 0x40) != 0) {
            fmemcpy_far((unsigned char far *)g_pStreamReadDst, (unsigned char far *)(g_pCurStreamRingBuf + tail),
                        count);
        }
        g_wCodecBytesRemaining -= count;
        g_pStreamReadDst += count;
    }
}

u16 near stream_consume(s16 stream_id, u16 count) {
    u16 delivered;
    delivered = count;
    g_wCodecBytesRemaining = delivered;
    stream_drain_chunk_window();
    if (g_wCodecBytesRemaining != 0) {
        (*(void(near *)(void))g_codec_vtable[(unsigned char)g_bCurStreamCodecId].pRead_chunk)();
        if (g_wCodecBytesRemaining != 0)
            stream_drain_chunk_window();
    }
    delivered -= g_wCodecBytesRemaining;
    g_pCurStreamDesc->dwOutPos += delivered;
    return delivered;
}
