/* DRIVERS/SX/ASB.ASM: the Sound Blaster's digital driver (port/drivers/asb.c) on the platform's
 * card (port/platform/sbdsp.c), every port access logged. Time does not pass here: a poll (the
 * probe's wait) ends the block under way, as the original's interrupt comes in its wait, and
 * asb.irq ends one between commands. */
#include <stdlib.h>
#include <string.h>

#include "difftest.h"
#include "../../drivers/dude.h"
#include "../../platform/platform.h"
#include "bak_port.h"

#define LOAD_SEG 0x1000 /* where the harness loads the original */
#define TRACE_MAX 65536

static unsigned char *s_chunk;
static size_t s_size;
static unsigned char *s_samples; /* the last block's buffer, at its segment */
static uint16_t s_samplesSeg;
static uint8_t s_trace[TRACE_MAX][4];
static uint32_t s_traceCount;

static void trace(uint16_t port, uint8_t value, int isWrite) {
    if (s_traceCount < TRACE_MAX) {
        uint8_t *t = s_trace[s_traceCount];
        t[0] = (uint8_t)port;
        t[1] = (uint8_t)(port >> 8);
        t[2] = value;
        t[3] = (uint8_t)isWrite;
    }
    s_traceCount++;
}

static void put_trace(DtOut *out) {
    dt_put_u32(out, s_traceCount);
    dt_put_bytes(out, s_trace, (s_traceCount < TRACE_MAX ? s_traceCount : TRACE_MAX) * 4);
    s_traceCount = 0;
}

static void poll_stand_in(void) {
    sbdsp_complete_now();
}

/* asb.load <image>: the chunk at segment 1000h; a card at 220h on IRQ 7 with DSP 2.01; no
 * MSCDEX; the vectors of IRQ 2-7 cleared; the 8259's mask B8h. */
static void cmd_load(DtIn *in, DtOut *out) {
    (void)out;
    size_t n = dt_remaining(in);
    if (s_chunk != NULL)
        bak_seg_unregister(LOAD_SEG);
    else if (bak_seg_extent(LOAD_SEG) != 0)
        dt_fail("segment %04Xh is taken", LOAD_SEG);
    free(s_chunk);
    if ((s_chunk = malloc(n)) == NULL)
        dt_fail("out of memory");
    memcpy(s_chunk, dt_bytes(in, n), n);
    s_size = n;
    bak_seg_register_at(LOAD_SEG, s_chunk, (uint32_t)n);
    sbdsp_configure(true, 0x220, 7, 0x0201);
    bak_cdrom_attach(-1, 0, 0, NULL);
    for (uint8_t v = 0x0A; v <= 0x0F; v++)
        bak_set_vector(v, NULL);
    machine_port_out8(0x21, 0xB8);
    bak_port_set_trace(trace);
    g_platformFreeRunningPoll = poll_stand_in;
    s_traceCount = 0;
}

/* asb.config u8 present, u16 base, u8 irq, u16 version, u8 8259 mask */
static void cmd_config(DtIn *in, DtOut *out) {
    (void)out;
    bool present = dt_u8(in) != 0;
    uint16_t base = dt_u16(in);
    int irq = dt_u8(in);
    uint16_t version = dt_u16(in);
    sbdsp_configure(present, base, irq, version);
    machine_port_out8(0x21, dt_u8(in));
}

/* A handler that was there before the driver's. */
static void before(void) {
}

/* asb.vector u8 n: vector n holds a handler before the driver */
static void cmd_vector(DtIn *in, DtOut *out) {
    (void)out;
    bak_set_vector(dt_u8(in), before);
}

/* asb.cd s8 drive: MSCDEX with a CD in that drive (0-based), or none (-1) */
static void cmd_cd(DtIn *in, DtOut *out) {
    static const unsigned char starts[1][3] = {{0, 2, 0}};
    (void)out;
    int drive = (signed char)dt_u8(in);
    bak_cdrom_attach(drive, 1, 1, drive >= 0 ? starts : NULL);
}

/* asb.call u16 cmd, u16 n, n bytes of arguments as the original reads them (for command 3,
 * then the samples' segment: the far pointer at +4 becomes a host pointer into a buffer at
 * that segment) -> u16 ax, the arguments after, the port accesses */
