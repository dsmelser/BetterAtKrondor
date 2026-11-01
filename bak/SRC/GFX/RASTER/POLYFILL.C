#include "SRC/GEN/GFXCTX.H"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/FONT/FONT.H"
#include "structs.h"
#include "SRC/GFX/RASTER/POLYFILL.H"
#include "SRC/GFX/RASTER/POLYGON.H"

/* The 256-byte table slot 39 translates the spans' pixels through. The original keeps its DGROUP
   offset in g_graphics_context.wTextureIdx, which natively cannot hold it. */
const unsigned char *g_pTexturedSpanLut;

void draw_polygon_textured(s16 vertex_count, s16 *verts_x, s16 *verts_y, const unsigned char *lut) {
    Slot27Fn far *saved;
    Slot27Fn far *src;

    saved = g_renderer_vtable.pfn_fill_spans_dithered_modey;
    g_renderer_vtable.pfn_fill_spans_dithered_modey =
        (Slot27Fn far *)g_renderer_vtable.pfn_fill_world_textured_span_modey;
    g_pTexturedSpanLut = lut;
    draw_polygon(vertex_count, verts_x, verts_y);
    g_renderer_vtable.pfn_fill_spans_dithered_modey = saved;
    return;
}
