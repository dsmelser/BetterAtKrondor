#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "SRC/GAME/GMAIN.H"
#include "structs.h"
#include "SRC/GAME/STATE/GSTATE.H"
#include "SRC/SYS/RAND.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/IO/RESFAR.H"
#include "SRC/GFX/PALETTE/PALETTE.H"
#include "SRC/DIALOG/EVTCOND.H"
#include "SRC/CHAR/STAT.H"
#include "SRC/GAME/STATE/TIMERPL.H"
#include "SRC/DIALOG/DIALOG.H"
#include "SRC/GAME/ACTOR/ITEMTBL.H"
#include "SRC/COMBAT/SPELL/SPELLFX.H"
#include "SRC/SYS/CFGPARSE.H"

TempWriteRegion g_pendingTempWriteRegion = TWR_NONE;
char g_chapterDefaultSpeaker[] = {CHR_LOCKLEAR, CHR_JAMES, CHR_JAMES, CHR_GORATH, CHR_JAMES,
                                  CHR_OWYN,     CHR_JAMES, CHR_OWYN,  CHR_PUG};
ResFile *g_tempGamFP = NULL;
char g_fatigueDrainPerChar[CHARACTER_POOL_SIZE] = {
    -2, // Locklear
    -1, // Gorath
    -2, // Owyn
    -2, // Pug
    -2, // James
    -3  // Patrus
};
char g_restRegenPerChar[CHARACTER_POOL_SIZE] = {
    1, // Locklear
    1, // Gorath
    1, // Owyn
    1, // Pug
    1, // James
    1  // Patrus
};

unsigned short far gstate_event_read(unsigned short id) {

    if (id < 0x2134) {
        return (g_gameState.event_bitmap_lo[id >> 3] & (1 << (id & 7))) ? 1 : 0;
    }
    if (id >= 0xc350 && id < 0xc3da) {
        return itemtbl_partySize_by_kind(id + 0x3cb0);
    }
    if (id >= 0xcb20 && id < 0xcb27) {
        u16 w = id + 0x34df;
        return spellfx_event_mask_test_bit(w);
    }
    if (id >= 0xcf08 && id <= 0xcf6c) {
        u16 w = id + 0x30f8;
        return RND(w);
    }
    if (id >= 0xdac0) {
        u16 cx = id + 0x2540;
        u16 row = cx / 10;
        u16 w = cx % 10 - 1;
        return (g_gameState.event_bitmap_hi[row] & (1 << w)) ? 1 : 0;
    }

    switch (id - 0x7530) {
    case 0:
        return g_gameState.nEvtArgCount;
    case 1: {
        s32 g = g_gameState.nParty_gold / 10;
        return (g > 0xffffL) ? 0xffff : (unsigned short)g;
    }
    case 2:
        return (g_gameState.nParty_gold > 0xffff) ? 0xffff : (unsigned short)g_gameState.nParty_gold;
    case 7:
        return g_gameState.nChapter;
    case 19:
        return g_gameState.nZoneId;
    case 3:
        return (g_gameState.nParty_gold >= g_gameState.lEvtArgGoldCost) ? 1 : 0;
    case 13:
        return g_gameState.nEvtArgDlgResult;
    case 5:
#ifdef BAK_NATIVE
        if (gstate_is_party_member(CHR_PUG))
            return CHR_PUG;
        /* Chapter 0 (no game loaded yet) reads the byte before the table: in the
         * original's DGROUP the high byte of g_pendingTempWriteRegion (FFh when unarmed). */
        if (g_gameState.nChapter == 0)
            return (unsigned short)(char)((u16)g_pendingTempWriteRegion >> 8);
        return g_chapterDefaultSpeaker[g_gameState.nChapter - 1];
#else
        return gstate_is_party_member(CHR_PUG) ? CHR_PUG
                                               : g_chapterDefaultSpeaker[g_gameState.nChapter - 1];
#endif
    case 10: {
        short h = (short)(g_gameState.game_time % 0xa8c0 / 0x708);
        return (h >= 4 && h < 0x14) ? 1 : 0;
    }
    case 9: {
        short h = (short)(g_gameState.game_time % 0xa8c0 / 0x708);
        return (h < 4 || h >= 0x14) ? 1 : 0;
    }
    case 12:
        return (unsigned short)(g_gameState.game_time % 0xa8c0 / 0x708);
    case 14:
        return (unsigned short)g_gameState.lEvtArgGoldCost;
    case 15:
        return (unsigned short)g_gameState.lEvtArgValue;
    case 18:
        return (unsigned short)g_gameState.lEvtArgAuxValue;
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
        return ((id - 0x7544 + 1) == g_gameState.nChapter) ? 1 : 0;
    case 29:
        return (g_gameState.dwPopup_retry_state > 0) ? 1 : 0;
    default:
        return evtcond_range_d_read_handler(id);
    }
}

