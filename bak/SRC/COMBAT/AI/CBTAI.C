#include "SRC/GAME/GMAIN.H"
#include "structs.h"
#include "SRC/COMBAT/AI/CBTAI.H"
#include "SRC/SYS/RAND.H"
#include "SRC/CHAR/STAT.H"
#include "SRC/COMBAT/ACTOR/CACTOR.H"
#include "SRC/COMBAT/ARENA/COMBAT.H"
#include "SRC/COMBAT/ENC/CBENC.H"
#include "SRC/COMBAT/AI/CMBTAI.H"
#include "SRC/COMBAT/AI/CBTAITRN.H"
#include "SRC/COMBAT/SPELL/CSPELL.H"
#include "defines.h"
#include <stddef.h>

EncounterAiTurnFn g_encounter_ai_turn_table[8] = {
    &combat_ai_try_cast_heal,     &combat_ai_turn_kind6,          &combat_ai_turn_packet_10006,
    &combat_ai_turn_packet_20006, &combat_ai_turn_packet_40006,   &combat_ai_turn_packet_50006,
    &combat_ai_turn_packet_30006, &combat_ai_try_aoe_cast_spell_7};
unsigned char __dat_153c[128] = {
    0x01, 0x00, 0x08, 0x00, 0x06, 0x00, 0x03, 0x00, 0x07, 0x00, 0x02, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x02, 0x00, 0x01, 0x00, 0x03, 0x00, 0x07, 0x00, 0x05, 0x00, 0x08, 0x00, 0x06, 0x00, 0x04, 0x00,
    0x03, 0x00, 0x06, 0x00, 0x08, 0x00, 0x07, 0x00, 0x01, 0x00, 0x02, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x04, 0x00, 0x03, 0x00, 0x05, 0x00, 0x06, 0x00, 0x02, 0x00, 0x01, 0x00, 0x07, 0x00, 0x08, 0x00,
    0x05, 0x00, 0x04, 0x00, 0x07, 0x00, 0x06, 0x00, 0x03, 0x00, 0x08, 0x00, 0x01, 0x00, 0x02, 0x00,
    0x06, 0x00, 0x08, 0x00, 0x01, 0x00, 0x03, 0x00, 0x02, 0x00, 0x07, 0x00, 0x04, 0x00, 0x05, 0x00,
    0x07, 0x00, 0x03, 0x00, 0x02, 0x00, 0x08, 0x00, 0x06, 0x00, 0x01, 0x00, 0x05, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x02, 0x00, 0x03, 0x00, 0x07, 0x00, 0x05, 0x00, 0x01, 0x00, 0x06, 0x00, 0x04, 0x00};

void far combat_ai_actor_cast_spell(Combatant *caster, Combatant *target, s16 spell_id) {
    cspell_resolve_cast(caster, target, spell_id,
                        cspell_actor_stat_get_comb_dflt(caster, spell_id));
}

void far combat_ai_resolve_attack_attempt(Combatant *attacker, Combatant *target,
                                          s16 spell_id) {
    if (combatenc_skill_check_random(attacker, target, stat_actor_get(attacker, 7, 0), -1) != 0) {
        cspell_resolve_cast(attacker, target, spell_id,
                            cspell_actor_stat_get_comb_dflt(attacker, spell_id));
    } else {
        combat_actor_anim0_if_not_dead(attacker, combat_actor_heading_from_to(attacker, target));
    }
}

#ifndef V102CD
void far combat_ai_resolve_hit(Combatant *attacker, Combatant *target, s16 damage, s16 dir) {
    s16 hpBefore;
    s16 staminaBefore;
    s16 b;

    if ((attacker == (Combatant *)0x0) || (cspell_stat_effect_find_type(attacker, 0x1f) == -1)) {
        hpBefore = target->stats[0].base;
        staminaBefore = target->stats[1].base;
        combat_arena_actor_set_anim_pose(target, '\x04');
        combat_arena_apply_damage(attacker, damage, 0, 0, 0, 1);
        attacker->inner->flags &= ~CAF_KNOCKBACK;
        if (((b = target->charSlot) == '\0') ||
            ((((b = (char)g_gameState.abActorStatusRanks[target->charSlot - 1][0]) == '\0' &&
               (b = (char)g_gameState.abActorStatusRanks[target->charSlot - 1][1]) == '\0' &&
               (b = (char)g_gameState.abActorStatusRanks[target->charSlot - 1][2]) == '\0' &&
               (b = (char)g_gameState.abActorStatusRanks[target->charSlot - 1][3]) == '\0' &&
               (b = (char)g_gameState.abActorStatusRanks[target->charSlot - 1][5]) == '\0' &&
               (b = (char)g_gameState.abActorStatusRanks[target->charSlot - 1][6]) == '\0')))) {
            stat_combatant_modify(target, 0x10, (s32)(dir << 8), 0x50);
        }
        target->inner->dmgFloatValue =
            -(((target->stats[0].base - hpBefore) + target->stats[1].base) - staminaBefore);
        target->inner->dmgFloatFrames = '\b';
        stat_actor_get(target, 0, 0);
        stat_actor_get(target, 1, 0);
        if (attacker != (Combatant *)0x0) {
            combat_actor_anim0_if_not_dead(attacker,
                                           combat_actor_heading_from_to(attacker, target));
        }
    }
    return;
}
#endif

