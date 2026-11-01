#ifndef BAK_FMAP_H
#define BAK_FMAP_H

#include "structs.h"

extern ImageRecord **g_pFmapIconTable;
extern unsigned short *g_pFmapTownXCoords;
extern unsigned short *g_pFmapTownYCoords;
extern char **g_pFmapTownLabels;
extern unsigned short g_wFmapMapWidth;
extern unsigned short g_wFmapMapHeight;
extern unsigned short g_wFmapHotspotW;
extern unsigned short g_wFmapHotspotH;
extern unsigned short g_wFmapTownCount;
extern unsigned short g_wFmapLabelRectW;
extern unsigned short g_wFmapLabelRectH;
extern unsigned char far *g_pFmapLabelRectBuf;

extern void fmap_screen_run(void);
extern void fmap_twn_load(void);
extern void fmap_hotspots_unload(void);
extern s16 fmap_hit_test_cursor(void);
extern s16 fmap_xy_lookup_for_chapter(s16 *out_x, s16 *out_y);
extern s16 fmap_farptr_normalize(u16 *pHeading);

#endif
