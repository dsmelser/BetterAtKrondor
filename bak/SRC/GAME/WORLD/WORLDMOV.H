#ifndef BAK_WORLDMOV_H
#define BAK_WORLDMOV_H

#include "structs.h"

extern unsigned short g_bWorldCrossing;
extern unsigned short g_nWorldCrossingKind;
extern u32 g_dwWorldCrossingCoord;
extern short g_nWorldStepSpeed;
extern short g_nWorldGridStride;
extern s32 g_lWorldTimePerStep;

extern void worldmove_dat_load(void);
extern void worldmove_combat_zone_snap_pos(void);
extern void worldmove_rgn_chap_trans_apply(void);
extern s16 worldmove_party_attempt_move(s16 mode);
extern void worldmove_apply_turn_step(short direction);
extern void worldmove_animate_hdg_tgt(s16 hdg_step, s16 hdg_target);
extern void worldmove_plr_hdg_align_grid(void);
extern void worldmove_camera_crossing_apply(void);
extern s16 worldmove_aabb_outcode_rotated(unsigned char *tile_rect);
extern s16 worldmove_sweep_alt_headings(WorldPos *pos, s16 heading, s32 step, s16 mode,
                                        s16 *out_dir, s16 *out_heading);
extern s16 worldmove_step_free_move(WorldPos *pos, s16 heading, s32 step, s16 mode);
extern s16 worldmove_crossing_check_8dir(WorldPos *pos, s16 heading, s32 step, s16 mode,
                                         s16 *out_dir, s16 *out_target);
extern void worldmove_heading_step(short *heading, short step, short direction);
extern s16 worldmove_align_to_grid_stride(u16 *value);
extern void worldmove_vec2_rotate_add_q14(s32 *out, s16 angle, s32 scale);
extern void worldmove_crossing_apply_offset(WorldPos *pPos, s16 wDir, s16 delta);
extern s16 worldmove_probe_walkable_at(WorldPos *pos, s16 angle, s32 scale, u16 *out_kind,
                                       s32 *out_coord);
extern s16 worldmove_prox_query_at_cell(WorldPos *pos, u16 *out_type, s32 *out_coord);
extern s16 worldmove_probe_adjacent_cell(WorldPos *pos, s16 dir, u16 *out_type, s32 *out_coord);
extern s16 worldmove_sweep_adjacent_cells(WorldPos *pos, u16 dir, s16 mode, s16 *out_result);
extern s16 worldmove_prox_find_near_pos(WorldPos *pos);
extern void worldmove_cross_get_probe_result(unsigned short *out_kind, u32 *out_coord);
extern s16 worldmove_cross_walkable_kind(void);
extern void worldmove_step_pending_clear(void);
extern s16 worldmove_step_once_along_axis(void);
extern short worldmove_step_pending_get(void);
extern s16 worldmove_try_step_along_axis(void);
extern void worldmove_step_tick_reset(void);
extern unsigned short worldmove_step_tick_get(void);
extern void worldmove_step_tick_advance(void);
extern unsigned short worldmove_step_ticks_per_step(void);

#endif
