#ifndef BAK_CMBTGRID_H
#define BAK_CMBTGRID_H

#include "structs.h"

extern unsigned char far *g_pPalLoadedBuf;
extern unsigned char far *g_pPalAltA;
extern unsigned char far *g_pPalAltB;
extern unsigned char far *g_pPalBlendScratch;
extern unsigned short g_nPalBlendMode;
extern unsigned char far *g_pPalLastInstalled;
extern char g_rgbDayNightCycle_4f4[15];
extern s16 g_nPaletteBlendFactorCached;
extern unsigned char g_bssgap_4c34[2];
extern GridCombatant *g_combatant_table;
extern short g_combatant_count;
extern CombatTile g_combat_tile_grid[8][13];
extern s16 g_nCombatTerrainWallColor;
extern unsigned char g_combat_move_map[8][13];

extern void combatgrid_cmbts_tbl_alloc(void);
extern void combatgrid_combatants_table_free(void);
extern s16 combatgrid_coord_valid(s16 x, s16 y);
extern void combatgrid_remove_cmbt_at_tile(s16 tile_x, s16 tile_y);
extern void combatgrid_kill_cmbt_at_tile(s16 tile_x, s16 tile_y);
extern Combatant *combatgrid_tile_terrain(char tile_x, char tile_y);
extern GridCombatant *combatgrid_find_cmbt_at_tile(char tile_x, char tile_y);
extern u16 combatgrid_tile_blockd_cmbt(char tile_x, char tile_y);
extern u16 combatgrid_tile_is_blocked(char tile_x, char tile_y);
extern void combatgrid_tile_set_word(char x, char y, Combatant *value);
extern u16 combatgrid_tile_terrain_field(char x, char y);
extern s16 combatgrid_tile_field4(char x, char y);
extern void combatgrid_tick_tile_effect(char x, char y);
extern void combatgrid_set_tile_effect(char x, char y, unsigned short type, s16 timer);
extern s16 combatgrid_prox_check_kind_set(WorldPos2 *pos);
extern void combatgrid_world_to_view_2d(WorldPos2 *origin, s16 angle, WorldPos2 far *point,
                                        s16 *out_xy);
extern s16 combatgrid_tile_cam_near_wedge(s16 tile_x, s16 tile_y);
extern s16 combatgrid_obj_kind_prox_actv(s16 kind);
extern s16 combatgrid_cmbt_block_kind(s16 kind);
extern s16 combatgrid_plr_prox_sweep_cnt(void);
extern s16 combatgrid_tiles_match_ctr_px(void);
extern s16 combatgrid_tiles_over_thresh(void);
extern void combatgrid_build_combatant_list(WorldPos2 *pWorldPos, short *pViewState);
extern void combatgrid_tile_to_world_rotated(WorldPos2 *p_out, s16 tile_x, s16 tile_y);
extern void combatgrid_actor_set_grid_pos(s16 actor_idx, s16 new_x, s16 new_y);
extern void combatgrid_load_traps_dat(void);
extern void combatgrid_clear_combatants(void);
extern void combatgrid_tile_fx_init_walk(void);
extern void combatgrid_classify_tiles_px(void);
extern void combatgrid_tile_fx_init_pass(void);
extern void combatgrid_load_and_init(void);
extern void combatgrid_draw_terrain_walls(void);
extern void combatgrid_apply_tile_status_fx(s16 tile_x, s16 tile_y, s16 target_actor);
extern void combatgrid_mark_engagement_line(s16 x1, s16 y1, s16 x2, s16 y2, short paged_id,
                                            s16 do_register);
extern s16 combatgrid_place_actor_on_tile(s16 tile_x, s16 tile_y, s16 new_tile_x, s16 new_tile_y);
extern void combatgrid_find_adj_pass_tile(s16 x, s16 y, u16 terrain_mask, s16 *out_dx,
                                          s16 *out_dy);
extern void combatgrid_spread_tile_fx_line(s16 x, s16 y);
extern void combatgrid_line_effect_propagate(s16 x, s16 y);
extern s16 combatgrid_tile_walkable_kind(s16 x, s16 y, s16 kind);
extern s16 combatgrid_nbr_probe_diag(s16 x, s16 y, s16 kind);
extern void combatgrid_kill_adj_cmbt_team(s16 tile_x, s16 tile_y, s16 team);
extern void combatgrid_push_back_actor(s16 tile_x, s16 tile_y);
extern void combatgrid_shove_until_unblocked(s16 tile_x, s16 tile_y);
extern s16 combatgrid_nbr_probe_orthogonal(s16 x, s16 y, s16 kind);
extern s16 combatgrid_is_combatant_type(s16 paged_id);
extern void combatgrid_clear_tile_effects(void);
extern void combatgrid_save_traps_terr(s16 encounter_idx);
extern s16 combatgrid_can_engage_same_kind(s16 idx_a, s16 idx_b);
extern void combatgrid_apply_engage_links(s16 start_idx);
extern void combatgrid_place_tile_fx_at_cur(unsigned short type, s16 timer);
extern void combatgrid_clear_terr_6_effects(void);
extern s16 combatgrid_actor_past_terr6_row(void);
extern s16 combatgrid_any_terrain_6(void);
extern void combatgrid_cur_tile_world(void);
extern s16 combatgrid_chebyshev_distance(char x1, char y1, char x2, char y2);
extern s16 combatgrid_actors_ortho_adj(Combatant *actor_a, Combatant *actor_b);
extern s16 combatgrid_is_pure_diagonal(Combatant *actor, s16 target_x, s16 target_y);
extern s16 combatgrid_actor_try_step_tile(Combatant *actor, s16 tx, s16 ty);
extern void combatgrid_build_move_attack_map(Combatant *actor);
extern s16 combatgrid_cursor_tile_movable(void);
extern s16 combatgrid_tile_has_terr_bit2(Combatant *actor);
extern void combatgrid_actor_step_to_tile(Combatant *mover, s16 tile_x, s16 tile_y);
extern void combatgrid_pathfind_from_tile(s16 tx, s16 ty, s16 dest);
extern s16 combatgrid_tile_blockd_kind10(Combatant *actor);
extern s16 combatgrid_tile_passable_check(s16 required_terrain, s16 tile_x, s16 tile_y,
                                          Combatant *mover);
extern void combatgrid_step_search(Combatant *actor, s16 preferred_dir);
extern void combatgrid_rndr_engagement_lines(void);
extern void combatgrid_unhighlight_terr_type(unsigned short terrain);
extern s16 combatgrid_tile_has_mobility(s16 col, s16 row, s16 tile_count);

#endif
