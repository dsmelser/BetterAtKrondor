#include <dos.h>
#include "structs.h"
#include "SRC/STREAM/CODEC/STREAM.H"

#define fmemcpy_far fmemcpy_far__proto
#include "SRC/IO/RESOURCE.H"
#include "SRC/SYS/FARPTR.H"
#include "SRC/STREAM/CODEC/CODEC.H"
#undef fmemcpy_far
extern void fmemcpy_far();

s16 stream_open(s16 codec_kind, ResFile *file, char *mode, u32 size) {
    s16 slot;
    char buf[4];

    if ((slot = stream_alloc_slot(mode)) == -1)
        return -1;

    g_pCurStreamDesc->src.pFile = file;
    g_pCurStreamDesc->dwFileStartOff = res_ftell(file);
    g_pCurStreamDesc->dwInPos = 5;

    if (mode_has_read(mode)) {
        if (codec_init(codec_kind = (unsigned char)(g_pCurStreamDesc->bFlags = res_fgetc(file)),
                       mode) == -1) {
            res_fseek(file, -1L, SEEK_CUR);
            goto release;
        }
        g_pCurStreamDesc->dwInSize = size;
        res_fread(&g_pCurStreamDesc->dwUncompressedSize, 1, 4, file);
        if (g_codec_vtable[codec_kind].pOpen_read)
            (*(void(near *)(void))g_codec_vtable[codec_kind].pOpen_read)();
        g_pCurStreamDesc->bFlags |= 0x40;
    } else {
        if (codec_init(codec_kind, mode) == -1) {
        release:
            return stream_release_slot(slot);
        }
        res_putc(codec_kind, file);
        res_fwrite(buf, 1, 4, file);
        if (g_codec_vtable[codec_kind].pOpen_write)
            (*(void(near *)(void))g_codec_vtable[codec_kind].pOpen_write)();
    }
    g_pCurStreamDesc->bFlags |= 0x20;
    return slot;
}

s16 stream_open_from_memory(s16 codec_id, unsigned char huge *pSrc, char *mode, u32 size) {
    s16 slot;

    if ((slot = stream_alloc_slot(mode)) == -1)
        return -1;

    g_pCurStreamDesc->src.pBufBase = pSrc;
    g_pCurStreamDesc->bFlags = (unsigned char)codec_id;
    g_pCurStreamDesc->dwInPos = 5;

    if (mode_has_read(mode)) {
        if (codec_init(codec_id = (unsigned char)(g_pCurStreamDesc->bFlags = *pSrc++), mode) == -1)
            goto release;
        fmemcpy_far((unsigned char far *)&g_pCurStreamDesc->dwUncompressedSize,
                    (unsigned char far *)pSrc, 4);
        g_pCurStreamDesc->dwInSize = size;
        if (g_codec_vtable[codec_id].pOpen_read)
            (*(void(near *)(void))g_codec_vtable[codec_id].pOpen_read)();
        g_pCurStreamDesc->bFlags |= 0x40;
    } else {
        if (codec_init(codec_id, mode) == -1) {
        release:
            return stream_release_slot(slot);
        }
        *g_pCurStreamDesc->src.pBufBase = (unsigned char)codec_id;
    }
    return slot;
}

s16 stream_close(s16 stream_id) {
    if (stream_select(stream_id) == 0)
        return -1;

    g_nStreamBytesWritten = 0;
    if (!(g_bCurStreamDescFlags & 0x40)) {
        (*(void(near *)(s16))g_codec_vtable[(unsigned char)g_bCurStreamCodecId].pWrite_chunk)(1);
        if (g_bCurStreamDescFlags & 0x20) {
            res_fseek(g_pCurStreamFp, g_pCurStreamDesc->dwFileStartOff + 1, SEEK_SET);
            res_fwrite(&g_pCurStreamDesc->dwUncompressedSize, 4, 1, g_pCurStreamFp);
            res_fseek(g_pCurStreamFp, 0L, SEEK_END);
        } else {
            fmemcpy_far((unsigned char far *)(g_pCurStreamDesc->src.pBufBase + 1),
                        (unsigned char far *)&g_pCurStreamDesc->dwUncompressedSize, 4);
        }
    }
    stream_release_slot(stream_id);
    return g_nStreamBytesWritten;
}

