/* PRESENT.ASM in C: a tail jump through the renderer's page-copy slot (4). */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/RASTER/PRESENT.H"
#include "bak_game_end.h"

void gfx_present_dispatch(s16 x, s16 y, s16 w, s16 h) {
    (*(PresentFn)g_renderer_vtable.pfn_present)(x, y, w, h);
}
