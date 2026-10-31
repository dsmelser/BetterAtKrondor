#ifndef BAK_CBENC_H
#define BAK_CBENC_H

#include "structs.h"
#include "SRC/GAME/ACTOR/ACTOR.H"

extern short g_anStatCheckThreshold[9];
extern unsigned short g_ai_flee_threshold_table[10];
extern unsigned char g_bssgap_5128[2];
extern Combatant *g_combat_actors_B;
extern AnimSlot *g_anim_pool_B;
extern s16 g_combat_count_B;
extern unsigned char g_bssgap_511e[4];
extern Combatant *g_actors_B_origin;
extern short g_chapter_roster[7];
extern ImageRecord **g_abCombatInnerPoolB[7][11];
extern char g_combat_save_filename[14];
extern NearPtr g_inner_pool_B;

extern void combatenc_chap_load_party_grn(s16 chapter);
extern void combatenc_actor_flee_tile_east(Combatant *actor);
extern void combatenc_apply_flee_tile_team(s16 team_id);
extern void combatenc_mnames_lookup_dest(s16 index, char **p_dest);
extern void combatenc_draw_actor_roster_idx(Combatant *actor);
extern void combatenc_actor_face_target(Combatant *actor);
extern void combatenc_refr_actor_flags_far(void);
extern s16 combatenc_actor_can_act(Combatant *actor, s16 strict);
extern s16 combatenc_count_active_enemies(void);
extern s16 combatenc_party_within_cheby(Combatant *actor, s16 range);
extern s16 combatenc_alive_actor_count(void);
extern s16 combatenc_actor_id_eq_22(Combatant *actor);
extern s16 combatenc_actor_has_spell_1or4(Combatant *actor);
extern void combatenc_save_state(void);
extern void combatenc_ai_attempt_melee(Combatant *actor);
extern void combatenc_set_flag8_clear_flag1(Combatant *actor);
extern void combatenc_actor_enter_defense(Combatant *actor);
extern void combatenc_anim_actor_stat_rolls(Combatant *actor);
extern Combatant *combatenc_find_nearest_actor(Combatant *from, s16 *out_dist);
extern s16 combatenc_show_missile_stat_row(Combatant *actor);
extern s16 combatenc_actor_can_cast_spells(Combatant *actor, s16 find_nearest);
extern s16 combatenc_compute_hit_chance(Combatant *attacker, Combatant *target, s16 base_skill,
                                        s16 quarrel_kind);
extern s16 combatenc_skill_check_random(Combatant *attacker, Combatant *target, s16 base_skill,
                                        s16 quarrel_kind);
extern void combatenc_ai_pick_target_by_role(Combatant *actor, s16 max_distance, u16 role_filter,
                                             s16 min_party_clearance);
extern s16 combatenc_actor_stat_above_table(s16 threshold_idx, Combatant *actor);
extern void combatenc_ai_run_turn(void);
extern void combatenc_place_b_free_tiles(void);
extern void combatenc_clear_pty_grid_occ(void);
extern s16 combatenc_is_encounter_actor(Combatant *actor);
extern void combatenc_spawns_load_zones_tmp(void);
extern void combatenc_init_actor_anim_pool(void);
extern char *combatenc_build_save_filename(s16 slot);
extern void combatenc_pty_load_chap_state(void);
extern void combatenc_persist_actors_to_temp(s16 param_1);
extern void combatenc_vis_actors_call_rev(void);
extern void combatenc_release_actors(void);
extern void combatenc_ai_morale_flee_check(Combatant *actor);
extern void combatenc_pick_next_active_actor(void);
extern void combatenc_refresh_actor_flags(void);
extern void combatenc_ai_suicide_on_arrival(Combatant *actor);
extern void combatenc_ai_sel_execute_action(Combatant *actor);
extern Actor far *combatenc_corpse_tbl_spawn_actor(s32 record_id, s16 slot);
extern void combatenc_saves_migr_all_slots(void);

#endif
