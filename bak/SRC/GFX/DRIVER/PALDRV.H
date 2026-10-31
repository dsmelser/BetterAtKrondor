#ifndef BAK_PALDRV_H
#define BAK_PALDRV_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern s16 g_palette_cycle_count;
extern s16 g_nPaletteScaledIntensity;
extern s16 g_nPaletteScaledLevel;
extern s16 g_nCurChunkSize;
extern u16 g_awPaletteSizeByAdapter[16];
extern char g_szPalVgaChunkTag[9];
extern char g_szPalEgaChunkTag[9];
extern char g_szPalCgaChunkTag[9];
extern char g_szPalEmptyChunkTag[1];
extern u16 g_apszPalChunkTagByAdapter[16];
extern unsigned char far *g_pCurPalette;
extern s16 g_palette_cycle_end[10];
extern s16 g_palette_cycle_start[10];
extern s16 g_palette_cycle_target[10];

extern unsigned char far *chunk_load_into_slot(FileRef *file);
extern unsigned char far *palette_set_active(unsigned char far *palette);
extern void cache_release(unsigned char far *buf_farptr);
extern void palette_set_scaled(u16 first_color, u16 last_color, s16 target_color, s16 intensity);
extern s16 palette_cycle_add(s16 start, s16 count, s16 target);
extern void palette_cycle_tick(void);
extern void near memcpy_inline(unsigned char far *src, unsigned char far *dst, s16 count);

#endif