s16 far combat_ai_pick_action(Combatant *actor) {
    s16 action_id;
    s16 i;
    s16 sc;

    action_id = -1;
    for (i = 0; i < g_nCombatActiveCount; i++) {
        if (g_pCombatActiveActors[i].inner->creatureType == 0x16 ||
            g_pCombatActiveActors[i].inner->creatureType == 0x17) {
            if (RND(100) > 10)
                break;
        }
    }
    if (i < g_nCombatActiveCount) {
        if (((sc = (signed char)g_pCombatActiveActors[i].inner->flags) & 2) == 0 &&
            cspell_check_castable(9, actor, 0) != 0 &&
            combat_actor_trace_proj_path(actor, &g_pCombatActiveActors[i], 1) != 0 &&
            g_pCombatActiveActors[i].inner->creatureType != 0x17) {
            action_id = 9;
        } else if ((g_pCombatActiveActors[i].inner->flags & CAF_DEAD) != 0 &&
                   cspell_check_castable(0x20, actor, 0) != 0 &&
                   ((sc = (signed char)g_pCombatActiveActors[i].inner->flags) & 0x10) == 0 &&
                   g_pCombatActiveActors[i].inner->gridX != -1) {
            action_id = 0x20;
        }
        if (action_id == -1)
            goto L_phase2;
        cspell_resolve_cast(actor, &g_pCombatActiveActors[i], action_id,
                            cspell_actor_stat_get_comb_dflt(actor, action_id));
        return 1;
    }

L_phase2:
    for (i = 0; i < g_nCombatActiveCount; i++) {
        if (g_pCombatActiveActors[i].inner->creatureType == 0x36) {
            if (RND(100) > 10)
                break;
        }
    }
    if (i < g_nCombatActiveCount &&
        ((sc = (signed char)g_pCombatActiveActors[i].inner->flags) & 2) == 0 &&
        cspell_check_castable(0x2a, actor, 0) != 0) {
        cspell_resolve_cast(actor, &g_pCombatActiveActors[i], 0x2a,
                            cspell_actor_stat_get_comb_dflt(actor, 0x2a));
        return 1;
    }

    for (i = 0; i < g_nCombatActiveCount; i++) {
        if (g_pCombatActiveActors[i].inner->creatureType == 0x29 ||
            g_pCombatActiveActors[i].inner->creatureType == 0x2a ||
            g_pCombatActiveActors[i].inner->creatureType == 0x2b) {
            if (RND(100) > 10)
                break;
        }
    }
    if (i < g_nCombatActiveCount &&
        ((sc = (signed char)g_pCombatActiveActors[i].inner->flags) & 2) == 0 &&
        ((sc = (signed char)g_pCombatActiveActors[i].inner->flags) & 0x10) == 0 &&
        cspell_check_castable(0x29, actor, 0) != 0) {
        cspell_resolve_cast(actor, &g_pCombatActiveActors[i], 0x29,
                            cspell_actor_stat_get_comb_dflt(actor, 0x29));
        return 1;
    }
    return 0;
}

