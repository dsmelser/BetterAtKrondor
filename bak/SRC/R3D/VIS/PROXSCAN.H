#ifndef BAK_PROXSCAN_H
#define BAK_PROXSCAN_H

#include "structs.h"

extern short g_nVisibleEntryCount;
extern WorldObject far **g_ppVisibleEntries;
extern s32 g_aFilterTable[43];
extern s32 *g_pVisibleEntryDistances;

extern void proxscan_filter_table_load(void);
extern void proxscan_scratch_alloc(void);
extern void proxscan_scratch_free(void);
extern void proxscan_full(ViewContext *widget);
extern void proxscan_run(VisibleEntryList *list, s32 *party_pos);
extern void proxscan_filter_by_distance(s32 *origin);
extern void proxscan_object_list(s32 *origin);
extern void proxscan_all_zones_for_encounter(ViewContext *pContext);
extern void proxscan_encounter_records(VisibleEntryList *list, s32 *party_pos);
extern s16 proxscan_vis_rec_kind_0e3e(void);
extern s16 proxscan_paged_find_next_type0f(WorldObject far **out_entry);
extern void proxscan_visibility(ViewContext *pViewContext);
extern void proxscan_region_bucket(VisibleEntryList *region, s32 *party_pos);
extern void proxscan_world_objects(s32 *p_pos);
extern void proxscan_paged_dispatch_all(void);
extern void proxscan_paged_list_dispatch(VisibleEntryList *list, s32 *party_pos);
extern void proxscan_paged_dispatch_by_type(s32 *p_coords);
extern void proxscan_draw_cmap_inset_markers(void);
extern void proxscan_draw_zone_vis_marks(VisibleEntryList *list, s32 *position);
extern void proxscan_draw_visible_markers(s32 *p_position);
extern void proxscan_draw_fixed_object_marks(s32 *p_position);
extern void proxscan_broadcast_scene_events(void);
extern void proxscan_rndr_zone_recs_near(VisibleEntryList *list, s32 *position);
extern void proxscan_draw_vis_marks_near(s32 *position);
extern void proxscan_notify_actors_near(s32 *position);

#endif
