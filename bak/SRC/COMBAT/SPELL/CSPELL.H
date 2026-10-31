#ifndef BAK_CSPELL_H
#define BAK_CSPELL_H

#include "structs.h"

extern u16 g_nSpellSymbolAnimFrame;
extern unsigned char g_bssgap_5154[6];
extern s16 g_nSpellCount;
extern unsigned char g_bssgap_5150[2];
extern SpellDef *g_pSpellDefs;
extern char *g_szSpellNameBlob;
extern SpellDocRow *g_pSpellDocRows;
extern unsigned char far *g_pSpellDocBlob;
extern StatusEffectSlot *g_pStatusEffectPool;
extern unsigned char far *g_pSpellScreenBuffer;
extern ImageRecord **g_pCastfaceSpriteTable;
extern u16 *g_pSpellCastableScratch;
extern s16 g_nSnowParticleSeedX;
extern s16 g_nSpellFontSlot;
extern s16 g_nCombatTileEffectPending;
extern s16 g_nCombatTileX;
extern s16 g_nCombatTileY;
extern s16 g_nSpellSymbolCount;
extern SpellSymbolRecord *g_pSpellSymbolRecords;
extern s16 *g_pSpellSymbolX;
extern s16 *g_pSpellSymbolY;

extern void cspell_gfx_wipe_horizontal_split(s16 x, s16 y, s16 w, s16 h);
extern s16 cspell_compute_effect_magnitude(Combatant *actor, s16 level, s16 spell_id);
extern void cspell_flash_actor_status_effect(Combatant *actor, s16 effect_type);
extern s16 cspell_check_castable(s16 spell_id, Combatant *actor, s16 param_3);
extern void cspell_summon_monster(s16 monster_type, s16 prompt_for_tile);
extern void cspell_apply_step_tile_spell(Combatant *mover, s16 spell_id, s16 intensity,
                                         s16 caster_class);
extern void cspell_status_effect_clear_actor(Combatant *actor);
extern s16 cspell_status_effect_add(Combatant *actor, s16 type, s16 source, s16 duration,
                                    unsigned char flag);
extern void cspell_status_effect_remove(Combatant *actor, s16 slot);
extern s16 cspell_stat_effect_find_type(Combatant *actor, s16 type);
extern void cspell_tile_trap_trigger(Combatant *actor);
extern void cspell_invoke_effect(Combatant *actor, Combatant **target_slot, s16 spell_id,
                                 s16 *out_chain_flag, s16 extra);
extern s16 cspell_record_field8(s16 idx);
extern void cspell_subsystem_load(void);
extern void cspell_subsystem_unload(void);
extern void cspell_resolve_cast(Combatant *caster, Combatant *target, s16 spell_id,
                                s16 intensity);
extern s16 cspell_actor_stat_get_comb_dflt(Combatant *actor, s16 spell_id);
extern void cspell_apply_hit_at(Combatant *caster, Combatant **target_ptr, s16 src_id,
                                s16 *dst_ptr, s16 magnitude, s16 spell_kind, s16 knockback);
extern void cspell_vfx_run_and_wait(void);
extern void cspell_tick_actor_stat_fx(void);
extern s16 cspell_ai_pick_castable_spell(Combatant *caster, Combatant *target, s16 school,
                                         s16 kind);
extern s16 cspell_cast_menu_loop(Combatant *caster, s16 *out_result, s16 *preferred_caster_slot,
                                 s16 *preselect_spell);
extern void cspell_tick_damage_terrain(void);
extern void cspell_status_effect_pool_init(void);
extern void cspell_status_effect_pool_free(void);
extern s16 cspell_status_effect_alloc_slot(void);
extern void cspell_apply_damage_armor_wear(Combatant *actor, s16 damage);
extern void cspell_palette_fade_to(s16 target_color, s16 target_intensity);
extern void cspell_actor_charge_to_target(Combatant *param_1, Combatant *param_2, s16 param_3);
extern void cspell_perform_ranged_hit(Combatant *param_1, Combatant *param_2, s16 param_3);
extern void cspell_select_target_tile(void);
extern void cspell_actor_walk_with_sound(Combatant *actor, Combatant *target);
extern void cspell_try_add_status_effect(Combatant *actor, s16 stat_idx, s16 value);
#ifdef V102CD
extern void far combat_ai_resolve_hit(Combatant *attacker, Combatant *target, s16 damage,
                                      s16 dir);
#endif
extern void cspell_apply_damage_with_recoil(Combatant *attacker, Combatant *target,
                                            s16 spell_id, s16 damage);
extern s16 cspell_count_terrain7_tiles(void);
extern s16 cspell_collect_castable(Combatant *caster, s16 category);
extern void cspell_draw_panel_text_list(MenuPage *panel, char **strings, s16 count,
                                        s16 first_visible, s16 row_count);
extern s16 cspell_paged_record_lookup_by_id(s16 slot);
extern void cspell_scroll_clamp_cursor(s16 *cursor, s16 *scroll, s16 visible_rows, s16 total_rows);
extern void cspell_symbol_resources_load(s16 chapter);

#endif
