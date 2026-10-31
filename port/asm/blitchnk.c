/*
 * BLITCHNK.ASM in C: two tail jumps through renderer-vtable slots 17 and 18. The first
 * becomes a call through its slot with the same arguments; the second has no caller.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/SPRITE/BLITCHNK.H"
#include "bak_game_end.h"

/* Slot 17's native form, BLITCHNK.H's prototype (structs.h's BlitChunkyFn splits the far
 * pointer and leaves out the height). */
typedef void BlitChunkyNativeFn(unsigned char *src, s16 dst_x, s16 dst_y, s16 width_bytes,
                                s16 height);

void blit_chunky(unsigned char far *src, s16 dst_x, s16 dst_y, s16 width_bytes, s16 height) {
    (*(BlitChunkyNativeFn *)g_renderer_vtable.pfn_blit_chunky)(src, dst_x, dst_y, width_bytes,
                                                               height);
}

void blit_chunky_planar_4bit(void) {
    BAK_UNIMPLEMENTED("BLITCHNK.ASM: blit_chunky_planar_4bit (no caller in the game)");
}
