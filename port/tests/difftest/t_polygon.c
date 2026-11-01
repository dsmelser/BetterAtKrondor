/* GFX/RASTER/POLYGON.ASM and POLYCLIP.ASM: the polygon rasterizer and its clipper
 * (port/asm/polygon.c, polyclip.c). The driver's line and span slots are replaced by
 * probes that log what reaches them; the span buffer is a block of the test's own, the
 * size of a segment and the paragraph before it. */
#include <stdlib.h>
#include <string.h>

#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/RASTER/POLYCLIP.H"
#include "SRC/GFX/RASTER/POLYGON.H"
#include "bak_game_end.h"

#define SPAN_BLOCK 0x10010u /* the paragraph before the span segment, then all of it */
#define TABLE_ROWS_LOGGED 1024u

static DtOut *s_log;
static unsigned char *s_spanBlock;

static u16 span_seg(void) {
    return (u16)(FP_SEG(s_spanBlock) + 1);
}

/* Slot 2: 2, then the page and the endpoints. */
static void probe_line(u16 seg, u16 x0, u16 y0, u16 x1, u16 y1) {
    dt_put_u16(s_log, 2);
    dt_put_u16(s_log, seg);
    dt_put_u16(s_log, x0);
    dt_put_u16(s_log, y0);
    dt_put_u16(s_log, x1);
    dt_put_u16(s_log, y1);
}

/* Slot 27: 27, the table's offset in the span block, then the table (the first row, the
 * rows, x0 and x1 of each), as far as the block goes. */
static void probe_spans(const unsigned char *table) {
    uint32_t at = (uint32_t)(table - s_spanBlock);
    if (at > SPAN_BLOCK - 4)
        dt_fail("span table outside the span block (at %#x)", (unsigned)at);
    u16 rows = (u16)(table[2] | table[3] << 8);
    uint32_t size = 4u + 4u * (rows < TABLE_ROWS_LOGGED ? rows : TABLE_ROWS_LOGGED);
    if (size > SPAN_BLOCK - at)
        size = SPAN_BLOCK - at;
    dt_put_u16(s_log, 27);
    dt_put_u32(s_log, at);
    dt_put_bytes(s_log, table, size);
}

typedef struct {
    void *line, *spans;
} Saved;

static Saved install(DtOut *out) {
    Saved s = {g_renderer_vtable.pfn_draw_line, g_renderer_vtable.pfn_fill_spans_dithered_modey};
    if (s_spanBlock == NULL)
        s_spanBlock = bak_dos_alloc(SPAN_BLOCK, 1);
    s_log = out;
    g_renderer_vtable.pfn_draw_line = (void *)probe_line;
    g_renderer_vtable.pfn_fill_spans_dithered_modey = (void *)probe_spans;
    g_graphics_context_render.wSpanTableBufSeg = span_seg();
    return s;
}

static void uninstall(Saved s) {
    g_renderer_vtable.pfn_draw_line = s.line;
    g_renderer_vtable.pfn_fill_spans_dithered_modey = s.spans;
    s_log = NULL;
}

/* A segment value as the test compares it: 0, the span segment (1) or another (2). */
static u16 seg_kind(u16 seg) {
    return seg == 0 ? 0 : seg == span_seg() ? 1 : 2;
}

/* polygon.span [SPAN_BLOCK bytes] -> the span block (after setting it from the payload) */
static void cmd_span(DtIn *in, DtOut *out) {
    if (s_spanBlock == NULL)
        s_spanBlock = bak_dos_alloc(SPAN_BLOCK, 1);
    if (dt_remaining(in) != 0)
        memcpy(s_spanBlock, dt_bytes(in, SPAN_BLOCK), SPAN_BLOCK);
    dt_put_bytes(out, s_spanBlock, SPAN_BLOCK);
}

/*
 * polygon.draw s16 count, s16 xs[count], s16 ys[count] (none for a negative count)
 *   -> the result (as seg_kind), the module's state words in the original's order (the
 *      returned segment as seg_kind), then the probe log
 */
static void cmd_draw(DtIn *in, DtOut *out) {
    s16 xs[64], ys[64];
    s16 count = (s16)dt_u16(in);
    int n = count < 0 ? 0 : count;
    if (n > 64)
        dt_fail("too many vertices");
    for (int i = 0; i < n; i++)
        xs[i] = (s16)dt_u16(in);
    for (int i = 0; i < n; i++)
        ys[i] = (s16)dt_u16(in);
    DtOut log = {0};
    Saved s = install(&log);
    s16 result = draw_polygon(count, xs, ys);
    uninstall(s);
    dt_put_u16(out, seg_kind((u16)result));
    dt_put_u16(out, g_wDrawPolyTopVertexOff);
    dt_put_u16(out, g_wDrawPolyBottomVertexOff);
    dt_put_u16(out, g_wDrawPolyChain1VertexCount);
    dt_put_u16(out, g_wDrawPolyChain2VertexCount);
    dt_put_u16(out, g_wDrawPolyChainRemaining);
    dt_put_u16(out, g_wDrawPolyEdgeWalkOff);
    dt_put_u16(out, g_wRasterEdgeColumnOff);
    dt_put_u16(out, (u16)g_nDrawPolyLastVertexX);
    dt_put_u16(out, (u16)g_nDrawPolyLastVertexY);
    dt_put_u16(out, seg_kind(g_wDrawPolySpanBufRetSeg));
    dt_put_u16(out, g_wDrawPolyOutlineVertexCount);
    dt_put_u16(out, g_wDrawPolyClipReplayByteCount);
    dt_put_u8(out, g_bRasterSpanDiStride);
    dt_put_u8(out, g_bDrawPolyReplayPending);
    if (log.size != 0)
        dt_put_bytes(out, log.data, log.size);
    free(log.data);
}

/* polygon.clip: polygon_clip_viewport on the context as it stands */
static void cmd_clip(DtIn *in, DtOut *out) {
    (void)in;
    (void)out;
    polygon_clip_viewport();
}

const DtEntry dt_polygon_entries[] = {
    DT_COMMAND("polygon.span", cmd_span),
    DT_COMMAND("polygon.draw", cmd_draw),
    DT_COMMAND("polygon.clip", cmd_clip),
    DT_END,
};
