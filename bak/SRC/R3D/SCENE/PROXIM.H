#ifndef BAK_PROXIM_H
#define BAK_PROXIM_H

#include "structs.h"

extern ProximityZoneSettings far *g_pCurProximityZoneRec;
extern unsigned short g_nProximityTableCount;
extern ProximityZone far *far *g_pProximityTable;

extern void proximity_table_load(char *file);
extern void proximity_table_free(void);
extern unsigned char proximity_scan_list(ProximityScanHit *out_hit, WorldPos2 *pos, unsigned short record_seg,
                                 unsigned short *record_offsets, s16 count);
extern char proximity_check_index(ProximityScanHit *out_hit, WorldPos2 *pos,
                                  ProximityRecord far *pRecord);
extern void proximity_vec2_long_rotate_q14(s32 *xy, s16 angle);
extern unsigned char far *proximity_record_vertex_ptr(ProximityZone far *zone, s16 vertex_idx);
extern unsigned char proximity_point_test(ProximityScanHit *out_hit, WorldPos2 *pos,
                                  ProximityZone far *zone, ProximityRecord far *pRecord,
                                  char check_first_vertex);
extern unsigned char proximity_polygon_contains(ProximityPolygon far *poly, s16 point_x, s16 point_y);
extern s16 proximity_interp_height(ProximityPolygon far *poly, s16 point_x, s16 point_y);

#endif