s16 far gstate_event_write(u16 event_id, u16 value) {
    u16 idx;
    u16 q;
    u16 bit;

    if (event_id < 0x2134) {
        if (value != 0) {
            g_gameState.event_bitmap_lo[event_id >> 3] |= (1 << (event_id & 7));
        } else {
            g_gameState.event_bitmap_lo[event_id >> 3] &= ~(1 << (event_id & 7));
        }
    } else if (event_id >= 0xdac0) {
        idx = event_id + 0x2540;
        q = idx / 10;
        bit = idx % 10 - 1;
        if (value != 0) {
            g_gameState.event_bitmap_hi[q] |= (1 << bit);
        } else {
            g_gameState.event_bitmap_hi[q] &= ~(1 << bit);
        }
    } else {
        switch (event_id - 0x7530) {
        case 0:
            g_gameState.nEvtArgCount = value;
            break;
        case 14:
            g_gameState.lEvtArgGoldCost = (s32)value;
            break;
        case 15:
            g_gameState.lEvtArgValue = (s32)value;
            break;
        case 18:
            g_gameState.lEvtArgAuxValue = (s32)value;
            break;
        case 16:
            g_gameState.bCombatExitRequest = value;
            break;
        case 17:
            g_gameState.nWorldLoopExitRequest = value;
            break;
        case 7:
            g_gameState.nChapter = value;
            break;
        case 6:
            g_gameState.dwLastActionTimeSnapshot = 0;
            break;
        default:
            evtcond_range_d_write_stub(event_id, value);
            break;
        }
    }
}

s16 gstate_temp_file_open(void) {
    char buf[50];

    if (g_cfgTempDrive != 0) {
        sprintf(buf, "%c:%s", g_cfgTempDrive, "TEMP.GAM");
        if ((g_tempGamFP = res_fopen(buf, "r+b")) != (ResFile *)0)
            return 1;
        return 0;
    }

    if ((g_tempGamFP = res_fopen("TEMP.GAM", "r+b")) != (ResFile *)0)
        return 1;
    return 0;
}

void gstate_temp_file_close(void) {
    res_fclose(g_tempGamFP);
    g_tempGamFP = (ResFile *)0;
    return;
}

s16 gstate_temp_file_read_at(void far *dst_far, s32 offset, u16 bytes) {
    if (res_fseek(g_tempGamFP, offset, SEEK_SET) != 0) {
        res_fseek(g_tempGamFP, 0, SEEK_SET);
        if (res_fseek(g_tempGamFP, offset, SEEK_SET) != 0) {
        }
    }
    if ((u32)res_ftell(g_tempGamFP) == offset) {
    }
    res_fread_far(dst_far, 1, (s32)bytes, g_tempGamFP);
    return 1;
}

s16 gstate_temp_file_write_at(unsigned char far *src_far, u32 offset, u16 bytes) {
#ifdef V102CD
    u32 lo;
    u32 hi;
    if (g_tempGamFP == (ResFile *)0 || offset > 0x51aa9)
        return 0;
    switch (g_pendingTempWriteRegion) {
    case TWR_ENCOUNTER_STATE:
        lo = 0xad7;
        hi = 0x90e7;
        if (offset < lo || offset + bytes > hi)
            return 0;
        break;
    case TWR_COMBAT_ACTOR:
        lo = 0x90e7;
        hi = lo + 0x281fe;
        if (offset < lo || offset + bytes > hi)
            return 0;
        break;
    case TWR_COMBAT_ACTOR_INNER:
        lo = 0x312e5;
        hi = lo + 0x94ac;
        if (offset < lo || offset + bytes > hi)
            return 0;
        break;
    case TWR_ACTOR_POOL:
        lo = 0x3a791;
        hi = 0x51aa9;
        if (offset < lo || offset + bytes > hi)
            return 0;
        break;
    case TWR_GAMESTATE:
        lo = 0;
        hi = 0xad7;
        if (offset < lo || offset + bytes > hi)
            return 0;
        break;
    }
    g_pendingTempWriteRegion = TWR_NONE;
#endif
    if (res_fseek(g_tempGamFP, offset, SEEK_SET) != 0) {
        res_fseek(g_tempGamFP, 0, SEEK_SET);
        if (res_fseek(g_tempGamFP, offset, SEEK_SET) != 0)
            ;
    }
    if (res_ftell(g_tempGamFP) == (s32)offset)
        ;
    res_fwrite_far(src_far, 1, (s32)bytes, g_tempGamFP);
    return 1;
}

