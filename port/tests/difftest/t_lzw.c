/* STREAM/CODEC/LZWDEC.ASM (port/asm/lzwdec.c), driven through the stream API: a memory
 * stream set up as stream_open_from_memory does, without its allocations, and read in
 * chunks with stream_read. */
#include <string.h>

#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/STREAM/CODEC/CODEC.H"
#include "SRC/STREAM/CODEC/LZHINIT.H"
#include "SRC/STREAM/CODEC/STREAM.H"
#include "bak_game_end.h"

#define SCRATCH_SIZE 0x3c8c /* the game's shared codec scratch (BOOT.C) */

static StreamDesc s_desc;
static unsigned char s_ring[0x80];
static unsigned char *s_scratch;
static unsigned char *s_src;
static unsigned char *s_dst;

/* lzw.open <stream: codec byte, u32 size, payload> */
static void cmd_open(DtIn *in, DtOut *out) {
    (void)out;
    uint32_t size = (uint32_t)dt_remaining(in);
    if (s_src != NULL)
        bak_dos_free(s_src);
    if (s_scratch == NULL)
        s_scratch = bak_dos_alloc(SCRATCH_SIZE, 0);
    s_src = bak_dos_alloc(size, 0);
    memcpy(s_src, dt_bytes(in, size), size);
    memset(&s_desc, 0, sizeof s_desc);
    memset(s_ring, 0, sizeof s_ring);
    s_desc.pBuf = s_ring;
    s_desc.pScratch = s_scratch;
    s_desc.src.pBufBase = s_src;
    s_desc.dwInPos = 5;
    s_desc.dwInSize = size;
    memcpy(&s_desc.dwUncompressedSize, s_src + 1, 4);
    s_desc.bFlags = (unsigned char)(s_src[0] | 0x40);
    g_apStreamSlots[0] = &s_desc;
    stream_select(0);
    lzh_decoder_init();
}

/* lzw.read u16 count -> u16 delivered, the destination buffer (count bytes) */
static void cmd_read(DtIn *in, DtOut *out) {
    u16 count = dt_u16(in);
    if (s_dst != NULL)
        bak_dos_free(s_dst);
    s_dst = bak_dos_alloc(count ? count : 1, 1);
    dt_put_u16(out, stream_read(0, s_dst, count));
    dt_put_bytes(out, s_dst, count);
}

const DtEntry dt_lzw_entries[] = {
    DT_COMMAND("lzw.open", cmd_open),
    DT_COMMAND("lzw.read", cmd_read),
    DT_END,
};
