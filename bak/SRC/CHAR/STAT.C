#include "SRC/GAME/GMAIN.H"
#include "SRC/COMBAT/ARENA/COMBAT.H"
#include "structs.h"
#include "SRC/GAME/ACTOR/ACTOR.H"

#include "SRC/CHAR/STAT.H"
#include "SRC/GAME/STATE/GSTATE.H"
#include "SRC/GAME/ACTOR/ITEMTBL.H"

unsigned char g_abStatMin[17] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
unsigned short g_awStatMax[17] = {0x01f4, 0x01f4, 0x01f4, 0x01f4, 0x00c8, 0x00c8,
                                  0x00c8, 0x00c8, 0x00c8, 0x00c8, 0x00c8, 0x00c8,
                                  0x00c8, 0x0064, 0x00c8, 0x00c8, 0x03e8};
unsigned char g_abStatClampMin[17] = {0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
unsigned char g_abStatClampMax[17] = {0xfa, 0xfa, 0xfa, 0xfa, 0x64, 0x64, 0x64, 0x64, 0x64,
                                      0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0x64, 0xf4};
unsigned char g_abStatRatio[17] = {0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                                   0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x00};
#ifdef V102CD
unsigned char g_abStatRatioBase[17] = {0x33, 0x33, 0x08, 0x08, 0x08, 0x33, 0x08, 0x33, 0x00,
                                       0x80, 0x80, 0x80, 0x20, 0x40, 0x80, 0x40, 0x00};
unsigned char g_abStatRatioMax[17] = {0x03, 0x03, 0x01, 0x01, 0x02, 0x03, 0x01, 0x03, 0x10,
                                      0x05, 0x05, 0x10, 0x04, 0x04, 0x10, 0x01, 0x00};
#else
unsigned char g_abStatRatioBase[17] = {0x33, 0x33, 0x08, 0x08, 0x08, 0x33, 0x08, 0x33, 0x00,
                                       0x00, 0x00, 0x80, 0x20, 0x33, 0x00, 0x40, 0x00};
unsigned char g_abStatRatioMax[17] = {0x03, 0x03, 0x01, 0x01, 0x02, 0x03, 0x01, 0x03, 0x08,
                                      0x05, 0x05, 0x20, 0x02, 0x03, 0x08, 0x01, 0x00};
#endif

void stat_apply_modifier(unsigned short *mod, s16 *stat) {
    if (((*mod & 0x100) == 0) || (g_wInCombatMode != 0)) {
        if (((*mod & 0x200) != 0) && (*(u32 *)(mod + 5) < g_gameState.game_time)) {
            *mod = 0;
        }
        if (*mod != 0) {
            if ((*mod & 0x400) == 0) {
            }
            if ((*mod & 0x800) != 0) {
                *stat = (s32)(s16)(*stat * (mod[2] + 100)) / 100;
            } else {
                *stat += (short)mod[2];
            }
        }
    }
}

void stat_actor_recalc_equip_bonuses(Combatant *actor) {
    s16 statIdx;
    s16 statVal;
#ifdef V102CD
    s16 found = 0;
#endif
    Actor far *actorRec;
    ItemRecord far *itemRec;
    s16 i;

    actorRec = actor->actor_record;
    for (statIdx = 0; statIdx < 0x10; statIdx++)
        actor->stats[statIdx].perm_mod = 0;
    for (i = 0; (s16)(u16)actorRec->itemCount > i; i++) {
#ifdef V102CD
        if (*(unsigned char far *)(ACTOR_ITEMS(actorRec) + (u16)i) == 0x5a) {
            if (found)
                continue;
            found = 1;
        }
#endif
        itemRec = itemtbl_record_ptr_by_id(
            (u16)*(unsigned char far *)(ACTOR_ITEMS(actorRec) + (u16)i));
        if (itemRec->wPlayer_stat_mask != 0) {
            statVal = itemRec->nStat_value;
            statIdx = 0;
            do {
                if (itemRec->wPlayer_stat_mask & (1U << statIdx))
                    actor->stats[statIdx].perm_mod = actor->stats[statIdx].perm_mod + (char)statVal;
                statIdx++;
            } while (statIdx < 0x10);
        }
    }
}

ConditionInfo far g_aConditionInfo[7] = {
    {"Sick", 1, -1, {0, 0, 0, 0}},     {"Plagued", 1, -2, {0, 0, 0, 0}},
    {"Poisoned", 1, -3, {0, 0, 0, 0}}, {"Drunk", -2, 0, {-14, -60, 0, 0}},
    {"Healing", -3, 1, {0, 0, 0, 0}},  {"Starving", 0, -2, {0, 0, 0, 0}},
    {"Near-death", 0, 0, {0, 0, 0, 0}}};

u16 stat_actor_get(Combatant *actor, s16 stat_idx, s16 mode) {
    u16 value;
    s16 rowIdx;
    u16 n;
    u16 statMask;
    unsigned short *modPtr;
    ConditionInfo far *cond;
    s16 rank;
    u16 ratio;
    u16 wMax;
    /* The local `a = actor` copy is the mechanism that forces `actor` into the
     * persistent DI register; `st` takes SI. Declaration order (st then a) fixes
     * SI/DI. */
    register StatSlot *st;
    register Combatant *a = actor;

    rowIdx = a->charSlot + -1;
    st = &a->stats[stat_idx];
    if (stat_idx == 0x10) {
        return stat_actor_get(a, 0, mode) + stat_actor_get(a, 1, mode);
    }
    if (mode == 3) {
        return (u16)st->base;
    }
    if (mode == 1) {
        return (u16)st->max;
    }

    value = (st->cached = st->base);
    if (st->perm_mod != 0) {
        value += (s16)st->perm_mod;
        if ((s16)value < 0) {
            value = 0;
        }
        st->cached = (unsigned char)value;
    }
    if (a->charSlot != 0) {
        statMask = 1 << (unsigned char)stat_idx;
        n = 0;
        modPtr = (unsigned short *)&g_gameState.aActorStatModifiers[rowIdx][0];
        while ((s16)n < 8) {
            if ((*modPtr != 0) && ((modPtr[1] & statMask) != 0)) {
                stat_apply_modifier(modPtr, (s16 *)&value);
            }
            n++;
            modPtr += 7;
        }
        n = 0;
        cond = g_aConditionInfo;
        while ((s16)n < 7) {
            if (0 < (rank = (char)g_gameState.abActorStatusRanks[rowIdx][n])) {
                if ((cond->pExtra[0] & statMask) != 0) {
                    value =
                        (u16)(((s32)(s16)value * ((s32)(cond->pExtra[1] * rank) / 100 + 100)) /
                               100);
                }
                if ((cond->pExtra[2] & statMask) != 0) {
                    value =
                        (u16)(((s32)(s16)value * ((s32)(cond->pExtra[3] * rank) / 100 + 100)) /
                               100);
                }
            }
            n++;
            cond += 1;
        }
    }
    if (mode != 4) {
        ratio = (u16)g_abStatRatio[stat_idx];
        if (ratio != 0) {
            n = (u16)a->stats[0].base;
            wMax = (u16)a->stats->max;
            if (1 < (s16)ratio) {
                n = (s16)(n + wMax * (ratio - 1)) / (s16)ratio;
            }
            if (wMax != 0) {
                value = (s16)((wMax + value * n) - 1) / (s16)wMax;
            } else {
                value = 0;
            }
        }
    }
    if (st->max == 0) {
        value = 0;
    }
    if ((s16)value < (s16)(u16)g_abStatMin[stat_idx]) {
        value = (u16)g_abStatMin[stat_idx];
    }
    if ((s16)g_awStatMax[stat_idx] < (s16)value) {
        value = g_awStatMax[stat_idx];
    }
    st->cached = (unsigned char)((s16)value > 0xfa ? 0xfa : value);
    return value;
}

u16 far stat_combatant_modify(Combatant *actor, s16 stat_idx, s32 delta, s16 mode) {
    StatSlot *slot;
    s16 origBase;
    s16 rowIdx;
#ifndef V102CD
    u16 sum;
    u16 uMaxSum;
#endif

    slot = actor->stats + stat_idx;
    origBase = (u16)slot->base;
    rowIdx = actor->charSlot + -1;
    if (stat_idx == 0x10) {
        u16 target;
        s16 rank;
#ifdef V102CD
        u16 sum;
        u16 uMaxSum;
#endif
        sum = (u16)actor->stats[1].base + (u16)actor->stats[0].base;
        uMaxSum = (u16)(actor->stats + 1)->max + (u16)actor->stats->max;
        target = (u16)((s32)(s16)(mode * uMaxSum) / 100L);
        if (actor->charSlot != 0) {
            rank = (char)g_gameState.abActorStatusRanks[rowIdx][6];
            if (rank != 0) {
                target = ((100 - rank) * 0x1e) / 100 + 1;
            }
        }
        if (0 < delta) {
            if ((s16)sum < (s16)target) {
                sum = sum + (s16)(delta / 0x100);
                if ((s16)sum > (s16)target) {
                    sum = target;
                }
            }
        } else {
            sum = sum + (s16)(delta / 0x100);
            if ((s16)sum <= 0) {
                sum = 0;
                if (actor->charSlot != 0) {
                    stat_combatant_apply_condition(actor, 6, 100);
                }
            }
        }
        if ((s16)(u16)actor->stats->max < (s16)sum) {
            actor->stats[1].base = (unsigned char)sum - actor->stats->max;
            actor->stats[0].base = actor->stats->max;
        } else {
            actor->stats[1].base = 0;
            actor->stats[0].base = (unsigned char)sum;
        }
        return sum;
    }
    if (slot->max == 0) {
        return 0;
    }
    if (mode == 3) {
#ifdef V102CD
        s32 lBase, lMax, iVar8;
        lBase = (s32)g_abStatRatioBase[stat_idx];
        lMax = (s32)g_abStatRatioMax[stat_idx];
        iVar8 = lBase + (lMax - lBase) * (s32)(unsigned char)slot->base / 100;
        if (iVar8 > 0) {
            delta = (delta == 0) ? iVar8 : delta * iVar8;
        } else {
            delta = 0;
        }
#else
        s16 ratioVal;
        ratioVal = (u16)g_abStatRatioBase[stat_idx] +
                   (s16)(((u16)g_abStatRatioMax[stat_idx] - (u16)g_abStatRatioBase[stat_idx]) *
                         (u16)slot->base) /
                       100;
        if (ratioVal > 0) {
            delta = (delta == 0) ? (s32)ratioVal : delta * ratioVal;
        } else {
            delta = 0;
        }
#endif
    } else if (mode == 2) {
        delta = (s16)(100 - (u16)slot->base) * delta;
    } else if (mode == 1) {
        delta = ((s16)(u16)slot->base * delta) / 100;
    }

    if ((actor->charSlot != 0) &&
        (gstate_event_read(SKILL_SELECTED(rowIdx * 0x11 + stat_idx)) != 0)) {
        delta += (delta * (short)g_gameState.aSkillTrainRate[rowIdx]) / 0x34;
    }
    delta += (s16)(u16)slot->frac;
    slot->frac = (unsigned char)(delta % 0x100);
    /* Magnitude compare against the underflow amount. The inner nested ternary is
     * the abs()-macro expansion (x<0 ? (x==INT_MIN ? INT_MAX : -x) : x). When the
     * decrement would drop base below zero, base is pinned to 0 and control joins
     * the shared clamp/finalize tail below, skipping the normal base += (delta/0x100)
     * add. */
    if (delta / 0x100 < 0) {
        if ((s32)(u32)slot->base <
            (delta / 0x100 < 0 ? (delta / 0x100 == -0x8000L ? 0x7fffL : -(delta / 0x100))
                               : delta / 0x100)) {
            slot->base = 0;
            goto clamp_finalize;
        }
    }
    slot->base = slot->base + (char)(delta / 0x100);
clamp_finalize:
    if (slot->base < g_abStatClampMin[stat_idx]) {
        slot->base = g_abStatClampMin[stat_idx];
    }
    if (slot->base > g_abStatClampMax[stat_idx]) {
        slot->base = g_abStatClampMax[stat_idx];
    }
    if (slot->max < slot->base) {
        slot->max = slot->base;
    }
    if (((actor->charSlot != 0) && ((u16)slot->base != (u16)origBase)) && (stat_idx != 0x10)) {
        if ((1 < stat_idx) || (origBase < slot->base)) {

            gstate_event_write(SKILL_IMPROVED(rowIdx * 0x11 + stat_idx), 1);
        }
        if (origBase < slot->base) {
            g_gameState.bPartyDirtyFlags |= 1;
        }
    }
    return (u16)slot->base;
}

u16 far stat_party_find_extreme(s16 stat_id, s16 mode, short *result_out) {
    s16 member_idx;
    u16 cur_val;
    u16 extreme;
    s16 i;

    if (stat_id == 0xf) {
        i = 0;
        extreme = 30000;
        for (; i < g_gameState.partySize; i++) {
            member_idx = g_gameState.activeParty[i];
            cur_val = stat_actor_get(&g_gameState.characters[member_idx], stat_id, mode);
            if ((s16)cur_val < (s16)extreme) {
                extreme = cur_val;
                g_gameState.nEvtArgStat = member_idx;
            }
        }
    } else {
        i = extreme = 0;
        for (; i < g_gameState.partySize; i++) {
            member_idx = g_gameState.activeParty[i];
            cur_val = stat_actor_get(&g_gameState.characters[member_idx], stat_id, mode);
            if ((s16)cur_val > (s16)extreme) {
                extreme = cur_val;
                g_gameState.nEvtArgStat = member_idx;
            }
        }
    }
    if (result_out != (short *)0) {
        *result_out = g_gameState.nEvtArgStat;
    }
    return extreme;
}

void far stat_party_broadcast_status_op(s16 stat_idx, s32 delta, s16 mode) {
    s16 i;

    for (i = 0; i < g_gameState.partySize; i++) {
        stat_combatant_modify(&g_gameState.characters[g_gameState.activeParty[i]], stat_idx,
                              delta, mode);
    }
}

u16 stat_combatant_apply_condition(Combatant *actor, s16 stat_idx, s16 amount) {
    s16 slot_idx;
    s16 new_val;
    s16 old_val;

    slot_idx = actor->charSlot - 1;
    if (actor->charSlot != 0 && amount != 0) {
        new_val = (signed char)g_gameState.abActorStatusRanks[slot_idx][stat_idx];
        old_val = new_val;
        new_val = new_val + amount;
        if (new_val < 0) {
            new_val = 0;
        }
        if (100 < new_val) {
            new_val = 100;
        }
        g_gameState.abActorStatusRanks[slot_idx][stat_idx] = (char)new_val;
        if (stat_idx != 4 && stat_idx != 3 && (stat_idx != 6 || g_wInCombatMode == 0)) {
            if (old_val == 0 && new_val != 0) {

                gstate_event_write(CONDITION(slot_idx * 7 + stat_idx), 1);
                g_gameState.bPartyDirtyFlags |= 2;
            } else if (old_val != 0 && new_val == 0) {
                gstate_event_write(CONDITION(slot_idx * 7 + stat_idx), 0);
            }
        }
        if (stat_idx != 6)
            return;
        for (stat_idx = 0, g_gameState.bCombatExitRequest = 1; stat_idx < g_gameState.partySize;
             stat_idx++) {
            if (!(signed char)
                     g_gameState.abActorStatusRanks[g_gameState.activeParty[stat_idx]][6]) {
                g_gameState.bCombatExitRequest = 0;
            }
        }
        if (0 < amount) {
            for (stat_idx = 0; stat_idx < 7; stat_idx++) {
                if (stat_idx != 6) {
                    g_gameState.abActorStatusRanks[slot_idx][stat_idx] = 0;
#ifndef V102CD
                    gstate_event_write(CONDITION(slot_idx * 7 + stat_idx), 0);
#endif
                }
            }
            actor->stats[1].base = actor->stats[0].base = 0;
            return stat_combatant_modify(actor, 0x10, 0x7fff, 100);
        }
    }
}

void far stat_modifier_table_insert(Combatant *actor, ActorStatModifier *pNewMod) {
    s16 bestCost;
    s16 i;
    s16 row;
    s16 cost;
    ActorStatModifier *pBest;
    ActorStatModifier *pSlot;

    row = actor->charSlot - 1;
    if (!actor->charSlot)
        return;
    i = 0;

    bestCost = 999;
    pBest = (ActorStatModifier *)0;
    for (pSlot = &g_gameState.aActorStatModifiers[row][0]; i < 8; pSlot++) {
        cost = pSlot->wMaskFlags & 0xff;
        if (pSlot->wMaskFlags == 0) {
            pBest = pSlot;
            break;
        }
        if (cost < bestCost) {
            pBest = pSlot;
            bestCost = cost;
        }
        i++;
    }
    *pBest = *pNewMod;
}

void far stat_actor_clear_mods_mask(Combatant *actor, unsigned short mask) {
    s16 party_slot_idx;
    s16 i;

    party_slot_idx = actor->charSlot - 1;
    if (actor->charSlot != 0) {
        i = 0;
        do {
            if (g_gameState.aActorStatModifiers[party_slot_idx][i].wMaskFlags & mask) {
                g_gameState.aActorStatModifiers[party_slot_idx][i].wMaskFlags = 0;
            }
            i++;
        } while (i < 8);
    }
}

s16 far stat_party_all_above_pct(s16 percent) {
    s16 memberIdx;
    u16 current;
    u16 max_val;
    s16 threshold;
    s16 i;

    i = 0;
    while (i < g_gameState.partySize) {
        memberIdx = g_gameState.activeParty[i];
        current = stat_actor_get(&g_gameState.characters[memberIdx], 0x10, 3);
        max_val = stat_actor_get(&g_gameState.characters[memberIdx], 0x10, 1);
        threshold = (s16)((s32)(percent * (s16)max_val) / 100L);
        if ((s16)current < threshold) {
            return 0;
        }
        i++;
    }
    return 1;
}

void far stat_combatant_heal(Combatant *combatant, s16 amount_pct) {
    s32 delta;
    s16 stat_idx;

    if (amount_pct == 100) {
        g_gameState.dwLastActionTimeSnapshot = g_gameState.game_time;
        stat_idx = 0;
        do {
            stat_combatant_apply_condition(combatant, stat_idx, -100);
            stat_idx++;
        } while (stat_idx < 7);
    }
    stat_combatant_modify(combatant, 0x10, 0x7fff, 100);
    if (amount_pct < 100) {
        delta = (s32)(s16)stat_actor_get(combatant, 0x10, 0) * -20L / 100L;
        stat_combatant_modify(combatant, 0x10, delta << 8, 100);
    }
}

void far stat_party_heal_all(s16 amount) {
    s16 i;

    for (i = 0; i < g_gameState.partySize; i++) {
        stat_combatant_heal(&g_gameState.characters[g_gameState.activeParty[i]], amount);
    }
}
