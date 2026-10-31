#ifndef BAK_SKY_H
#define BAK_SKY_H

#include "structs.h"

extern void sky_horizon_strip_compute_slope(SkyStrip far *pStrip);
extern void sky_horizon_strips_init(s16 a);
extern void sky_draw_ground_band(void);
extern void sky_polygon_draw_dispatch(u16 vcount, s16 *xs, s16 *ys);

#endif