static void cmd_call(DtIn *in, DtOut *out) {
    uint16_t cmd = dt_u16(in);
    uint16_t n = dt_u16(in);
    unsigned char args[64];
    if (n > sizeof args)
        dt_fail("arguments too long");
    memcpy(args, dt_bytes(in, n), n);
    unsigned char *pointer = NULL;
    if (cmd == 3) {
        uint16_t off = (uint16_t)(args[4] | args[5] << 8);
        uint16_t seg = (uint16_t)(args[6] | args[7] << 8);
        uint16_t length = (uint16_t)(args[8] | args[9] << 8);
        uint32_t size = (uint32_t)off + length + 1;
        if (s_samples != NULL) {
            bak_seg_unregister(s_samplesSeg);
            free(s_samples);
        }
        if ((s_samples = calloc(1, size)) == NULL)
            dt_fail("out of memory");
        s_samplesSeg = seg;
        bak_seg_register_at(seg, s_samples, size);
        pointer = s_samples + off;
        memcpy(args + 4, &pointer, sizeof pointer);
    }
    s_traceCount = 0;
    uint16_t ax = asb_dispatch(s_chunk, cmd, args);
    if (cmd == 3) { /* the block as the original holds it, if the pointer is unchanged */
        unsigned char *after;
        memcpy(&after, args + 4, sizeof after);
        uint16_t off = (uint16_t)(after - s_samples), seg = s_samplesSeg;
        if (after != pointer)
            off = seg = 0xFFFF;
        args[4] = (uint8_t)off;
        args[5] = (uint8_t)(off >> 8);
        args[6] = (uint8_t)seg;
        args[7] = (uint8_t)(seg >> 8);
    }
    dt_put_u16(out, ax);
    dt_put_bytes(out, args, n);
    put_trace(out);
}

/* asb.irq -> u8 whether a block was under way, the port accesses of its interrupt */
static void cmd_irq(DtIn *in, DtOut *out) {
    (void)in;
    s_traceCount = 0;
    dt_put_u8(out, sbdsp_complete_now() ? 1 : 0);
    put_trace(out);
}

static void cmd_image(DtIn *in, DtOut *out) {
    (void)in;
    dt_put_bytes(out, s_chunk, s_size);
}

/* asb.state -> u8 the 8259's mask; for vectors 08h, 0Ah, 0Bh, 0Dh, 0Fh the original's offset
 * of the driver's handler there (0: none, -1: another); the card: u8 time constant, speaker,
 * active, halted, u32 length, u8 DMA mode, page, u16 address, count, u8 masked */
static void cmd_state(DtIn *in, DtOut *out) {
    static const uint8_t vectors[] = {0x08, 0x0A, 0x0B, 0x0D, 0x0F};
    SbdspState s;
    uint8_t mask;
    (void)in;
    machine_port_in8(0x21, &mask);
    dt_put_u8(out, mask);
    for (size_t i = 0; i < sizeof vectors; i++)
        dt_put_u32(out, (uint32_t)asb_isr_offset(bak_get_vector(vectors[i])));
    sbdsp_state(&s);
    dt_put_u8(out, s.timeConstant);
    dt_put_u8(out, s.speaker);
    dt_put_u8(out, s.active);
    dt_put_u8(out, s.halted);
    dt_put_u32(out, s.length);
    dt_put_u8(out, s.dmaMode);
    dt_put_u8(out, s.dmaPage);
    dt_put_u16(out, s.dmaAddress);
    dt_put_u16(out, s.dmaCount);
    dt_put_u8(out, s.dmaMasked);
}

const DtEntry dt_asb_entries[] = {
    DT_COMMAND("asb.load", cmd_load),
    DT_COMMAND("asb.config", cmd_config),
    DT_COMMAND("asb.cd", cmd_cd),
    DT_COMMAND("asb.call", cmd_call),
    DT_COMMAND("asb.vector", cmd_vector),
    DT_COMMAND("asb.irq", cmd_irq),
    DT_COMMAND("asb.image", cmd_image),
    DT_COMMAND("asb.state", cmd_state),
    DT_END,
};
