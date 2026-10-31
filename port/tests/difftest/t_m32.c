/* DRIVERS/SX/M32.ASM: the MT-32 driver (port/drivers/m32.c) over the emulated MPU-401. */
#include <stdlib.h>
#include <string.h>

#include "difftest.h"
#include "../../drivers/dude.h"
#include "bak_port.h"

static unsigned char *s_chunk;
static size_t s_size;
static uint8_t s_midi[1 << 16];
static size_t s_midiCount;

static void record(uint8_t byte) {
    if (s_midiCount < sizeof s_midi)
        s_midi[s_midiCount++] = byte;
}

/* m32.load <image>: the chunk as loaded, a freshly reset MPU-401 */
static void cmd_load(DtIn *in, DtOut *out) {
    (void)out;
    size_t n = dt_remaining(in);
    free(s_chunk);
    if ((s_chunk = malloc(n)) == NULL)
        dt_fail("out of memory");
    memcpy(s_chunk, dt_bytes(in, n), n);
    s_size = n;
    bak_mpu401_reset();
    bak_midi_set_sink(record);
}

/* m32.call u16 slot, u16 ax, u16 cx, then for slot 1 the bank -> u16 ax, u16 cx, MIDI */
static void cmd_call(DtIn *in, DtOut *out) {
    DudeRegs r;
    uint16_t slot = dt_u16(in);
    r.ax = dt_u16(in);
    r.cx = dt_u16(in);
    r.bank = dt_remaining(in) != 0 ? dt_bytes(in, dt_remaining(in)) : NULL;
    s_midiCount = 0;
    m32_dispatch(s_chunk, slot, &r);
    dt_put_u16(out, r.ax);
    dt_put_u16(out, r.cx);
    dt_put_bytes(out, s_midi, s_midiCount);
}

static void cmd_image(DtIn *in, DtOut *out) {
    (void)in;
    dt_put_bytes(out, s_chunk, s_size);
}

const DtEntry dt_m32_entries[] = {
    DT_COMMAND("m32.load", cmd_load),
    DT_COMMAND("m32.call", cmd_call),
    DT_COMMAND("m32.image", cmd_image),
    DT_END,
};