s16 far combat_ai_execute_turn(Combatant *actor, s16 param_2, u16 param_3) {
    s16 ret;
    s16 clearance;
    s16 spell_id;

    if (combat_ai_pick_action(actor) != 0) {
        return 1;
    }
    ret = 0;
    clearance = 4 - (s16)stat_actor_get(actor, 7, 0) / 0x19;
    combatenc_ai_pick_target_by_role(actor, param_2, param_3, clearance);
    if (actor->inner->target != (Combatant *)0) {
        spell_id = cspell_ai_pick_castable_spell(actor, actor->inner->target, 1, 0);
        if (spell_id < 0) {
            spell_id = cspell_ai_pick_castable_spell(actor, actor->inner->target, 1, 1);
        }
        if (spell_id > -1) {
            if (combatenc_actor_stat_above_table(0, actor) != 0) {
                if (combat_actor_trace_proj_path(actor, actor->inner->target, 1) != 0) {
                    combat_ai_actor_cast_spell(actor, actor->inner->target, spell_id);
                    ret = 1;
                }
            }
        }
    } else {
        combatenc_ai_pick_target_by_role(actor, param_2, param_3, 0);
        if (actor->inner->target != (Combatant *)0) {
            spell_id = cspell_ai_pick_castable_spell(actor, actor->inner->target, 1, 1);
            if (spell_id > -1) {
                if (combatenc_actor_stat_above_table(1, actor) != 0) {
                    combat_ai_resolve_attack_attempt(actor, actor->inner->target, spell_id);
                    ret = 1;
                }
            }
        }
    }
    return ret;
}

/* The spell combat_ai_try_cast_heal casts, if the caster knows it: Gift of Sung (7) when the
   lowest health among the caster's living allies is below a roll of 0-79, else Hocho's Haven (6)
   when the target lacks it, else none (-1). In an AI turn g_pCombatOtherActors holds the caster's
   side. The original reads the health of g_pCombatActiveActors, the caster's opponents, keeps the
   lowest and tests the last one's. The port skips dead allies, as the cast that follows does. */
s16 far combat_ai_pick_heal_spell(Combatant *caster, Combatant *target) {
    Combatant *ally;
    u16 hp;
    u16 lowestHp;
    u16 threshold;
    register s16 i;

    lowestHp = 0x6e;
    for (i = 0; i < g_nCombatOtherCount; i++) {
        ally = &g_pCombatOtherActors[i];
        if (ally == caster || (ally->inner->flags & CAF_DEAD) != 0)
            continue;
        hp = stat_actor_get(ally, 0, 0);
        if ((s16)hp < (s16)lowestHp)
            lowestHp = hp;
    }
    threshold = RND(80);
    if ((s16)lowestHp < (s16)threshold && cspell_check_castable(7, caster, -1))
        return 7;
    if (cspell_check_castable(6, caster, -1) && cspell_stat_effect_find_type(target, 6) == -1)
        return 6;
    return -1;
}

s16 far combat_ai_try_cast_heal(register Combatant *caster) {
    s16 casted;
    s16 spell_id;
    Combatant *target;

    casted = 0;
    target = g_pCombatOtherActors;
    if (combatenc_actor_stat_above_table(2, caster) != 0) {
        spell_id = combat_ai_pick_heal_spell(caster, target);
        if (spell_id == -1) {
            return 0;
        }
        {
            s16 hp_pct;
            s16 i;
            hp_pct = combat_actor_stat_percent(target, 0);
            if (caster != target && hp_pct > 0 && hp_pct < 100) {
                cspell_resolve_cast(caster, target, spell_id,
                                    cspell_actor_stat_get_comb_dflt(caster, spell_id));
                casted = 1;
            } else {
                i = 0;
                if (i < g_nCombatOtherCount) {
                    do {
                        hp_pct = combat_actor_stat_percent(&g_pCombatOtherActors[i], 0);
                        if (&g_pCombatOtherActors[i] != caster && hp_pct > 0 && hp_pct < 100) {
                            cspell_resolve_cast(caster, &g_pCombatOtherActors[i], spell_id,
                                                cspell_actor_stat_get_comb_dflt(caster, spell_id));
                            i = g_nCombatOtherCount;
                            casted = 1;
                        }
                        i++;
                    } while (i < g_nCombatOtherCount);
                }
            }
        }
    }
    return casted;
}

s16 far combat_ai_turn_kind6(Combatant *actor) {
    s16 result;

    result = combat_ai_execute_turn(actor, 6, 0);
    return result;
}

s16 far combat_ai_turn_packet_10006(Combatant *actor) {
    s16 result;

    result = combat_ai_execute_turn(actor, 6, 1);
    return result;
}

