/*
 * DECOMP.ASM in C: the BMX images' codecs, LZSS and a byte RLE.
 *
 * Both read the compressed data through a buffer that a pump refills from the file:
 * min(buffer size, bytes left) each time, the count of bytes left kept by the caller.
 * They write through ES:DI, so the output stays in its segment: its offset wraps at
 * 64 KB, and an LZSS back-reference is an offset in that segment, copied a word at a
 * time. Output past the segment's block stops here; the original writes on into DOS
 * memory.
 *
 * The LZSS decoder has a fast path for when the buffer holds more than 30 bytes, which
 * reads a flag byte's whole run without checking for a refill, and a slow one that
 * checks before every byte; the two hand over in the middle of a flag byte, so the
 * labels follow the original's.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GFX/SPRITE/DECOMP.H"
#include "bak_game_end.h"

static void file_read(unsigned char *buf, u16 size, ResFile *fp) {
    res_fread(buf, (s16)size, 1, fp);
}

/* res_fread(buf, size, 1, fp); the differential test puts its source here. */
void (*g_decompRead)(unsigned char *buf, u16 size, ResFile *fp) = file_read;

typedef struct {
    unsigned char *seg; /* the output's segment, from its offset 0 */
    u32 extent;         /* the bytes its block holds from there */
    u16 di;
    u16 remaining; /* bytes of the file not read yet */
    unsigned char *buf;
    u16 bufSize;
    ResFile *fp;
    const unsigned char *si; /* the next byte in the buffer */
    u16 cx;                  /* bytes left in it */
} Decomp;

static void start(Decomp *d, unsigned char *dst, s16 remaining, unsigned char *buf, s16 bufSize,
                  ResFile *fp) {
    u16 seg = FP_SEG(dst);
    d->seg = MK_FP(seg, 0);
    d->extent = bak_seg_extent(seg);
    d->di = FP_OFF(dst);
    d->remaining = (u16)remaining;
    d->buf = buf;
    d->bufSize = (u16)bufSize;
    d->fp = fp;
}

/* decomp_stream_refill: min(buffer size, bytes left) more bytes, from the buffer's start. */
static void refill(Decomp *d) {
    u16 size = d->bufSize <= d->remaining ? d->bufSize : d->remaining;
    d->remaining -= size;
    g_decompRead(d->buf, size, d->fp);
    d->si = d->buf;
    d->cx = size;
}

/* The checks before a byte on the slow paths: false at the end of the file. */
static int have_byte(Decomp *d) {
    if (d->cx != 0)
        return 1;
    if (d->remaining == 0)
        return 0;
    refill(d);
    return 1;
}

/* The next n bytes of the buffer. LZSS with nothing to read takes its first flag byte from
 * the empty buffer, and CX, run below 0, then lets it read on into DGROUP. */
static const unsigned char *input(Decomp *d, u16 n) {
    const unsigned char *p = d->si;
    if (p + n > d->buf + d->bufSize)
        BAK_UNIMPLEMENTED("DECOMP.ASM: input past its buffer (the original reads on into DGROUP)");
    d->si += n;
    return p;
}

static unsigned char next(Decomp *d) {
    d->cx--;
    return *input(d, 1);
}

static unsigned char *out_at(Decomp *d, u16 off) {
    if (off >= d->extent)
        BAK_UNIMPLEMENTED("DECOMP.ASM: output past its block (the original writes on into DOS "
                          "memory)");
    return d->seg + off;
}

static void put(Decomp *d, unsigned char b) {
    *out_at(d, d->di) = b;
    d->di++;
}

/* REP ES:MOVSW, then ES:MOVSB for an odd count: earlier output to the output. */
static void copy_back(Decomp *d, u16 from, u16 count) {
    for (u16 words = count >> 1; words != 0; words--) {
        unsigned char lo = *out_at(d, from), hi = *out_at(d, (u16)(from + 1));
        put(d, lo);
        put(d, hi);
        from += 2;
    }
    if (count & 1)
        put(d, *out_at(d, from));
}

void decomp_lzss(unsigned char *dst, s16 remaining, void *inbuf, s16 buf_size, ResFile *fp) {
    Decomp d;
    u16 flags; /* the flag byte under an FFh sentinel: 8 bits are read when it runs out */
    u16 count, from;
    unsigned char b;

    start(&d, dst, remaining, inbuf, buf_size, fp);
    refill(&d);

flag_fast: /* LAB_003f */
    flags = 0xff00 | next(&d);
    if ((s16)d.cx <= 0x1e)
        goto bits_slow;
    if ((flags & 0xff) == 0xff) { /* eight literals: four MOVSW */
        for (int i = 0; i < 8; i++)
            put(&d, next(&d));
        goto flag_fast;
    }
bits_fast: /* LAB_0057 */
    if ((flags >> 8) == 0)
        goto flag_fast;
    b = flags & 1;
    flags >>= 1;
    if (b) {
        put(&d, *input(&d, 1));
        if (--d.cx != 0)
            goto bits_fast;
    }
    /* LAB_0062: a back-reference, the offset and the length - 5 */
    {
        const unsigned char *ref = input(&d, 3);
        from = (u16)(ref[0] | ref[1] << 8);
        count = (u16)(ref[2] + 5);
    }
    copy_back(&d, from, count);
    d.cx -= 3;
    goto bits_fast;

flag_slow: /* LAB_0085 */
    if (!have_byte(&d))
        return;
    flags = 0xff00 | next(&d);
    if ((s16)d.cx >= 0x1e)
        goto bits_fast;
bits_slow: /* LAB_009e */
    if ((flags >> 8) == 0)
        goto flag_slow;
    b = flags & 1;
    flags >>= 1;
    if (b) {
        if (!have_byte(&d))
            return;
        put(&d, next(&d));
        goto bits_slow;
    }
    if (!have_byte(&d))
        return;
    from = next(&d);
    if (!have_byte(&d))
        return;
    from |= (u16)(next(&d) << 8);
    if (!have_byte(&d))
        return;
    count = (u16)(next(&d) + 5);
    copy_back(&d, from, count);
    goto bits_slow;
}

void decomp_rle(unsigned char *dst, s16 refill_count, NearPtr buf_ptr, s16 buf_size, ResFile *fp) {
    Decomp d;
    unsigned char token, fill;
    u16 count;

    start(&d, dst, refill_count, (unsigned char *)buf_ptr, buf_size, fp);
    refill(&d);
    for (;;) {
        if (!have_byte(&d))
            return;
        token = next(&d);
        count = token & 0x7f;
        if (count == 0)
            return;
        if (token & 0x80) {
            if (!have_byte(&d))
                return;
            fill = next(&d);
            while (count-- != 0)
                put(&d, fill);
        } else if ((s16)d.cx >= (s16)count) {
            for (u16 i = 0; i < count; i++)
                put(&d, *input(&d, 1));
            d.cx -= count;
        } else {
            do {
                if (!have_byte(&d))
                    return;
                put(&d, next(&d));
            } while (--count != 0);
        }
    }
}
