/* GFX/SPRITE/DECOMP.ASM: the BMX codecs (port/asm/decomp.c), reading the test's bytes
 * through the refill hook and writing into a 64 KB DOS block of their own. */
#include <stdlib.h>
#include <string.h>

#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GFX/SPRITE/DECOMP.H"
#include "bak_game_end.h"

#define SEGMENT 0x10000

extern void (*g_decompRead)(unsigned char *buf, u16 size, ResFile *fp);

static const uint8_t *s_src;
static uint32_t s_srcLen, s_srcPos;
static unsigned char *s_segment;

/* res_fread(buf, size, 1, fp) over the source: what is left of it, up to size. */
static void source_read(unsigned char *buf, u16 size, ResFile *fp) {
    (void)fp;
    uint32_t n = s_srcLen - s_srcPos;
    if (n > size)
        n = size;
    memcpy(buf, s_src + s_srcPos, n);
    s_srcPos += n;
}

/* decomp.run <u8 codec: 1 LZSS, 2 RLE> <u16 output offset> <u16 bytes to read>
 *            <u16 buffer size> <u32 source size> <source> <the buffer> <the 64 KB segment>
 *   -> u32 bytes read from the source, the buffer, the segment */
static void cmd_run(DtIn *in, DtOut *out) {
    uint8_t codec = dt_u8(in);
    uint16_t off = dt_u16(in);
    uint16_t remaining = dt_u16(in);
    uint16_t bufSize = dt_u16(in);
    s_srcLen = dt_u32(in);
    s_src = dt_bytes(in, s_srcLen);
    s_srcPos = 0;
    unsigned char *buf = malloc(bufSize ? bufSize : 1);
    memcpy(buf, dt_bytes(in, bufSize), bufSize);
    if (s_segment == NULL)
        s_segment = bak_dos_alloc(SEGMENT, 1);
    memcpy(s_segment, dt_bytes(in, SEGMENT), SEGMENT);

    void (*saved)(unsigned char *, u16, ResFile *) = g_decompRead;
    g_decompRead = source_read;
    if (codec == 1)
        decomp_lzss(s_segment + off, (s16)remaining, buf, (s16)bufSize, NULL);
    else
        decomp_rle(s_segment + off, (s16)remaining, (NearPtr)buf, (s16)bufSize, NULL);
    g_decompRead = saved;

    dt_put_u32(out, s_srcPos);
    dt_put_bytes(out, buf, bufSize);
    dt_put_bytes(out, s_segment, SEGMENT);
    free(buf);
}

const DtEntry dt_decomp_entries[] = {
    DT_COMMAND("decomp.run", cmd_run),
    DT_END,
};
