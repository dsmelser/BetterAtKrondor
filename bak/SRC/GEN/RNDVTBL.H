#ifndef BAK_RNDVTBL_H
#define BAK_RNDVTBL_H

#include "structs.h"

extern RendererVtable g_renderer_vtable;
#ifdef BAK_NATIVE
/* RNDVTBL.ASM's labels on individual slots, for inline asm: the slots themselves. */
#define g_pfn_blit_glyph_row (g_renderer_vtable.pfn_blit_glyph_row)
#define g_pfn_blit_stretched (g_renderer_vtable.pfn_blit_stretched)
#define g_pfn_fill_run_a (g_renderer_vtable.pfn_fill_run_a)
#define g_pfn_blit_sprite_chunky_flip (g_renderer_vtable.pfn_blit_sprite_chunky_flip)
#else
extern Slot01Fn far *g_pfn_blit_glyph_row;
extern void(far *g_pfn_blit_stretched)(void);
extern void(far *g_pfn_fill_run_a)(void);
extern void(far *g_pfn_blit_sprite_chunky_flip)(void);
#endif

#endif
