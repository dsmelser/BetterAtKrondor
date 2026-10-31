/*
 * LZWDEC.ASM in C: the LZW decoder behind stream codec 2.
 *
 * Codes are 9 to 12 bits wide, read LSB-first in groups of as many bytes as the code
 * width (eight codes a group); a change of width or a reset (code 100h) discards the
 * rest of the group. The dictionary lives in the codec scratch block: prefix codes as
 * words from offset 0, suffix bytes from 2720h, and each code's string is spelled
 * backwards at 3720h before being copied out.
 *
 * The decoder runs until the caller's byte count is met. The byte after the last one
 * wanted goes to the stream's ring buffer and the decoder suspends in the middle of the
 * string, resuming there on the next call. g_pLzwEmitPos, a near pointer into the
 * string area in the original, is a pointer into it natively.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/STREAM/CODEC/CODEC.H"
#include "SRC/STREAM/CODEC/LZHINIT.H"
#include "SRC/STREAM/CODEC/LZWDEC.H"
#include "bak_game_end.h"

#define PREFIX_AT 0x0000
#define SUFFIX_AT 0x2720
#define STRING_AT 0x3720
#define RESET_CODE 0x100
#define MAX_CODES 0x1000

unsigned char g_lzwBitBuf[12] = {0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c,
                                 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c};
unsigned char g_lzwExtractMask[9] = {0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};
unsigned char *g_pLzwEmitPos;

static u16 prefix_of(const unsigned char *scratch, u16 code) {
    const unsigned char *p = scratch + PREFIX_AT + (u16)(code << 1);
    return (u16)(p[0] | p[1] << 8);
}

static void set_prefix(unsigned char *scratch, u16 code, u16 prefix) {
    unsigned char *p = scratch + PREFIX_AT + (u16)(code << 1);
    p[0] = (unsigned char)prefix;
    p[1] = (unsigned char)(prefix >> 8);
}

/* The next code from the bit buffer, refilling it a group at a time; FFFFh at the end
 * of the input. Register widths are kept: the bit cursor's high byte rides along in AX. */
u16 lzw_get_next_code(void) {
    u16 pos;
    if ((s16)g_wLzwNextCode > (s16)g_wLzwMaxCode) {
        g_wLzwCodeBits++;
        g_wLzwMaxCode =
            (unsigned char)g_wLzwCodeBits == 12 ? MAX_CODES : (u16)((1u << g_wLzwCodeBits) - 1);
        if (g_wLzwResetWidthPending != 0)
            goto reset_width;
        goto refill;
    }
    if (g_wLzwResetWidthPending != 0)
        goto reset_width;
    pos = g_wLzwInBitPos;
    if ((s16)pos >= (s16)g_wLzwInBufBitsTotal)
        goto refill;
    goto extract;

reset_width:
    g_wLzwCodeBits = 9;
    g_wLzwMaxCode = 0x1ff;
    g_wLzwResetWidthPending = 0;
refill: {
    u16 bits = g_wLzwCodeBits;
    s16 n = codec_raw_block_read(g_lzwBitBuf, bits);
    if (n <= 0) {
        g_wLzwInBufBitsTotal = (u16)n;
        return 0xffff;
    }
    g_wLzwInBitPos = 0;
    g_wLzwInBufBitsTotal = (u16)((u16)(n << 3) - (bits - 1));
    pos = 0;
}
extract: {
    u16 bits = g_wLzwCodeBits;
    unsigned char *p = g_lzwBitBuf + (pos >> 3);
    unsigned ch = pos & 7;      /* bit offset within the first byte */
    u16 ax = (u16)(pos + bits); /* AX before LODSB: its high byte survives */
    unsigned char bl;
    u16 dx;
    g_wLzwInBitPos = ax;
    ax = (u16)((ax & 0xff00) | *p++);
    ax = (u16)(ax >> ch);
    dx = ax;
    ch = (unsigned char)(8 - ch);    /* bits taken from the first byte */
    bl = (unsigned char)(bits - ch); /* bits still wanted */
    if ((signed char)bl >= 8) {
        ax = (u16)((ax & 0xff00) | *p++);
        ax = (u16)(ax << ch);
        dx |= ax;
        ch += 8;
        bl -= 8;
    }
    ax = (u16)(g_lzwExtractMask[(bits & 0xff00) | bl] & *p);
    return (u16)((u16)(ax << ch) | dx);
}
}