u16 stream_read(s16 handle, void far *dest, u16 count) {
    if (stream_select(handle) == 0)
        return 0xffff;
    g_pStreamReadDst = normalize_far_ptr_thunk((unsigned char far *)dest);
    g_bCurStreamOpMode |= 0x40;
    return stream_consume(handle, count);
}

u16 stream_write(s16 stream_id, void far *pSrc, u16 count) {
    register unsigned char *pBuf; /* held in DI as the ring-buffer base across the copy loop */
    unsigned char huge *src;
    u16 ring_head, ring_end;
    if (stream_select(stream_id) == 0)
        return 0xffff;
    g_nStreamBytesWritten = 0;
    g_pCurStreamDesc->dwUncompressedSize += count;
    pBuf = g_pCurStreamDesc->pBuf;
    src = (unsigned char huge *)pSrc;
    while (count != 0) {
        ring_head = g_pCurStreamDesc->bRingHead;
        ring_end = (g_pCurStreamDesc->bRingTail - 1) & 0x7f;
        do {
            pBuf[ring_head++] = *src++;
            count--;
            ring_head &= 0x7f;
        } while (ring_head != ring_end && count != 0);
        g_pCurStreamDesc->bRingHead = ring_head & 0x7f;
        (*(void(near *)(s16))g_codec_vtable[(unsigned char)g_bCurStreamCodecId].pWrite_chunk)(0);
    }
    return g_nStreamBytesWritten;
}

u32 stream_size(s16 stream_id) {
    if (stream_select(stream_id) == 0) {
        return 0xffffffffUL;
    }
    return g_pCurStreamDesc->dwUncompressedSize;
}

s32 stream_seek(s16 handle, s32 position, s16 origin) {
    s32 pos;

    if (!stream_select(handle))
        return -1L;

    pos = 0;
    switch (origin) {
    case 1:
        pos = g_pCurStreamDesc->dwOutPos;
        break;
    case 2:
        pos = g_pCurStreamDesc->dwUncompressedSize;
        break;
    }
    pos += position;

    if (g_pCurStreamDesc->dwOutPos == pos)
        return pos;

    if ((s32)g_pCurStreamDesc->dwOutPos > pos) {
        stream_rewind(handle);
        if (pos <= 0)
            return 0L;
    } else {
        if ((s32)g_pCurStreamDesc->dwUncompressedSize <= pos) {
            pos = g_pCurStreamDesc->dwUncompressedSize - g_pCurStreamDesc->dwOutPos;
        } else {
            pos -= (s32)g_pCurStreamDesc->dwOutPos;
        }
    }
    while ((pos -= stream_consume(handle, pos < 32000L ? (u16)pos : 32000)) != 0) {
        g_pCurStreamSrcCursor = (char huge *)normalize_far_ptr_thunk(
            g_pCurStreamDesc->src.pBufBase + g_pCurStreamDesc->dwInPos);
    }
    return g_pCurStreamDesc->dwOutPos;
}

s16 stream_rewind(s16 stream_id) {
    if (!stream_select(stream_id) || !(g_bCurStreamDescFlags & 0x40))
        return -1;
    if (g_codec_vtable[(unsigned char)g_bCurStreamCodecId].pOpen_read)
        (*(void(near *)(void))g_codec_vtable[(unsigned char)g_bCurStreamCodecId].pOpen_read)();
    g_pCurStreamDesc->dwInPos = 5;
    if (g_pCurStreamDesc->bFlags & 0x20) {
        res_fseek(g_pCurStreamFp, g_pCurStreamDesc->dwFileStartOff + 5, SEEK_SET);
    } else {
        g_pCurStreamSrcCursor =
            (char huge *)normalize_far_ptr_thunk(g_pCurStreamDesc->src.pBufBase + 5);
    }
    g_pCurStreamDesc->bRingHead = g_pCurStreamDesc->bRingTail =
        (unsigned char)(g_pCurStreamDesc->dwOutPos = 0);
    return 0;
}
