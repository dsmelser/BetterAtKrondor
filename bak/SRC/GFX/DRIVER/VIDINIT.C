/*
 * VIDINIT.C -- video_init (180c:2a44 / 0x1ab04, 290 bytes).
 *
 * Video subsystem bring-up: frees any stale palette-scratch buffer, records the
 * BIOS video mode, detects the adapter, loads the matching driver and calls its
 * init entry, copies the driver's 100-entry vtable template into
 * g_renderer_vtable (segment half patched to the driver segment), publishes
 * DGROUP at the absolute engine cell 0000:04f0, allocates the span-table scratch
 * buffer, and grabs the ROM 8x8 font via INT 10h AH=11h AL=30h.
 *
 * Borland RTL-style .cas (C with pseudo-registers + inline asm, cf. DOSMEM.C):
 * the driver init returns its vtable template in DS-relative SI/DX registers
 * consumed by the REP MOVSW block, and the return value rides an asm PUSH AX /
 * POP AX bracket across the tail. Every direction-ambiguous reg,reg op is
 * compiler-emitted pseudo-register C; the asm statements proper are all
 * direction-unambiguous instructions, so the bc31 built-in assembler's store-
 * form habit never shows. This TU owns the video-mode _DATA block (the three
 * mode/driver-init globals at DG 0x2ea8..0x2eae).
 *
 * Split from VIDINIT.ASM: the sibling thunk cga_rect_paste_from_buffer (0x1ab00)
 * cannot be C (bare vtable tail-jump) and stays asm in CGAPASTE.ASM, linked
 * immediately before this object.
 */
#define alloc_far alloc_far_typed /* this TU calls alloc_far K&R (4 word args) */
#include "structs.h"
#include "defines.h"
#include "SRC/SYS/DOSMEM.H"
#include "SRC/GFX/DRIVER/VIDINIT.H"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GEN/VIDVTBL.H"
#include "SRC/GFX/DRIVER/VIDMODE.H"
#include "SRC/GFX/DRIVER/VIDDET.H"
#include "SRC/GFX/DRIVER/VIDDRV.H"
#include "SRC/GFX/FONT/FONT.H"
#undef alloc_far

void far *alloc_far(); /* K&R view: (size_lo, size_hi, flags_lo, flags_hi) */

extern unsigned char g_renderer_vtable[]; /* this TU's opaque view (BASM can't offset a struct symbol) */

typedef void far VideoDriverInitFn(void *ctx, VideoDriverImports far *imports);

unsigned char g_bSavedBiosVideoMode = 0xff; /* DG+0x2ea8 */
unsigned char g_bRequestedVideoMode = 0xff; /* DG+0x2ea9 */
void far *g_pfnVideoDriverInit = 0;         /* DG+0x2eaa */

/*
 * The same sequence with the port's built-in driver: video_driver_bind stands for loading
 * the chunk and calling its init entry, the slot table is copied as is (no segment halves
 * to patch), and the ROM 8x8 font comes from the platform. Publishing DGROUP at 0000:04f0
 * served the loaded drivers' callbacks and has no native counterpart.
 */
s16 video_init(s16 mode, s16 b, char *driver_name) {
    const void *slots;
    unsigned char far *span;
    unsigned char far *rom_font;
    s16 adapter;

    (void)b;
    (void)driver_name;
    g_bRequestedVideoMode = (unsigned char)mode;
    g_graphics_context.bYResDoubled = 0;
    g_graphics_context.bGfxRenderStateFlag = 0;
    g_wScreen_width = 0x140;
    g_wScreen_height = 0xc8;
    if (g_graphics_context.pPaletteScratchBuf != NULL) {
        _freemem(g_graphics_context.pPaletteScratchBuf);
        g_graphics_context.pPaletteScratchBuf = NULL;
    }
    g_bSavedBiosVideoMode = (unsigned char)bios_get_video_mode_bits();
    adapter = video_detect_adapter();
    g_graphics_context.bVideoAdapter = (unsigned char)adapter;
    slots = adapter != 0 ? video_driver_bind(adapter) : NULL;
    if (slots != NULL)
        memcpy(g_renderer_vtable, slots, 0x32 * 4);
    else
        g_graphics_context.bVideoAdapter = 0;
    g_graphics_context.wGfxBlitSrcPage = g_graphics_context.wVgaFrontPageBase;
    g_graphics_context.wGfxBlitDstPage = g_graphics_context.wVgaPage2Base;
    adapter = g_graphics_context.bVideoAdapter;
    if (adapter == 0)
        return 0;
    if (g_graphics_context_render.wSpanTableBufSeg != 0)
        _freemem(MK_FP(g_graphics_context_render.wSpanTableBufSeg - 1, 0));
    span = alloc_far((u32)(u16)((g_wScreen_height << 2) + 0x20), 0L);
    if (span == NULL)
        return adapter;
    g_graphics_context_render.wSpanTableBufSeg = FP_SEG(span) + 1;
    rom_font = bak_rom_font_8x8();
    g_font_bitmap_data[0] = rom_font;
    g_font_bitmap_data[1] = rom_font;
    *(u16 *)g_graphics_context.pFont_height = 0x808;
    *(u16 *)g_graphics_context.pFont_glyph_width_bits = 0x808;
    *(u16 *)g_graphics_context.pFont_base_char = 0;
    *(u16 *)g_graphics_context.pFont_glyph_count = 0xffff;
    return adapter;
}
