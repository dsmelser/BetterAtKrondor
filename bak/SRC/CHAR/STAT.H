#ifndef BAK_STAT_H
#define BAK_STAT_H

#include "structs.h"

extern unsigned char g_abStatMin[17];
extern u16 g_awStatMax[17];
extern unsigned char g_abStatClampMin[17];
extern unsigned char g_abStatClampMax[17];
extern unsigned char g_abStatRatio[17];
extern unsigned char g_abStatRatioBase[17];
extern unsigned char g_abStatRatioMax[17];

extern s16 stat_party_all_above_pct(s16 percent);
extern u16 stat_actor_get(Combatant *actor, s16 stat_idx, s16 mode);
extern u16 stat_combatant_modify(Combatant *actor, s16 stat_idx, s32 delta, s16 mode);
extern u16 stat_party_find_extreme(s16 stat_id, s16 param_2, s16 *result_out);
extern void stat_party_broadcast_status_op(s16 stat_idx, s32 delta, s16 mode);
extern u16 stat_combatant_apply_condition(Combatant *actor, s16 stat_idx, s16 amount);
extern void stat_modifier_table_insert(Combatant *actor, ActorStatModifier *pNewMod);
extern void stat_actor_clear_mods_mask(Combatant *actor, u16 mask);
extern void stat_combatant_heal(Combatant *combatant, s16 amount_pct);
extern void stat_party_heal_all(s16 amount);
extern void stat_actor_recalc_equip_bonuses(Combatant *actor);
extern void stat_apply_modifier(u16 *mod, s16 *stat);

#endif
