#ifndef BAK_COMBAT_H
#define BAK_COMBAT_H

#include "structs.h"

extern u16 g_bCombatGridTerrainFeaturesEnabled;
extern u16 g_bCombatGridLinesEnabled;
extern u16 g_bShowActorRosterIndex;
extern u16 g_bStormAmplify;
extern u16 **g_pTerrainBackupGrid;
extern u16 g_awQuarrelKindItemIdTable[8];
extern u16 g_quarrel_kind_field19_table[8];
extern Combatant *g_current_actor;
extern Combatant *g_picked_actor;
extern u16 g_acting_actor_speed;
extern s16 g_grid_tile_size;
extern s16 g_cursor_tile_x;
extern s16 g_cursor_tile_y;
extern u16 g_wInCombatMode;
extern MenuPage *g_combat_menu;
extern MenuPage *g_shoot_menu;
extern u16 g_combat_menu_selected_item;
extern u16 g_combat_menu_current_page;
extern s16 g_nCurrentQuarrelKindIdx;
extern u16 g_combat_cancelled;
extern u16 g_combat_music_handle;
extern Combatant *g_acting_actor;
extern u16 g_traps_loaded_flag;
extern u16 g_encounter_id;
extern Combatant *g_pCombatActiveActors;
extern AnimSlot *g_pCombatActiveAnimPool;
extern s16 g_nCombatActiveCount;
extern Combatant *g_pCombatOtherActors;
extern AnimSlot *g_pCombatOtherAnimPool;
extern s16 g_nCombatOtherCount;
extern ImageRecord **g_combat_sprites;
extern ImageRecord **g_parch_bmx;
extern ImageRecord **g_combat_render_table_prev;

extern void combat_arena_mode_enter(void);
extern void combat_arena_mode_exit(void);
extern void combat_arena_tick_status_timers(void);
extern void combat_arena_actor_turn_loop(s16 encounter_id, s16 *p_status, s16 b_has_fired);
extern void combat_arena_suspend_char_screen(Combatant *actor, s16 *p_spell_result,
                                             s16 *p_spell_out);
extern void combat_arena_splash_dmg_near(Combatant *source_actor, s16 damage);
extern void combat_arena_actor_set_anim_pose(Combatant *actor, unsigned char frame);
extern void combat_arena_swap_tgt_state(void);
extern s16 combat_arena_wait_confirm_cancel(void);
extern void combat_arena_resolve_melee_swing(Combatant *attacker, Combatant *defender,
                                             s16 damage);
extern void combat_arena_melee_attack(Combatant *attacker, Combatant *defender, s16 damage);
extern void combat_arena_apply_damage(Combatant *actor, s16 damage, s16 apply_magic_defense,
                                      s16 knockback, u16 flags, s16 source_type);
extern void combat_arena_actor_poison_tick(Combatant *actor);
extern void combat_arena_actor_die(Combatant *actor, s16 play_anim);
extern void combat_arena_load_remap_pals(void);
extern void combat_arena_remove_flagd_actors(void);
extern s16 combat_arena_all_targets_immune(void);
extern u16 combat_arena_anim_tbl_lookup_2d(s16 row, s16 col);
extern void combat_arena_round_trans_show(s16 param_1);
extern void combat_arena_draw_tgt_info_hud(Combatant *param_1, s16 param_2, s16 param_3,
                                           s16 param_4);
extern void combat_arena_draw_tgt_info_panel(void);
extern void combat_arena_hud_melee_panel(void);
extern s16 combat_arena_cnt_actors_combat(void);
extern s16 combat_arena_cnt_actors_flag_10(void);
extern void combat_arena_resume_dispatch(s16 command_id, s16 *p_spell_result,
                                         s16 *out_spell_result);
extern s16 combat_arena_dist_actors_by_id(s16 actor_id_a, s16 actor_id_b);
extern void combat_arena_show_message_by_id(s16 message_id, u16 *p_menu, s16 *p_param3,
                                            s16 *p_param4, s16 *p_param5, s16 *p_spell_result,
                                            s16 *p_spell_out, s16 *p_param8, MenuPage **p_menu_slot,
                                            s16 *p_param10, s16 *p_param11);
extern Combatant *combat_arena_disp_spell_action(s16 *param_1, s16 *param_2, s16 *param_3,
                                                   s16 param_4, s16 *param_5, s16 *param_6);
extern void combat_arena_turn_actor_inact(u16 *p_far_offset, s16 *p_turn_done, s16 *p_hud_state,
                                          s16 *p_ai_flag);
extern void combat_arena_resolve_menu_action(s16 *p_state_a, s16 *p_move_cost, s16 *p_state_b,
                                             s16 b_input_locked, s16 b_input_gate, s16 *p_dirty_hud,
                                             Combatant **pp_cursor_target, s16 arg8,
                                             s16 spell_record_id, s16 arg10,
                                             MenuPage **pp_menu_slot);

#endif
