/* GFX/RASTER/DRAWLINE.ASM and GFX/SPRITE/DRAWRECT.ASM: the line clipper and the filled
 * rectangle (port/asm/drawline.c, drawrect.c). The driver's line and span slots are
 * replaced by probes that log what reaches them. */
#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/RASTER/DRAWLINE.H"
#include "SRC/GFX/SPRITE/RECTSPR.H"
#include "bak_game_end.h"

#define SPAN_TABLE_SIZE 0x1000

static DtOut *s_log;
static unsigned char *s_spanTable;

/* Slot 2: 2, then the page and the endpoints (the original's ES BX CX DX SI). */
static void probe_line(u16 seg, u16 x0, u16 y0, u16 x1, u16 y1) {
    dt_put_u16(s_log, 2);
    dt_put_u16(s_log, seg);
    dt_put_u16(s_log, x0);
    dt_put_u16(s_log, y0);
    dt_put_u16(s_log, x1);
    dt_put_u16(s_log, y1);
}

/* Slot 27: 27, then the span table: the first row, the rows, x0 and x1 of each. */
static void probe_spans(const unsigned char *spans) {
    u16 rows = (u16)(spans[2] | spans[3] << 8);
    dt_put_u16(s_log, 27);
    dt_put_bytes(s_log, spans, 4u + 4u * rows);
}

typedef struct {
    void *line, *spans;
    u16 spanSeg;
} Saved;

/* Probes in the slots and a span table of our own, for the length of one command. */
static Saved install(DtOut *out) {
    Saved s = {g_renderer_vtable.pfn_draw_line, g_renderer_vtable.pfn_fill_spans_dithered_modey,
               g_graphics_context_render.wSpanTableBufSeg};
    if (s_spanTable == NULL)
        s_spanTable = bak_dos_alloc(SPAN_TABLE_SIZE, 1);
    s_log = out;
    g_renderer_vtable.pfn_draw_line = (void *)probe_line;
    g_renderer_vtable.pfn_fill_spans_dithered_modey = (void *)probe_spans;
    g_graphics_context_render.wSpanTableBufSeg = FP_SEG(s_spanTable);
    return s;
}

static void uninstall(Saved s) {
    g_renderer_vtable.pfn_draw_line = s.line;
    g_renderer_vtable.pfn_fill_spans_dithered_modey = s.spans;
    g_graphics_context_render.wSpanTableBufSeg = s.spanSeg;
    s_log = NULL;
}

/* drawline.run x1 y1 x2 y2 -> the probe log */
static void cmd_line(DtIn *in, DtOut *out) {
    s16 x1 = (s16)dt_u16(in), y1 = (s16)dt_u16(in), x2 = (s16)dt_u16(in), y2 = (s16)dt_u16(in);
    Saved s = install(out);
    draw_line(x1, y1, x2, y2);
    uninstall(s);
}

/* drawrect.run x y w h -> the probe log */
static void cmd_rect(DtIn *in, DtOut *out) {
    s16 x = (s16)dt_u16(in), y = (s16)dt_u16(in), w = (s16)dt_u16(in), h = (s16)dt_u16(in);
    Saved s = install(out);
    draw_rect_filled(x, y, w, h);
    uninstall(s);
}

const DtEntry dt_drawline_entries[] = {
    DT_COMMAND("drawline.run", cmd_line),
    DT_COMMAND("drawrect.run", cmd_rect),
    DT_END,
};
