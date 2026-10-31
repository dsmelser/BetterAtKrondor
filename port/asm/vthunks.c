/*
 * VTHUNKS.ASM in C: a no-op and five tail jumps through renderer-vtable slots. Each
 * becomes a call through the slot with the same arguments.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/DRIVER/VTHUNKS.H"
#include "bak_game_end.h"

void gfx_dispatch_noop(void) {
}

void set_border_color(s16 color) {
    (*(SetBorderColorFn)g_renderer_vtable.pPfn_set_border_color)(color);
}

/* Slot 26 is empty in every driver but HEG's (a palette-entry setter); nothing calls this in
 * the game. */
void renderer_vslot26_dispatch(void) {
    (*(UnimplementedSlotFn)g_renderer_vtable.slot_26_unimplemented)();
}

void vsync_hook(s16 a) {
    (*(VsyncWaitFn)g_renderer_vtable.pfn_vsync_wait)(a);
}

void cga_save_rect_to_buffer(unsigned char far *pDest, s16 x_start, s16 row, s16 width,
                             s16 row_count) {
    (*(Slot05Fn)g_renderer_vtable.pfn_save_rect)(pDest, x_start, row, width, row_count);
}

s32 rect_byte_size(s16 width, s16 height) {
    return (*(RectByteSizeFn)g_renderer_vtable.pfn_rect_byte_size)(width, height);
}
