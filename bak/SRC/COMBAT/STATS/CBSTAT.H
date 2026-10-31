#ifndef BAK_CBSTAT_H
#define BAK_CBSTAT_H

#include "structs.h"

extern s16 g_anHexVertexX0[6];
extern s16 g_anHexVertexY0[6];
extern s16 g_anHexVertexX2[6];
extern s16 g_anHexVertexY2[6];
extern s16 g_anHexVertexX3[6];
extern s16 g_anHexVertexY3[6];
extern s16 g_anHexVertexX4[6];
extern s16 g_anHexVertexY4[6];
extern s16 g_anHexVertexX5[6];
extern s16 g_anHexVertexY5[6];
extern s16 g_anHexVertexX1[6];
extern s16 g_anHexVertexY1[6];
extern s16 g_anHexVertexX6[6];
extern s16 g_anHexVertexY6[6];
extern s16 *g_apHexVertexX[7];
extern s16 *g_apHexVertexY[7];
extern short g_aClassGroupModifier[3][4];
extern unsigned short g_aClassCombatGroup[59];
extern unsigned short g_aClassProficiencyMask[64];
extern unsigned short g_aClassWeaknessMask[64];
extern unsigned char *g_pSpellWeaTable;
extern unsigned char *g_pSpellResTable;

extern void cbstat_apply_drain_tick(Combatant *actor);
extern s16 cbstat_to_hit_roll(Combatant *attacker, Combatant *target, s16 roll,
                              ItemRecord far *weapon_item);
extern s16 cbstat_armor_absorption_by_class(Combatant *attacker, Combatant *defender,
                                            s16 attack_type);
extern s16 cbstat_compute_attack_damage(ItemRecord far *weapon, Combatant *attacker,
                                        Combatant *defender, s16 attack_type);
extern ItemRecord far *cbstat_find_intact_equip_cat(Combatant *actor, s16 category);
extern void cbstat_damage_equipped_items(Combatant *actor, s16 category, s16 severity);
extern s16 cbstat_magic_defense_bonus(Combatant *actor);
extern s16 cbstat_magic_attack_bonus(Combatant *actor);
extern u16 cbstat_actor_class_table_1802(Combatant *actor);
extern s16 cbstat_scale_base_stat_pct(Combatant *actor, s16 percent);
extern s16 cbstat_apply_proficiency_bonus(Combatant *actor, s16 value, u16 proficiency_mask);
extern u16 cbstat_armor_coverage_mask(Combatant *combat_actor);
extern s16 cbstat_apply_weakness_penalty(Combatant *actor, s16 value, u16 weakness_mask);
extern void cbstat_spell_tables_load(void);
extern void cbstat_spell_tables_free(void);
extern s16 cbstat_char_bitmap_3w_test_170c(s16 char_idx, s16 bit_idx);
extern s16 cbstat_char_bitmap_3w_test(s16 char_idx, s16 bit_idx);
extern s16 cbstat_inv_item_condition_rec(ItemRecord far *target_far, Combatant *actor);
extern s16 cbstat_damage_apply_protection(Combatant *actor, s16 damage, s16 damage_type);
extern s16 cbstat_item_get_condition(ItemSlot far *slot);
extern s16 cbstat_apply_equipped_item_mult(Combatant *actor, s16 value, s16 category);

#endif