s16 far combat_ai_turn_packet_20006(Combatant *actor) {
    s16 result;

    result = combat_ai_execute_turn(actor, 6, 2);
    return result;
}

s16 far combat_ai_turn_packet_40006(Combatant *actor) {
    s16 result;

    result = combat_ai_execute_turn(actor, 6, 4);
    return result;
}

s16 far combat_ai_turn_packet_50006(Combatant *actor) {
    s16 result;

    result = combat_ai_execute_turn(actor, 6, 5);
    return result;
}

s16 far combat_ai_turn_packet_30006(Combatant *actor) {
    s16 result;

    result = combat_ai_execute_turn(actor, 6, 3);
    return result;
}

/* The ally an AI caster gives Gift of Sung (spell 7) in combat_ai_try_aoe_cast_spell_7: the first
   that an opponent attacks, alive and below 70% of its health (80% for side B's first combatant),
   or NULL. In an AI turn g_pCombatOtherActors holds the caster's side and g_pCombatActiveActors
   its opponents. */
Combatant *combat_ai_sung_ally_under_attack(Combatant *caster) {
    Combatant *ally;
    s16 i;

    for (i = 0; i < g_nCombatActiveCount; i++) {
        ally = g_pCombatActiveActors[i].inner->target;
        if (ally == caster || ally == NULL || (ally->inner->flags & CAF_DEAD) != 0)
            continue;
        if (combat_actor_stat_percent(ally, 0) < (ally == g_actors_B_origin ? 0x50 : 0x46))
            return ally;
    }
    return NULL;
}

/* The original never casts here. It tests the target's flags as !flags & 2, which is 0 whatever
   they are, where CAF_DEAD was meant; it walks the caster's own side and takes each one's target,
   an opponent; and it casts whether or not the caster knows the spell. The port walks the
   opponents and asks for the spell as combat_ai_pick_heal_spell does. */
s16 far combat_ai_try_aoe_cast_spell_7(Combatant *actor) {
    Combatant *ally;

    if (!combatenc_actor_stat_above_table(2, actor) || !cspell_check_castable(7, actor, -1))
        return 0;
    ally = combat_ai_sung_ally_under_attack(actor);
    if (ally == NULL)
        return 0;
    cspell_resolve_cast(actor, ally, 7, cspell_actor_stat_get_comb_dflt(actor, 7));
    return 1;
}

/* The order in which a turn profile (1-8) tries the turn actions, and the actions, numbered
   from 1. The original reaches both from __dat_1492 at 152Ch and 1518h: the rows and the
   entries before 1 are its DGROUP neighbours __dat_153c and g_encounter_ai_turn_table. */
#define AI_TURN_INDEX(profile, i) (((short *)__dat_153c)[((profile) - 1) * 8 + (i)])
#define AI_TURN_FN(n) (g_encounter_ai_turn_table[(n) - 1])

typedef s16(far *AiTurnFn)(Combatant *actor);

void far combat_ai_take_turn(Combatant *actor) {
    s16 spell_attempt;
    s16 dist;
    s16 rand_mod;
    s16 stat_pct;
    s16 result;
    AiTurnFn fn;

    spell_attempt = 0;
    result = 0;
    if (combataiturn_pick_tile_or_attack(actor, 1, 1)) {
        result = 1;
    } else {
        if ((s16)stat_actor_get(actor, 0x10, 0) < 5)
            spell_attempt = 8;
    }

    while (result == 0 && spell_attempt < 8 && actor->inner->aiTurnProfile != '\0') {
        rand_mod = RND(100);
        if (rand_mod < 0x5b) {
            fn = (AiTurnFn)AI_TURN_FN(
                AI_TURN_INDEX((s16)(signed char)actor->inner->aiTurnProfile, spell_attempt));
            result = fn(actor);
        }
        spell_attempt++;
    }

    if (result == 0) {
        stat_pct = combat_actor_stat_percent(actor, 1);
        if (stat_pct > 0x28 && RND(100) > 0xa && !actor->charSlot) {
            combatenc_find_nearest_actor(g_picked_actor, &dist);
            if (dist <= 6)
                dist = 7;
            dist -= 6;
            g_acting_actor_speed = (dist > (s16)g_acting_actor_speed) ? g_acting_actor_speed : dist;
            combataipath_select_target(actor, 0x64, 0);
        } else {
            combatenc_actor_enter_defense(actor);
        }
    }

    actor->inner->target = (Combatant *)0;
}
