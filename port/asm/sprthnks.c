/*
 * SPRTHNKS.ASM in C: tail jumps through the renderer's sprite slots. blit_sprite (slot 3)
 * and blit_stretched_dispatch (slot 37) have no caller in the game and stay traps.
 */
#include "../drivers/vga.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/SPRITE/SPRTHNKS.H"
#include "bak_game_end.h"

typedef void PutSpriteFn(ImageRecord *sprite, s16 x, s16 y);

void blit_sprite(void) {
    BAK_UNIMPLEMENTED("SPRTHNKS.ASM: blit_sprite (no caller in the game)");
}

/* Slot 29: the record is a near pointer, natively a pointer. */
void blit_sprite_indirect(NearPtr sprite_desc_offset, short x, short y, unsigned short flip_flags) {
    ((SpriteMasterFn *)g_renderer_vtable.pfn_blit_sprite_master)(
        (const ImageRecord *)sprite_desc_offset, x, y, flip_flags);
}

/* Slot 33. */
void gfx_putsprite_fp(ImageRecord *sprite, s16 x, s16 y) {
    ((PutSpriteFn *)g_renderer_vtable.pfn_blit_sprite_simple)(sprite, x, y);
}

void blit_stretched_dispatch(void) {
    BAK_UNIMPLEMENTED("SPRTHNKS.ASM: blit_stretched_dispatch (no caller in the game)");
}
