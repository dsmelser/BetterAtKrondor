/*
 * GFXCTX.ASM in C: the engine's graphics context, the block a video driver uses as
 * its data segment, with the original's initial values (full-screen clip rectangle,
 * fill enabled, 320x200, adapter auto-detection on). See GFXCTX.H for the layout.
 */
#include <stddef.h>

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "bak_game_end.h"

GfxContextBlock g_gfxContextBlock = {
    .ctx =
        {
            .clip = {.xmin = 0, .xmax = 0x13f, .ymin = 0, .ymax = 0xc7},
            .bGfx_fill_enabled = 1,
        },
    .wScreen_width = 0x140,
    .wScreen_height = 0xc8,
    .render = {.bVideoAutoDetectEnabled = 1},
};

_Static_assert(sizeof(GraphicsContext) == 0x6ea, "GraphicsContext layout");
_Static_assert(offsetof(GfxContextBlock, pRow_offset_lut) == 0x6f2, "row LUT offset");
_Static_assert(offsetof(GfxContextBlock, render) == 0x882, "render tail offset");
_Static_assert(sizeof(GfxContextBlock) == 0xab6, "graphics-context block size");
