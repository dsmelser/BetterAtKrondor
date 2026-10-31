/* CGAPASTE.ASM in C: a tail jump through the renderer's paste-rectangle slot (7). */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/DRIVER/VIDINIT.H"
#include "bak_game_end.h"

void cga_rect_paste_from_buffer(unsigned char far *pDest, s16 x, s16 row, s16 width, s16 height) {
    (*(PasteRectFn)g_renderer_vtable.pfn_paste_rect)(pDest, x, row, width, height);
}
