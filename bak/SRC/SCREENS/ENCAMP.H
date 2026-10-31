#ifndef BAK_ENCAMP_H
#define BAK_ENCAMP_H

#include "structs.h"

extern u16 *g_pEncampBmpAssetTable;
extern u16 *g_pEncampHotspotX;
extern u16 *g_pEncampHotspotY;
extern u16 *g_pClockHandVertsX;
extern u16 *g_pClockHandVertsY;
extern unsigned char *g_pEncampPaletteRemapLut;
extern char *g_apszEncampStatColHeaders[2];
extern char *g_pszHpSeparator;
extern u16 g_anEncampStatColX[2];
extern u16 g_wEncampHotspotSubW;
extern u16 g_wEncampHotspotSubH;
extern u16 g_wEncampHotspotBoxW;
extern u16 g_wEncampHotspotBoxH;
extern u16 g_wEncampHotspotCount;
extern u16 g_wEncampClockVertexCount;

extern void encamp_run(void);
extern void encamp_load(void);
extern void encamp_teardown(void);
extern void encamp_restore_screen_background(void);
extern void encamp_proj_trail_emit_facing(s16 a, s16 b, s16 c, u16 d, u16 e, u16 f);
extern s16 encamp_hotspot_at_cursor(void);
extern void encamp_build_palette_remap_lut(void);
extern void encamp_free_buffer(void);
extern u16 encamp_palette_nearest_color(unsigned char far *palette_ptr, s16 bias);
extern void encamp_draw_clock_hand(u16 time_in_period);
extern void encamp_draw_party_stats(void);

#endif
