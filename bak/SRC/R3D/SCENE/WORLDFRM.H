#ifndef BAK_WORLDFRM_H
#define BAK_WORLDFRM_H

#include "structs.h"

extern EncounterTable *g_pEncounterTable;

extern void worldframe_encounter_table_load(void);
extern void worldframe_encounter_table_apply(void);
extern void worldframe_enc_tbl_reapply_chap(void);
extern void worldframe_enc_rec_prox(unsigned char *record, s16 direction);
extern s16 worldframe_enc_check_visited(unsigned char *pRecord, s16 bitIndex);
extern void worldframe_debug_print_compass(WorldObject far *entry);
extern void worldframe_vislist_cull_occl(u16 seg, u16 *offsets, s32 *data, s16 *count);
extern void worldframe_render_chapter_full(void);
extern void worldframe_render_chapter(s16 animate);
extern void worldframe_chapter_tick_objects(void);
extern void worldframe_vislist_reorder(u16 visible_seg, u16 *visible_offsets, s16 count);

#endif
