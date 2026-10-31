/* SYS/FARPTR.ASM: the far-pointer runtime (port/asm/farptr.c). Each command works on a
 * DOS block holding the given bytes and returns them afterwards. */
#include <string.h>

#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/SYS/FARPTR.H"
#include "bak_game_end.h"

static unsigned char *load_block(DtIn *in, uint32_t *size) {
    *size = dt_u32(in);
    unsigned char *buf = bak_dos_alloc(*size, 0);
    memcpy(buf, dt_bytes(in, *size), *size);
    return buf;
}

static void reply_block(DtOut *out, unsigned char *buf, uint32_t size) {
    dt_put_bytes(out, buf, size);
    bak_dos_free(buf);
}

/* farptr.memset <block> u32 at, u16 value, u32 count */
static void cmd_memset(DtIn *in, DtOut *out) {
    uint32_t size;
    unsigned char *buf = load_block(in, &size);
    uint32_t at = dt_u32(in);
    s16 value = (s16)dt_u16(in);
    uint32_t count = dt_u32(in);
    memset_far(buf + at, value, count);
    reply_block(out, buf, size);
}

/* farptr.memcpy <block> u32 dst, u32 src, u16 count */
static void cmd_memcpy(DtIn *in, DtOut *out) {
    uint32_t size;
    unsigned char *buf = load_block(in, &size);
    uint32_t dst = dt_u32(in), src = dt_u32(in);
    u16 count = dt_u16(in);
    fmemcpy_far((u32)(uintptr_t)(buf + dst), (u32)(uintptr_t)(buf + src), count);
    reply_block(out, buf, size);
}

/* farptr.memmove <block> u32 dst, u32 src, u32 count */
static void cmd_memmove(DtIn *in, DtOut *out) {
    uint32_t size;
    unsigned char *buf = load_block(in, &size);
    uint32_t dst = dt_u32(in), src = dt_u32(in), count = dt_u32(in);
    fmemmove(buf + dst, buf + src, count);
    reply_block(out, buf, size);
}

/* farptr.cmp u32 a, u32 b (offsets into one block) -> s16 */
static void cmd_cmp(DtIn *in, DtOut *out) {
    static unsigned char block[0x40000];
    uint32_t a = dt_u32(in), b = dt_u32(in);
    dt_put_u16(out, (u16)far_ptr_cmp(block + a, block + b));
}

const DtEntry dt_farptr_entries[] = {
    DT_COMMAND("farptr.memset", cmd_memset),
    DT_COMMAND("farptr.memcpy", cmd_memcpy),
    DT_COMMAND("farptr.memmove", cmd_memmove),
    DT_COMMAND("farptr.cmp", cmd_cmp),
    DT_END,
};
