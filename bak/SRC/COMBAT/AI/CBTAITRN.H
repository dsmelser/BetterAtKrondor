#ifndef BAK_CBTAITRN_H
#define BAK_CBTAITRN_H

#include "structs.h"

extern EncounterAiActionFn g_encounter_ai_action_table[8];
extern unsigned char __dat_15dc[128];

extern s16 combataiturn_sel_consum_qrl(Combatant *actor, s16 kind, s16 consume_flag);
extern void combataiturn_ranged_attack(Combatant *attacker, Combatant *target,
                                       s16 quarrel_type);
extern s16 combataiturn_armor_eff_stat(Combatant *actor);
extern s16 combataiturn_thunk_action_6(Combatant *actor);
extern s16 combataiturn_action_kind6(Combatant *actor);
extern s16 combataiturn_action_1000a(Combatant *actor);
extern s16 combataiturn_action_2000a(Combatant *actor);
extern s16 combataiturn_action_4000a(Combatant *actor);
extern s16 combataiturn_action_5000a(Combatant *actor);
extern s16 combataiturn_action_3000a(Combatant *actor);
extern s16 combataiturn_select_and_engage(Combatant *actor);
extern s16 combataiturn_pick_tile_or_attack(Combatant *actor, s16 min_score_threshold,
                                            s16 may_attack);
extern s16 combataiturn_action_disp_base(Combatant *actor, s16 param_2, u16 param_3);
extern void combataiturn_take_actor_turn(Combatant *actor);

#endif
