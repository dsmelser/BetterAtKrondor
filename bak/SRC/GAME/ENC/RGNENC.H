#ifndef BAK_RGNENC_H
#define BAK_RGNENC_H

#include "structs.h"

extern short g_nEncounter_record_count;
extern short g_nFixed_object_count;
extern short g_nVisible_entry_count;
extern unsigned short *g_pZoneShapeIndex;
extern unsigned short *g_pChapterShapeIds;
extern EncounterRecordTemplate *g_pEncounterRecordTemplates;
extern s32 g_anEncounterRecordIds[5];
extern WorldObject far *g_pFixed_object_entries;
extern WorldObject far *g_pVisible_entry_pool;
extern EncounterObjectState *g_pEncounterObjectState;
extern s16 g_nCombatTempZoneEncounterCount;
extern unsigned char g_bCombatTempZoneRefPair;

extern void rgnenc_alloc_3bufs_init(void);
extern void rgnenc_free_3bufs_clear(void);
extern s16 rgnenc_load_zone_shape_index(char *filename);
extern s16 rgnenc_rel_object_state_group(void);
extern void rgnenc_savefile_init_35slot_tbl(void);
extern void rgnenc_load_encounter_actors(void);
extern void rgnenc_release_anim_images_slot4(void);
extern void rgnenc_zone_rectr_save_objects(void);
extern void rgnenc_persist_zone_snapshot(void);
extern s16 rgnenc_persist_actor_placed(s32 record_id, s16 slot_index,
                                       EncounterObjectState *src_partial);
extern s16 rgnenc_persist_actor_removed(s32 record_id, s16 slot_index);
extern void rgnenc_reset_and_save(void);
extern void rgnenc_mark_defended(u32 filter_encounter_id);
extern void rgnenc_complete_consume(u32 encounter_id);
extern void rgnenc_render_object(WorldObject far *entry, s16 bAnimTick);
extern void rgnenc_corpse_tbl_iterate_22byte(void);
extern s16 rgnenc_slot_actor_kind_eq_placed(WorldObject far *pSlot);
extern void rgnenc_world_objects_reset_spawn(void);
extern s16 rgnenc_visible_pool_append_spawn(unsigned char far *pEntry);
extern void rgnenc_vis_pool_remove_matching(unsigned char far *pKey);
extern s16 rgnenc_chap_shp_init(void);
extern s16 rgnenc_release_sfx_slot_table(void);
extern s16 rgnenc_global_flag_tbl_contains(unsigned short id);

#endif
