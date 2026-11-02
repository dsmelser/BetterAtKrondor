/* GFX/RASTER/GOURAUD.ASM: the slanted quad rasterizer (port/asm/gouraud.c), run the
 * original's way, and its parts. The port's way, with each column's skip count its own, is
 * driven through polyrast.quad (t_polyrast.c), which hands it any quad without vertical
 * sides. */
#include <stdint.h>

#include "difftest.h"
#include "bak_video.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GFX/RASTER/GOURAUD.H"
#include "SRC/STREAM/RESLOAD/IMGLOAD.H"
#include "bak_game_end.h"

int gouraud_blit_quad_as_original(ImageRecord *texture, s16 *xs, s16 *ys);

/* gouraud.quad u16 record[5] (segment, offset, flags, width, height), s16 xs[4], ys[4]
 * -> u8 map mask, u8 whether a texture column went past the table */
static void cmd_quad(DtIn *in, DtOut *out) {
    ImageRecord rec;
    s16 xs[4], ys[4];
    u16 seg = dt_u16(in);
    imgload_set_far(&rec, seg, dt_u16(in));
    rec.wUnk4 = dt_u16(in);
    rec.nWidth = (s16)dt_u16(in);
    rec.nHeight = (s16)dt_u16(in);
    for (int i = 0; i < 4; i++)
        xs[i] = (s16)dt_u16(in);
    for (int i = 0; i < 4; i++)
        ys[i] = (s16)dt_u16(in);
    int past = gouraud_blit_quad_as_original(&rec, xs, ys);
    dt_put_u8(out, bak_vga_sequencer(2));
    dt_put_u8(out, (uint8_t)past);
}

/* gouraud.step s32 acc, s32 step, s16 count -> s32 acc, s32 step */
static void cmd_step(DtIn *in, DtOut *out) {
    s32 edge[2];
    edge[0] = (s32)dt_u32(in);
    edge[1] = (s32)dt_u32(in);
    edge_step_init(edge, (s16)dt_u16(in));
    dt_put_u32(out, (uint32_t)edge[0]);
    dt_put_u32(out, (uint32_t)edge[1]);
}

/* gouraud.slopes s16 yA, xA, y0, x0, yB, xB -> s16 */
static void cmd_slopes(DtIn *in, DtOut *out) {
    s16 a[6];
    for (int i = 0; i < 6; i++)
        a[i] = (s16)dt_u16(in);
    dt_put_u16(out, (u16)compare_edge_slopes(a[0], a[1], a[2], a[3], a[4], a[5]));
}

/* gouraud.dispatch s16 v0, v1, width, height -> s16 column0, column1, row0, row1 */
static void cmd_dispatch(DtIn *in, DtOut *out) {
    ImageRecord rec = {0};
    s16 r[4];
    s16 v0 = (s16)dt_u16(in), v1 = (s16)dt_u16(in);
    rec.nWidth = (s16)dt_u16(in);
    rec.nHeight = (s16)dt_u16(in);
    gfx_gouraud_span_dispatch(v0, v1, &rec, &r[0], &r[1], &r[2], &r[3]);
    for (int i = 0; i < 4; i++)
        dt_put_u16(out, (u16)r[i]);
}

const DtEntry dt_gouraud_entries[] = {
    DT_COMMAND("gouraud.quad", cmd_quad),
    DT_COMMAND("gouraud.step", cmd_step),
    DT_COMMAND("gouraud.slopes", cmd_slopes),
    DT_COMMAND("gouraud.dispatch", cmd_dispatch),
    DT_END,
};
