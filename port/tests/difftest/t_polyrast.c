/* GFX/RASTER/POLYRAST.ASM: the scaled sprite blit, the textured quad blit and the edge
 * stepper (port/asm/polyrast.c). */
#include <stdint.h>
#include <string.h>

#include "difftest.h"
#include "bak_video.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "gfx169d.h"
#include "SRC/GAME/GMAIN.H"
#include "SRC/GAME/MAINDATA.H"
#include "SRC/GFX/RASTER/POLYRAST.H"
#include "SRC/STREAM/RESLOAD/IMGLOAD.H"
#include "bak_game_end.h"

/* polyrast.block u32 size -> u16 seg, u16 off, u32 address of a zeroed DOS block */
static void cmd_block(DtIn *in, DtOut *out) {
    void *p = bak_dos_alloc(dt_u32(in), 1);
    if (p == NULL)
        dt_fail("out of DOS memory");
    dt_put_u16(out, FP_SEG(p));
    dt_put_u16(out, FP_OFF(p));
    dt_put_u32(out, (uint32_t)(uintptr_t)p);
}

/* polyrast.scratch u32 address: g_pMainScratchBuf */
static void cmd_scratch(DtIn *in, DtOut *out) {
    (void)out;
    g_pMainScratchBuf = (unsigned char *)(uintptr_t)dt_u32(in);
}

/* polyrast.write u32 address, bytes / polyrast.read u32 address, u32 size -> bytes */
static void cmd_write(DtIn *in, DtOut *out) {
    (void)out;
    unsigned char *p = (unsigned char *)(uintptr_t)dt_u32(in);
    size_t n = dt_remaining(in);
    memcpy(p, dt_bytes(in, n), n);
}

static void cmd_read(DtIn *in, DtOut *out) {
    const unsigned char *p = (const unsigned char *)(uintptr_t)dt_u32(in);
    dt_put_bytes(out, p, dt_u32(in));
}

/* An ImageRecord as the original's ten bytes give it: segment, offset, flags, width, height. */
static void read_record(DtIn *in, ImageRecord *rec) {
    u16 seg = dt_u16(in);
    imgload_set_far(rec, seg, dt_u16(in));
    rec->wUnk4 = dt_u16(in);
    rec->nWidth = (s16)dt_u16(in);
    rec->nHeight = (s16)dt_u16(in);
}

/* polyrast.blit ImageRecord (10 bytes), s16 x, y, u16 flags, s16 w, h, colour -> u8 map mask */
static void cmd_blit(DtIn *in, DtOut *out) {
    ImageRecord rec;
    read_record(in, &rec);
    s16 x = (s16)dt_u16(in), y = (s16)dt_u16(in);
    u16 flags = dt_u16(in);
    s16 w = (s16)dt_u16(in), h = (s16)dt_u16(in), colour = (s16)dt_u16(in);
    polyrast_spr_scaled_blit_planar(&rec, x, y, flags, w, h, colour);
    dt_put_u8(out, bak_vga_sequencer(2));
}

/* polyrast.quad u16 record[5], s16 xs[4], ys[4] -> u8 map mask, s16 xs[4], ys[4] */
static void cmd_quad(DtIn *in, DtOut *out) {
    ImageRecord rec;
    s16 xs[4], ys[4];
    read_record(in, &rec);
    for (int i = 0; i < 4; i++)
        xs[i] = (s16)dt_u16(in);
    for (int i = 0; i < 4; i++)
        ys[i] = (s16)dt_u16(in);
    polyrast_gouraud_blit_clipped(&rec, xs, ys);
    dt_put_u8(out, bak_vga_sequencer(2));
    for (int i = 0; i < 4; i++)
        dt_put_u16(out, (u16)xs[i]);
    for (int i = 0; i < 4; i++)
        dt_put_u16(out, (u16)ys[i]);
}

/* polyrast.out u16 port, u8 value: an OUT to the emulated hardware */
static void cmd_out(DtIn *in, DtOut *out) {
    (void)out;
    uint16_t port = dt_u16(in);
    bak_port_out8(port, dt_u8(in));
}

/* polyrast.step s32 acc, s32 step, u16 steps -> s32 acc, s32 step */
static void cmd_step(DtIn *in, DtOut *out) {
    s32 edge[2];
    edge[0] = (s32)dt_u32(in);
    edge[1] = (s32)dt_u32(in);
    polyrast_edge_init_step(edge, dt_u16(in));
    dt_put_u32(out, (uint32_t)edge[0]);
    dt_put_u32(out, (uint32_t)edge[1]);
}

const DtEntry dt_polyrast_entries[] = {
    DT_COMMAND("polyrast.block", cmd_block),
    DT_COMMAND("polyrast.scratch", cmd_scratch),
    DT_COMMAND("polyrast.write", cmd_write),
    DT_COMMAND("polyrast.read", cmd_read),
    DT_COMMAND("polyrast.blit", cmd_blit),
    DT_COMMAND("polyrast.quad", cmd_quad),
    DT_COMMAND("polyrast.out", cmd_out),
    DT_COMMAND("polyrast.step", cmd_step),
    DT_GLOBAL(g_polyRasterState),
    DT_GLOBAL(g_polyrastTables),
    DT_END,
};
