/*
 * DRAWRECT.ASM in C: draw_rect_filled fills a rectangle through the driver's span filler
 * (slot 27) when fill is on, then outlines it with draw_line (right, bottom, left and top
 * edges) unless the fill and outline colours agree.
 *
 * The fill is clipped here, the outline by draw_line: the outline keeps the rectangle as
 * given. The spans go into the span table at g_graphics_context_render.wSpanTableBufSeg:
 * the first row, the row count, then x0 and x1 of each row.
 */
#include "../drivers/vga.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/RASTER/DRAWLINE.H"
#include "SRC/GFX/SPRITE/RECTSPR.H"
#include "bak_game_end.h"

static void put16(unsigned char *p, s16 v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)((u16)v >> 8);
}

void draw_rect_filled(s16 x, s16 y, s16 w, s16 h) {
    s16 x2 = (s16)(w + x - 1), y2 = (s16)(h + y - 1);
    if (g_graphics_context.bGfx_fill_enabled) {
        s16 fx = x, fy = y, d;
        if (g_graphics_context.bClip_enabled) {
            const ClipRect *c = &g_graphics_context.clip;
            if ((d = (s16)(fx - c->xmin)) < 0) {
                fx = (s16)(fx - d);
                w = (s16)(w + d);
            }
            if ((d = (s16)(fy - c->ymin)) < 0) {
                fy = (s16)(fy - d);
                h = (s16)(h + d);
            }
            if ((d = (s16)(c->xmax - x2)) < 0)
                w = (s16)(w + d);
            if ((d = (s16)(c->ymax - y2)) < 0)
                h = (s16)(h + d);
        }
        if (w > 0 && h > 0) {
            u16 seg = g_graphics_context_render.wSpanTableBufSeg;
            if (4u + 4u * (u16)h > bak_seg_extent(seg))
                BAK_UNIMPLEMENTED("draw_rect_filled: more rows than the span table holds (the "
                                  "original writes past it)");
            unsigned char *spans = MK_FP(seg, 0);
            unsigned char *p = spans;
            put16(p, fy);
            put16(p + 2, h);
            p += 4;
            for (s16 n = h; n != 0; n--, p += 4) {
                put16(p, fx);
                put16(p + 2, (s16)(fx + w - 1));
            }
            ((FillSpansFn *)g_renderer_vtable.pfn_fill_spans_dithered_modey)(spans);
        }
    }
    if (g_graphics_context.bGfx_fill_enabled &&
        g_graphics_context.bGfx_fill_color == g_graphics_context.bGfx_outline_color)
        return;
    draw_line(x2, y, x2, y2);
    draw_line(x, y2, x2, y2);
    draw_line(x, y, x, y2);
    draw_line(x, y, x2, y);
}