static s16 far gstate_consume_rations_tick(s16 ctx) {
    s16 table_loaded;
    s16 i;
    s16 event_total;

    table_loaded = itemtbl_load();
    i = event_total = 0;
    for (; i < g_gameState.partySize; i = i + 1) {
        event_total += gstate_member_consume_rations(g_gameState.activeParty[i], ctx);
    }
    if (table_loaded != 0) {
        itemtbl_free();
    }
    return event_total;
}

s16 far gstate_member_consume_rations(s16 member_slot, s16 a) {
    s16 ret;
    Combatant *actor;

    actor = &g_gameState.characters[member_slot];
    ret = 0;
    g_gameState.nEvtArgActor0 = member_slot;

    if (itemtbl_inv_consume_one_by_kind(actor->actor_record, 0x48) != 0) {
        stat_combatant_apply_condition(actor, 5, -100);
    } else if (itemtbl_inv_consume_one_by_kind(actor->actor_record, 0x4a) != 0) {
        stat_combatant_apply_condition(actor, 5, -100);
        stat_combatant_apply_condition(actor, 0, 3);
    } else if (itemtbl_inv_consume_one_by_kind(actor->actor_record, 0x49) != 0) {
        stat_combatant_apply_condition(actor, 2, 4);
    } else {
        stat_combatant_apply_condition(actor, 5, 5);
    }
    return ret;
}

extern ConditionInfo far g_aConditionInfo[7];

static s16 gstate_hourly_tick(s16 arg0, s16 arg1, s16 arg2) {
    s16 cond;
    s16 regen;
    s16 eventFired;
    s16 allConscious;
    u32 elapsed;
    s16 val;
    register s16 actor;
    register s16 slot;

    eventFired = 0;
    allConscious = 1;
    elapsed = g_gameState.game_time - g_gameState.dwLastActionTimeSnapshot;
    if ((((arg0 != 0) && (arg1 != 0)) && (elapsed >= 0x7788)) && (elapsed < 0x7e90)) {
        dialog_play_record(0x40, eventFired = 1);
    } else if ((arg1 != 0) && (elapsed >= 0x7e90)) {
        for (slot = 0; slot < g_gameState.partySize; slot = slot + 1) {
            actor = g_gameState.activeParty[slot];
            if (stat_combatant_modify(&g_gameState.characters[actor], 0x10,
                                      (s32)((s16)g_fatigueDrainPerChar[actor] << 8), 100) == 0) {
                allConscious = 0;
            }
        }
        if (allConscious && (arg0 != 0))
            dialog_play_record(0x40, eventFired = 1);
    }
    for (slot = 0; slot < g_gameState.partySize; slot = slot + 1) {
        actor = g_gameState.activeParty[slot];
        if (arg2 != 0) {
            stat_combatant_apply_condition(&g_gameState.characters[actor], 0, -3);
            val = (arg2 == 100) ? 0x50 : 100;
            regen = (g_restRegenPerChar[actor] * arg2) / 100;
            if (g_gameState.abActorStatusRanks[actor][4] != '\0') {
                regen *= 2;
            }
        } else {
            regen = 0;
            val = 100;
        }
        cond = 0;
        do {
            if (g_gameState.abActorStatusRanks[actor][cond] != '\0') {
                s16 amount;
                amount = g_aConditionInfo[cond].nStatDelta;
                if ((cond < 4) && (g_gameState.abActorStatusRanks[actor][4] != '\0')) {
                    amount -= (cond == 0) + 2;
                }
                stat_combatant_apply_condition(&g_gameState.characters[actor], cond, amount);
            }
            if ((g_gameState.abActorStatusRanks[actor][cond] != '\0') &&
                (g_aConditionInfo[cond].nRegenDelta != 0)) {
                regen += g_aConditionInfo[cond].nRegenDelta;
            }
            cond++;
        } while (cond < 7);
        if (regen != 0) {
            stat_combatant_modify(&g_gameState.characters[actor], 0x10, (s32)(regen << 8), val);
        }
    }
    if ((arg0 != 0) && ((g_gameState.bPartyDirtyFlags & 2) != 0)) {
        val = (char)g_gameState.bPartyDirtyFlags;
        g_gameState.bPartyDirtyFlags = '\x02';
        evtcond_pty_dirty_flags_process();
        g_gameState.bPartyDirtyFlags = val & 1;
        eventFired = 1;
    }
    return eventFired;
}