/* The seed: the first code after initialization is a literal that adds no entry. */
static void decode_seed(void) {
    u16 code;
    g_bLzwNeedSeedCode = 0;
    code = lzw_get_next_code();
    g_wLzwPrevCode = code;
    g_wLzwLastByte = code;
    codec_emit_byte((s16)code);
}

void lzw_decompress(void) {
    decode_seed();
    lzw_decode_step();
}

void lzw_decode_step(void) {
    unsigned char *scratch = g_pCurCodecScratch;
    unsigned char *string = scratch + STRING_AT;
    s16 si;
    u16 cx, code, di;
    unsigned char b;

    if (g_bLzwOutputSuspended != 0) {
        cx = (u16)(g_wCodecBytesRemaining + 1);
        si = (s16)(g_pLzwEmitPos - string);
        g_bLzwOutputSuspended = 0;
        goto resume;
    }
    if (g_bLzwNeedSeedCode != 0)
        decode_seed();

    for (;;) {
        code = lzw_get_next_code();
        if ((s16)code < 0)
            return;
        if (code == RESET_CODE) {
            /* The fill value and the counters come from the scratch pointer's offset. */
            u16 fill = FP_OFF(scratch);
            for (u16 i = 0; i < 0x100; i++)
                set_prefix(scratch, i, fill);
            g_wLzwResetWidthPending = (u16)(fill + 1);
            g_wLzwNextCode = (u16)((u16)(fill + 1) << 8 | (u16)(fill + 1) >> 8);
            code = lzw_get_next_code();
            if ((s16)code < 0)
                return;
        }

        /* Spell the string backwards; a code not yet in the dictionary is the previous
         * string plus its own first byte. */
        di = 0;
        si = (s16)code;
        g_wLzwNewCode = code;
        if ((s16)code >= (s16)g_wLzwNextCode) {
            string[di++] = (unsigned char)g_wLzwLastByte;
            si = (s16)g_wLzwPrevCode;
        }
        while (si >= RESET_CODE) {
            string[di++] = scratch[SUFFIX_AT + (u16)si];
            si = (s16)prefix_of(scratch, (u16)si);
        }
        b = scratch[(u16)(SUFFIX_AT + si)];
        string[di++] = b;
        g_wLzwLastByte = b;

        /* Copy it out, last byte spelled first, until the caller has what it asked for. */
        cx = (u16)(g_wCodecBytesRemaining + 1);
        si = (s16)(di - 1);
        for (;;) {
            b = string[si++];
            if (--cx == 0) {
                /* The byte past the request goes to the ring buffer; suspend here. */
                u16 head = (u16)(g_pCurStreamDesc->bRingHead | g_pCurStreamDesc->bRingTail << 8);
                g_pLzwEmitPos = string + si;
                g_pCurStreamRingBuf[(unsigned char)head] = b;
                head++; /* a word increment: it carries into the tail */
                g_pCurStreamDesc->bRingHead = (unsigned char)head;
                g_pCurStreamDesc->bRingTail = (unsigned char)(head >> 8);
                g_wCodecBytesRemaining = 0;
                g_bLzwOutputSuspended = 1;
                return;
            }
            if (g_bCurStreamOpMode & 0x40)
                *g_pStreamReadDst = b;
            g_pStreamReadDst++;
        resume:
            si = (s16)(si - 2);
            if (si < 0)
                break;
        }

        /* Add the previous string plus this one's first byte to the dictionary. */
        g_wCodecBytesRemaining = (u16)(cx - 1);
        if ((s16)g_wLzwNextCode < MAX_CODES) {
            set_prefix(scratch, g_wLzwNextCode, g_wLzwPrevCode);
            scratch[SUFFIX_AT + g_wLzwNextCode] = (unsigned char)g_wLzwLastByte;
            g_wLzwNextCode++;
        }
        g_wLzwPrevCode = g_wLzwNewCode;
    }
}
