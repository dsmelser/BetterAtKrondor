#ifndef BAK_GFXCTX_H
#define BAK_GFXCTX_H

#include "structs.h"

/*
 * GFXCTX.ASM lays these out as one run: the context, then the screen size, the
 * row-offset LUT and the render tail, the other names being labels inside it. The
 * driver addresses all of it by offset and the LUT is read past its end, so natively
 * it is one packed object as well, and the names are its fields.
 */
typedef struct GfxContextBlock {
    GraphicsContext ctx;                 /* +0x000 */
    unsigned short wScreen_width;        /* +0x6ea */
    unsigned short wScreen_height;       /* +0x6ec */
    unsigned char pReserved_0x6ee[4];    /* +0x6ee */
    unsigned short pRow_offset_lut[200]; /* +0x6f2 */
    GraphicsContextRender render;        /* +0x882 */
} GfxContextBlock;

extern GfxContextBlock g_gfxContextBlock;
#define g_graphics_context (g_gfxContextBlock.ctx)
#define g_wScreen_width (g_gfxContextBlock.wScreen_width)
#define g_wScreen_height (g_gfxContextBlock.wScreen_height)
#define g_pRow_offset_lut (g_gfxContextBlock.pRow_offset_lut)
#define g_graphics_context_render (g_gfxContextBlock.render)
#define g_wGfxBlitDstPage (g_gfxContextBlock.ctx.wGfxBlitDstPage)

#endif
