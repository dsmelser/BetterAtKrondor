#include "structs.h"
#include "SRC/STREAM/CODEC/RLEWRITE.H"
#include "SRC/STREAM/CODEC/CODEC.H"

void near codec_rle_emit_run(s16 flush) {
    unsigned char tail;
    unsigned char head;
    u16 remaining;
    s16 runLen;
    u16 scanIdx;
    unsigned char isRepeat;
    unsigned char nextTail;
    register unsigned char *pRing;
    u16 runByte;

    pRing = g_pCurStreamRingBuf;
    tail = g_pCurStreamDesc->bRingTail;
    head = g_pCurStreamDesc->bRingHead;

    while ((remaining = (u16)head - (u16)tail & 0x7f) != 0) {
        runByte = 0xffff;
        scanIdx = (u16)tail;
        runLen = 1;
        do {
            if (pRing[scanIdx] == runByte) {
                runLen++;
            } else {
                if (runLen >= 3)
                    break;
                runLen = 1;
            }
            runByte = (u16)pRing[scanIdx];
            ++scanIdx;
        } while ((u16)head != (scanIdx &= 0x7f));

        isRepeat = 0;
        if (runLen >= 3) {
            nextTail = (unsigned char)scanIdx - (char)runLen & 0x7f;
            if (nextTail == tail) {
                isRepeat = 0x80;
                nextTail = (unsigned char)scanIdx;
            }
        } else {
            nextTail = head;
        }
        runLen = (u16)nextTail - (u16)tail & 0x7f;
        if (runLen == (s16)remaining && runLen < 0x7f && flush == 0)
            break;
        stream_putc((u16)runLen | isRepeat);
        if (isRepeat & 0x80) {
            stream_putc(runByte);
            tail = tail + (char)runLen & 0x7f;
        } else {
            while (runLen-- != 0) {
                stream_putc((u16)pRing[tail++]);
                tail &= 0x7f;
            }
        }
    }

    g_pCurStreamDesc->bRingTail = tail;
    g_pCurStreamDesc->bRingHead = head;
}
