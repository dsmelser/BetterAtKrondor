/*
 * SPRTHNKS.ASM in C: tail jumps through the renderer's sprite slots. blit_stretched_dispatch
 * (slot 37) has no caller in the game and stays a trap.
 */
#include "../drivers/vga.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/SPRITE/SPRTHNKS.H"
#include "bak_game_end.h"

typedef void PutSpriteFn(ImageRecord *sprite, s16 x, s16 y);

/* Slot 3: the record is a near pointer, natively a pointer. The 1.02 non-rotating map's party
   marker is its one caller. */
void blit_sprite(NearPtr sprite_desc_offset, s16 x, s16 y) {
    ((SpriteFn *)g_renderer_vtable.pfn_blit_sprite)((const ImageRecord *)sprite_desc_offset, x, y);
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
