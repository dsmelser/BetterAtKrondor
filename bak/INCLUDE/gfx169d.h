#ifndef GFX169D_H
#define GFX169D_H

#ifdef BAK_NATIVE
/* The tables sit one after the other in POLYRAST.ASM's code segment, from
 * FOG_REMAP_TAB_OFF on, with 400h scratch bytes after them that the combat code fills
 * through g_abCursorPaletteLut; the scaled blit reads them by those offsets. */
extern unsigned char g_polyrastTables[0xf00];
#define g_abFogRemapTable (g_polyrastTables)
#define g_abCursorPaletteLut (g_polyrastTables + 0xa00)
#else
extern unsigned char far g_abCursorPaletteLut[256];

extern unsigned char far g_abFogRemapTable[2560];
#endif

#ifdef V102CD
#define FOG_REMAP_TAB_OFF 0x66
#define CURSOR_REMAP_TAB_OFF 0xa66
#else
#define FOG_REMAP_TAB_OFF 0x5c
#define CURSOR_REMAP_TAB_OFF 0xa5c
#endif

#endif
