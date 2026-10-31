/* GFX/RASTER/VGABLIT.ASM: the texture span copiers (port/asm/vgablit.c). Each command
 * answers with the map mask and the graphics controller's mode register it leaves. */
#include <string.h>

#include "difftest.h"
#include "bak_video.h"
#include "../../platform/vgaregs.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "vgablit.h"
#include "SRC/GFX/RASTER/VGABLIT.H"
#include "bak_game_end.h"

static void registers(DtOut *out) {
    dt_put_u8(out, bak_vga_sequencer(2));
    dt_put_u8(out, g_vga.gc[5]);
}

/* vgablit.runs u8 tiled <span table> */
static void cmd_runs(DtIn *in, DtOut *out) {
    uint8_t tiled = dt_u8(in);
    size_t n = dt_remaining(in);
    unsigned char *spans = bak_dos_alloc((uint32_t)n, 0);
    memcpy(spans, dt_bytes(in, n), n);
    if (tiled)
        vgablit_planar_runs(spans);
    else
        vgablit_planar_runs_untiled(spans);
    bak_dos_free(spans);
    registers(out);
}

/* vgablit.line seg x0 y0 x1 y1 */
static void cmd_line(DtIn *in, DtOut *out) {
    u16 seg = dt_u16(in), x0 = dt_u16(in), y0 = dt_u16(in), x1 = dt_u16(in), y1 = dt_u16(in);
    vgablit_span_emit_textured(seg, x0, y0, x1, y1);
    registers(out);
}

const DtEntry dt_vgablit_entries[] = {
    DT_COMMAND("vgablit.runs", cmd_runs), DT_COMMAND("vgablit.line", cmd_line),
    DT_GLOBAL(g_vgablit_si_base),         DT_GLOBAL(g_vgablit_si_stride),
    DT_GLOBAL(g_vgablit_row_count),       DT_GLOBAL(g_vgablit_row_sub),
    DT_GLOBAL(g_vgablit_si_mask),         DT_END,
};
