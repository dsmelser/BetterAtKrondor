#ifndef BAK_FMAP_H
#define BAK_FMAP_H

#include "structs.h"

extern ImageRecord **g_pFmapIconTable;
extern u16 *g_pFmapTownXCoords;
extern u16 *g_pFmapTownYCoords;
extern u16 *g_pFmapTownLabels;
extern u16 g_wFmapMapWidth;
extern u16 g_wFmapMapHeight;
extern u16 g_wFmapHotspotW;
extern u16 g_wFmapHotspotH;
extern u16 g_wFmapTownCount;
extern u16 g_wFmapLabelRectW;
extern u16 g_wFmapLabelRectH;
extern unsigned char far *g_pFmapLabelRectBuf;

extern void fmap_screen_run(void);
extern void fmap_twn_load(void);
extern void fmap_hotspots_unload(void);
extern s16 fmap_hit_test_cursor(void);
extern s16 fmap_xy_lookup_for_chapter(s16 *out_x, s16 *out_y);
extern s16 fmap_farptr_normalize(u16 *pHeading);

#endif
