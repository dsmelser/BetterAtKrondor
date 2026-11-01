/* DRIVERS/SX/ADL.ASM: the AdLib driver (port/drivers/adl.c) over the emulated OPL2. */
#include <stdlib.h>
#include <string.h>

#include "difftest.h"
#include "../../drivers/dude.h"
#include "bak_port.h"

static unsigned char *s_chunk;
static size_t s_size;
static uint8_t s_writes[1 << 17];
static size_t s_writeCount;

static void record(uint8_t reg, uint8_t value) {
    if (s_writeCount + 2 <= sizeof s_writes) {
        s_writes[s_writeCount++] = reg;
        s_writes[s_writeCount++] = value;
    }
}

/* adl.load <image>: the chunk as loaded */
static void cmd_load(DtIn *in, DtOut *out) {
    (void)out;
    size_t n = dt_remaining(in);
    free(s_chunk);
    if ((s_chunk = malloc(n)) == NULL)
        dt_fail("out of memory");
    memcpy(s_chunk, dt_bytes(in, n), n);
    s_size = n;
    bak_opl_set_sink(record);
}

/* adl.call u16 slot, u16 ax, u16 bx, u16 cx, then for slot 1 the bank -> u16 ax, u16 cx, then
 * the OPL writes as (register, value) pairs */
static void cmd_call(DtIn *in, DtOut *out) {
    DudeRegs r;
    uint16_t slot = dt_u16(in);
    r.ax = dt_u16(in);
    r.bx = dt_u16(in);
    r.cx = dt_u16(in);
    r.bank = dt_remaining(in) != 0 ? dt_bytes(in, dt_remaining(in)) : NULL;
    s_writeCount = 0;
    adl_dispatch(s_chunk, slot, &r);
    dt_put_u16(out, r.ax);
    dt_put_u16(out, r.cx);
    dt_put_bytes(out, s_writes, s_writeCount);
}

static void cmd_image(DtIn *in, DtOut *out) {
    (void)in;
    dt_put_bytes(out, s_chunk, s_size);
}

const DtEntry dt_adl_entries[] = {
    DT_COMMAND("adl.load", cmd_load),
    DT_COMMAND("adl.call", cmd_call),
    DT_COMMAND("adl.image", cmd_image),
    DT_END,
};