s16 far gstate_advance_time(s32 seconds, s16 flags, s16 recompute_party, s16 a, s16 b) {
    s16 actor;
    s16 rank;
    s16 n;
    s16 i;
    s16 old_hd;
    s16 eventTotal;
    s16 old_tick;
    Combatant *member;

    eventTotal = 0;
    old_tick = (s16)(g_gameState.game_time % 0xa8c0 / 0x708);
    old_hd = (s16)(g_gameState.game_time / 0xa8c0);
    g_gameState.game_time += seconds;
    if (g_gameState.game_time / 0xa8c0 != (s32)old_hd) {
        if (old_hd % 0x1e == 0) {
            for (i = 0; i < g_gameState.partySize; i = i + 1) {
                member = &g_gameState.characters[g_gameState.activeParty[i]];
                member->stats[0].max = member->stats[0].max + (member->stats[0].max < 0xfa);
                member->stats[1].max = member->stats[1].max + (member->stats[1].max < 0xfa);

                n = SKILL_IMPROVED(g_gameState.activeParty[i] * 0x11);
                gstate_event_write(n, 1);
                gstate_event_write(n + 1, 1);
            }
        }
        if (recompute_party != 0) {
            eventTotal += gstate_consume_rations_tick(flags);
        }
        for (i = 0; i < g_gameState.partySize; i = i + 1) {
            actor = g_gameState.activeParty[i];
            rank = (char)g_gameState.abActorStatusRanks[actor][6];
            n = (rank + -100) / 10 + -1;
            if (g_gameState.abActorStatusRanks[actor][4] != '\0') {
                n *= 2;
            }
            if (rank != 0) {
                stat_combatant_apply_condition(&g_gameState.characters[actor], 6, n);
            }
        }
    }
    if ((g_gameState.game_time % 0xa8c0) / 0x708 != (s32)old_tick) {
        eventTotal += gstate_hourly_tick(flags, a, b);
    }
    timerpool_tick((s16)seconds);
    palette_daylight_tick();
    return eventTotal;
}

s16 far gstate_advance_hours(s16 hours, s16 pad, s16 flags) {
    s16 eventTotal;

    g_gameState.dwLastActionTimeSnapshot = g_gameState.game_time;
    eventTotal = gstate_advance_time((s32)hours * 0x708, 1, 1, 0, flags);
    g_gameState.dwLastActionTimeSnapshot = g_gameState.game_time;
    return eventTotal;
}

s16 gstate_is_party_member(s16 actor_id) {
    s16 i;

    for (i = 0; g_gameState.partySize > i; i++) {
        if (g_gameState.activeParty[i] == actor_id) {
            return 1;
        }
    }
    return 0;
}

s16 gstate_actor_is_caster(Combatant *actor) {
    return actor->stats[SKILL_CASTING].max != 0;
}

Combatant *gstate_party_member_record(s16 slot) {
    if (slot >= 0 && slot < g_gameState.partySize) {
        return &g_gameState.characters[g_gameState.activeParty[slot]];
    }
    return 0;
}

s16 gstate_find_party_slot(Combatant *actor) {
    s16 result_slot;
    s16 i;

    i = 0;
    result_slot = -1;
    for (; i < g_gameState.partySize; i = i + 1) {
        if (g_gameState.characters[g_gameState.activeParty[i]].charSlot ==
            actor->charSlot) {
            result_slot = i;
        }
    }
    return result_slot;
}

void gstate_format_money(char *buf, s32 gold, s16 mode) {
    s32 g = gold / 10;
    s32 s = gold % 10;

    if (mode == 0) {
        sprintf(buf, "%ld.%ld gold", g, s);
    } else if (mode == 1) {
        if (s != 0) {
            if (g != 0) {
                sprintf(buf, "%ld gold %ld silver", g, s);
            } else {
                sprintf(buf, "%ld silver", s);
            }
        } else {
            sprintf(buf, "%ld gold", g);
        }
    } else {
        if (s != 0) {
            if (g > 1) {
                sprintf(buf, "%ld sovereigns and %ld royal", g, s);
            } else {
                if (g != 0) {
                    sprintf(buf, "%ld sovereign and %ld royal", g, s);
                } else {
                    sprintf(buf, "%ld royal", s);
                }
            }
            if (s > 1) {
                strcat(buf, "s");
            }
        } else {
            sprintf(buf, "%ld sovereign%c", g, g > 1 ? 's' : 0);
        }
    }
}
