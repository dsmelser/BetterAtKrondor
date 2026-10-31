/*
 * RNDVTBL.ASM in C: the renderer vtable. Every slot starts at gfx_retf_stub, the
 * no-op the original links in, until video_init copies a driver's table over it.
 * The original's labels on single slots (g_pfn_blit_glyph_row, ...) are macros for
 * the slots natively (RNDVTBL.H).
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/RASTER/VGARESET.H"
#include "bak_game_end.h"

#define STUB ((void *)gfx_retf_stub)
#define STUB10 STUB, STUB, STUB, STUB, STUB, STUB, STUB, STUB, STUB, STUB

RendererVtable g_renderer_vtable = {STUB10, STUB10, STUB10, STUB10, STUB10};
