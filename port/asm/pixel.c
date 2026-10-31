/*
 * PIXEL.ASM in C: getpixel and putpixel reject a pixel outside the clip rectangle
 * when clipping is on (getpixel then reads 0xffff), and otherwise hand over to the
 * renderer's pixel slots (21, 22).
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/RASTER/PIXEL.H"
#include "bak_game_end.h"

static int clipped(s16 x, s16 y) {
    const ClipRect *c = &g_graphics_context.clip;
    return g_graphics_context.bClip_enabled != 0 &&
           (x < c->xmin || x > c->xmax || y < c->ymin || y > c->ymax);
}

u16 getpixel(s16 x, s16 y) {
    if (clipped(x, y))
        return 0xffff;
    return (*(GetpixelFn)g_renderer_vtable.pfn_getpixel)(x, y);
}

void putpixel(s16 x, s16 y, s16 color) {
    if (clipped(x, y))
        return;
    (*(PutpixelFn)g_renderer_vtable.pfn_putpixel)(x, y, color);
}
