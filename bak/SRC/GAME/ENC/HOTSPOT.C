#include "SRC/GAME/GMAIN.H"
#include "SRC/GEN/GFXCTX.H"
#include "structs.h"
#include "SRC/GAME/ENC/HOTSPOT.H"
#include "SRC/SYS/RAND.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/AUDIO/ENGINE/AUDIO.H"
#include "SRC/R3D/SCENE/WORLDHIT.H"
#include "SRC/R3D/ACTOR/ACTMOTN.H"
#include "SRC/R3D/CORE/R3D.H"
#include "SRC/COMBAT/GRID/CMBTGRID.H"
#include "SRC/DIALOG/EVTCOND.H"
#include "SRC/GAME/STATE/GSTATE.H"
#include "SRC/CHAR/STAT.H"
#include "SRC/R3D/VIS/VISLIST.H"
#include "SRC/R3D/VIS/PROXSCAN.H"
#include "SRC/DIALOG/DIALOG.H"
#include "SRC/SCREENS/TOWNSCN.H"
#include "SRC/COMBAT/ARENA/COMBAT.H"
#include "SRC/GAME/WORLD/WORLDLP.H"
#include "SRC/COMBAT/SPELL/SPELLFX.H"
#include "SRC/GAME/WORLD/MAP.H"
#include "SRC/GAME/WORLD/WORLDMOV.H"
#include "SRC/R3D/SCENE/CZONE.H"
#include "SRC/R3D/SCENE/ZONE.H"
#include "SRC/GAME/ENC/RGNENC.H"
#include <stdlib.h>

ZoneHotspot g_aZoneHotspots[10];
unsigned short g_awHotspotPendingFlags[10];

unsigned short g_nHotspotCount = 0x0000;
unsigned short g_wHotspotIterIdx = 0x0000;
unsigned short g_wHotspotMatchIdx = 0x0000;
unsigned short g_wHotspotEventEnabled = 0x0001;
unsigned short g_bZoneEntryInProgress = 0x0000;
unsigned short g_wHotspotIterCursor = 0x0000;
char g_monst_filename_buf[12] = "Tzzxxyy.DAT";
char g_aDefFileNames[12][13] = {
    0x64, 0x65, 0x66, 0x5f, 0x62, 0x6b, 0x67, 0x72, 0x2e, 0x64, 0x61, 0x74, 0x00, 0x64, 0x65, 0x66,
    0x5f, 0x63, 0x6f, 0x6d, 0x62, 0x2e, 0x64, 0x61, 0x74, 0x00, 0x64, 0x65, 0x66, 0x5f, 0x63, 0x6f,
    0x6d, 0x6d, 0x2e, 0x64, 0x61, 0x74, 0x00, 0x64, 0x65, 0x66, 0x5f, 0x64, 0x69, 0x61, 0x6c, 0x2e,
    0x64, 0x61, 0x74, 0x00, 0x64, 0x65, 0x66, 0x5f, 0x68, 0x65, 0x61, 0x6c, 0x2e, 0x64, 0x61, 0x74,
    0x00, 0x64, 0x65, 0x66, 0x5f, 0x73, 0x6f, 0x75, 0x6e, 0x2e, 0x64, 0x61, 0x74, 0x00, 0x64, 0x65,
    0x66, 0x5f, 0x74, 0x6f, 0x77, 0x6e, 0x2e, 0x64, 0x61, 0x74, 0x00, 0x64, 0x65, 0x66, 0x5f, 0x74,
    0x72, 0x61, 0x70, 0x2e, 0x64, 0x61, 0x74, 0x00, 0x64, 0x65, 0x66, 0x5f, 0x7a, 0x6f, 0x6e, 0x65,
    0x2e, 0x64, 0x61, 0x74, 0x00, 0x64, 0x65, 0x66, 0x5f, 0x64, 0x69, 0x73, 0x61, 0x2e, 0x64, 0x61,
    0x74, 0x00, 0x64, 0x65, 0x66, 0x5f, 0x65, 0x6e, 0x61, 0x62, 0x2e, 0x64, 0x61, 0x74, 0x00, 0x64,
    0x65, 0x66, 0x5f, 0x62, 0x6c, 0x6f, 0x63, 0x2e, 0x64, 0x61, 0x74, 0x00};
short g_aDefRecordSizes[12] = {21, 399, 10, 8, 13, 7, 21, 409, 19, 7, 7, 8};

void far hotspotevt_monst_load_tbl_cur_id(void) {
    ResFile *file;
    VisibleEntryList *p;

    p = g_apCombat_zone_actor_lists[0];
    g_monst_filename_buf[1] = 0x30 | p->bZone / 10;
    g_monst_filename_buf[2] = 0x30 | p->bZone % 10;
    g_monst_filename_buf[3] = 0x30 | p->bParty_x / 10;
    g_monst_filename_buf[4] = 0x30 | p->bParty_x % 10;
    g_monst_filename_buf[5] = 0x30 | p->bParty_y / 10;
    g_monst_filename_buf[6] = 0x30 | p->bParty_y % 10;
    file = res_fopen(g_monst_filename_buf, "rb");
    if (file != (ResFile *)0) {
        res_fseek(file, (u32)((g_gameState.nChapter - 1) * 0xc0), SEEK_CUR);
        res_fread(&g_nHotspotCount, 2, 1, file);
        res_fread(g_aZoneHotspots, 0x13, g_nHotspotCount, file);
        res_fclose(file);
    } else {
        g_nHotspotCount = 0;
    }
}

void hotspotevt_flags_clear(void) {
    unsigned short *pFlag;
    s16 i;

    pFlag = g_awHotspotPendingFlags;
    i = 0;
    if (i < (s16)g_nHotspotCount) {
        do {
            *pFlag = 0;
            i = i + 1;
            pFlag = pFlag + 1;
        } while (i < (s16)g_nHotspotCount);
    }
}

s16 hotspotevt_activate_at_player(void) {
    ZoneHotspot *evt;
    u16 *pOut_accepted;
    s16 allNoInteraction;
    s16 noInteraction;
    s16 ambushArmed;
    s16 ambushOut;

    evt = hotspotevt_find_at_player_tile(1);
    allNoInteraction = 1;
    ambushArmed = 0;
    hotspotevt_flags_clear();
    while (evt != (ZoneHotspot *)0) {
        pOut_accepted = (u16 *)&g_awHotspotPendingFlags[g_wHotspotMatchIdx];
        switch (evt->wKind) {
        case 0:
            hotspotevt_dialog_popup_run(evt, &noInteraction, (s16 *)pOut_accepted);
            break;
        case 1:
            if (ambushArmed != 0) {
                hotspotevt_skill_check_trigger(1, evt, &ambushOut, &noInteraction,
                                               (s16 *)pOut_accepted);
            } else {
                hotspotevt_skill_check_trigger(0, evt, &ambushOut, &noInteraction,
                                               (s16 *)pOut_accepted);
                ambushArmed |= ambushOut;
            }
            break;
        case 6:
            hotspotevt_show_record_message(evt, &noInteraction, (s16 *)pOut_accepted);
            break;
        case 7:
            if (ambushArmed != 0) {
                hotspotevt_trap_prefire_scout(1, evt, &ambushOut, &noInteraction,
                                              (s16 *)pOut_accepted);
            } else {
                hotspotevt_trap_prefire_scout(0, evt, &ambushOut, &noInteraction,
                                              (s16 *)pOut_accepted);
                ambushArmed |= ambushOut;
            }
            break;
        case 8:
            hotspotevt_action_try_enter_zone(evt, &noInteraction, pOut_accepted);
            break;
        case 11:
            hotspotevt_dlg_run_msg_event(evt, &noInteraction);
            break;
        default:
            noInteraction = *pOut_accepted = 1;
            break;
        }
        allNoInteraction &= noInteraction;
        evt = hotspotevt_find_at_player_tile(2);
    }
    return allNoInteraction;
}

unsigned short far hotspotevt_disp_pending_events(void) {
    s16 running;
    unsigned short wEventMask;
    unsigned short *pPendingFlag;
    unsigned short wHaltFlag;
    u16 eventBit;
    ZoneHotspot *evt;

    running = 1;
    wEventMask = 0;
    pPendingFlag = g_awHotspotPendingFlags;
    g_wHotspotMatchIdx = 0;
    while (running && (s16)g_wHotspotMatchIdx < (s16)g_nHotspotCount) {
        if (*pPendingFlag != 0) {
            evt = g_aZoneHotspots + g_wHotspotMatchIdx;
            switch (evt->wKind) {
            case 0:
                hotspotevt_load_record0_town(evt);
                eventBit = 1;
                goto done_mask;
            case 1:
                hotspotevt_type1_encounter_run(evt, (s16 *)&wHaltFlag);
                if (wHaltFlag != 0) {
                    running = 0;
                }
                eventBit = 0;
                break;
            case 3:
                eventBit = hotspotevt_monst_load_speak(evt);
                goto done_mask;
            case 5:
                hotspotevt_maybe_play_random_sfx(evt);
                eventBit = 0;
                break;
            case 6:
                hotspotevt_action_enter_town(evt);
                eventBit = 1;
                goto done_mask;
            case 7:
                hotspotevt_trap_main_fire(evt, &wHaltFlag);
                if (wHaltFlag != 0) {
                    running = 0;
                }
                eventBit = 0;
                break;
            case 8:
                hotspotevt_action_enter_zone(evt);
                running = 0;
                eventBit = 1;
                goto done_mask;
            case 9:
                hotspotevt_combat_play_sfx_voice(evt);
                eventBit = 0;
                break;
            case 10:
                hotspotevt_chance_trigger(evt);
                eventBit = 0;
                break;
            default:
                eventBit = 0;
                break;
            }
        done_mask:
            wEventMask |= eventBit;
        }
        g_wHotspotMatchIdx++;
        pPendingFlag++;
    }
    return wEventMask;
}

s16 hotspotevt_dispatch_at_point(s16 target_type, unsigned char x, unsigned char y, s16 *out_flag) {
    ZoneHotspot *hotspot;
    s16 result;
    s16 more;
    unsigned short statusFlag;
    u16 shouldFire;

    hotspot = hotspotevt_find_at_point(1, x, y);
    result = 0;
    more = 1;
    while (more != 0 && hotspot != (ZoneHotspot *)0x0) {
        if (hotspot->wKind == target_type) {
            switch (hotspot->wKind) {
            case 7:
                hotspotevt_trap_prefire_scout(0, hotspot, out_flag, (s16 *)&statusFlag,
                                              (s16 *)&shouldFire);
                if (shouldFire != 0) {
                    g_wHotspotEventEnabled = 0;
                    hotspotevt_trap_main_fire(hotspot, &statusFlag);
                    g_wHotspotEventEnabled = 1;
                    *out_flag = 1;
                }
            LAB_759a_036c:
                result = 1;
                break;
            case 8:
                hotspotevt_action_try_enter_zone(hotspot, (s16 *)&statusFlag, &shouldFire);
                if (shouldFire != 0) {
                    hotspotevt_action_enter_zone(hotspot);
                }
                *out_flag = shouldFire;
                goto LAB_759a_036c;
            }
            more = 0;
        }
        hotspot = hotspotevt_find_at_point(2, x, y);
    }
    return result;
}

s16 far hotspotevt_play_sound_zone_entry(s16 index) {
    ZoneHotspot *p;
    s16 count;
    s16 found;
    s16 running;
    char buf[409];

    p = hotspotevt_next_entry_19byte(1);
    count = 0;
    found = 0;
    running = 1;
    while (running && p != (ZoneHotspot *)0) {
        switch (p->wKind) {
        case 1:
            if (count == index) {
                hotspotevt_bak_load_indexed_rec(1, buf + 10, p->dwDef_record_offset);
                if (*(s32 *)(buf + 16) != 0) {
                    dialog_play_record(*(s32 *)(buf + 16), 1);
                }
                found = 1;
                running = 0;
            } else {
                count++;
            }
            break;
        case 7:
            if (count == index) {
                hotspotevt_bak_load_indexed_rec(7, buf, p->dwDef_record_offset);
                if (*(s32 *)(buf + 6) != 0) {
                    dialog_play_record(*(s32 *)(buf + 6), 1);
                }
                found = 1;
                running = 0;
            } else {
                count++;
            }
            break;
        }
        p = hotspotevt_next_entry_19byte(2);
    }
    return found;
}

s16 far hotspotevt_trap_gates_pass(s16 nIndex) {
    ZoneHotspot *pHotspot;
    s16 nCombatTrapIdx;
    s16 bGatesPass;
    s16 running;

    pHotspot = hotspotevt_next_entry_19byte(1);
    nCombatTrapIdx = 0;
    bGatesPass = 1;
    running = 1;

    while (running && pHotspot) {
        switch (pHotspot->wKind) {
        case 1:
            if (nCombatTrapIdx == nIndex) {
                if (pHotspot->wEvent_flag_pre2 == 0 ||
                    gstate_event_read(pHotspot->wEvent_flag_pre2) == 0) {
                    if (pHotspot->wEvent_flag_pre1 != 0) {
                        if (gstate_event_read(pHotspot->wEvent_flag_pre1) == 0)
                            goto fail;
                    }
                } else {
                fail:
                    bGatesPass = 0;
                }
                running = 0;
            } else {
                nCombatTrapIdx++;
            }
            break;
        case 7:
            if (nCombatTrapIdx == nIndex) {
                if (pHotspot->wEvent_flag_pre2 == 0 ||
                    gstate_event_read(pHotspot->wEvent_flag_pre2) == 0) {
                    if (pHotspot->wEvent_flag_pre1 != 0) {
                        if (gstate_event_read(pHotspot->wEvent_flag_pre1) == 0)
                            goto fail;
                    }
                } else {
                    goto fail;
                }
                running = 0;
            } else {
                nCombatTrapIdx++;
            }
            break;
        }
        pHotspot = hotspotevt_next_entry_19byte(2);
    }
    return bGatesPass;
}

s16 far hotspotevt_available(ZoneHotspot *pHotspot) {
    if (pHotspot == (ZoneHotspot *)0x0 || hotspotevt_done_read() != 0 ||
        hotspotevt_scout_tried_read() != 0 ||
        (pHotspot->wEvent_flag_pre2 != 0 && gstate_event_read(pHotspot->wEvent_flag_pre2) != 0) ||
        (pHotspot->wEvent_flag_pre1 != 0 && gstate_event_read(pHotspot->wEvent_flag_pre1) == 0)) {
        return 0;
    }
    return 1;
}

void far hotspotevt_dialog_popup_run(ZoneHotspot *pHotspot, s16 *out_skipped,
                                     s16 *out_user_confirmed) {
    unsigned char buf[22];

    if (hotspotevt_available(pHotspot) != 0) {
        hotspotevt_bak_load_indexed_rec(0, buf, pHotspot->dwDef_record_offset);
        *out_skipped = 0;
        if (*(u32 *)(buf + 6) != 0) {
            *out_user_confirmed = (dialog_play_record(*(u32 *)(buf + 6), 0) == 0);
        } else {
            *out_user_confirmed = 0;
        }
    } else {
        *out_skipped = 1;
        *out_user_confirmed = 0;
    }
}

void far hotspotevt_load_record0_town(ZoneHotspot *pHotspot) {
    char buf[21];

    hotspotevt_bak_load_indexed_rec(0, buf, pHotspot->dwDef_record_offset);
    if (buf[0x12] != '\0') {
        map_animate_camera_to_tile((TileMoveRecord *)(buf + 0xE));
    }
    townscene_main_loop(*(u16 *)(buf + 2), 1);
    if (pHotspot->wEvent_key_post != 0) {
        gstate_event_write(pHotspot->wEvent_key_post, 1);
    }
    if (!(s16)g_gameState.abTeleportRecord[0]) {
        g_nSceneReloadPending = 1;
        g_nMapReloadPending = 1;
    }
    return;
}

void far hotspotevt_skill_check_trigger(s16 isAuto, ZoneHotspot *pHotspot, s16 *out_a, s16 *out_b,
                                        s16 *out_c) {
    char buf[0x190];
    register s16 *pOutB = out_b;
    register s16 *pOutC = out_c;

    *out_a = 0;
    if (hotspotevt_done_read() != 0 ||
        (pHotspot->wEvent_flag_pre2 != 0 && gstate_event_read(pHotspot->wEvent_flag_pre2) != 0) ||
        (pHotspot->wEvent_flag_pre1 != 0 && gstate_event_read(pHotspot->wEvent_flag_pre1) == 0)) {
        *pOutC = 0;
        *pOutB = 1;
        return;
    }
    hotspotevt_bak_load_indexed_rec(1, buf, pHotspot->dwDef_record_offset);
    if (hotspotevt_enc_fought_read(*(u32 *)(buf + 2)) != 0) {
        *pOutC = 0;
        *pOutB = 1;
        return;
    }
    if (*(u16 *)(buf + 0x18d) & 1) {
        if (isAuto != 0) {
            *pOutC = 0;
            *pOutB = 1;
            return;
        }
        *out_a = 1;
        if (hotspotevt_scout_tried_read() == 0) {
            hotspotevt_scout_tried_set();
            if (RND(100) <= stat_party_find_extreme(0xe, 0, (short *)0x0)) {
                stat_party_broadcast_status_op(0xe, 1, 3);
                if (*(u32 *)(buf + 0xa) != 0)
                    dialog_play_record(*(u32 *)(buf + 0xa), 1);
                else
                    dialog_play_record(0x2fL, 1);
                hotspotevt_scouted_set();
                *pOutC = 0;
                *pOutB = 0;
                return;
            }
        }
    }
    *pOutC = 1;
    *pOutB = 1;
}

void far hotspotevt_type1_encounter_run(ZoneHotspot *pHotspot, s16 *out_moved) {
    s16 combatStatus;
    s16 hasFired;
    short landingHeading;
    s32 landingY;
    s32 landingX;
    unsigned char tileX;
    unsigned char tileY;
    s32 visitedTime;
    short origHeading;
    s16 facing;
    char buf[0x190];
    char worldCtr[12];

    u16 chance;
    s16 n;

    hasFired = 0;
    *out_moved = 0;
    hotspotevt_bak_load_indexed_rec(1, buf, pHotspot->dwDef_record_offset);
    if ((n = hotspotevt_monst_dispatch_by_tag(*(u32 *)(buf + 2))) == 0) {
        if ((*(u16 *)(buf + 0x18d) & 1) != 0) {
            if (hotspotevt_scouted_read() != 0)
                goto do_chance_check;
        }
        if ((*(u16 *)(buf + 0x18d) & 1) != 0)
            goto skip_event_check;
        if (spellfx_event_mask_test_bit(0) == 0)
            goto skip_event_check;
    do_chance_check:
        if ((n = worldmove_step_tick_get()) == 0) {
            return;
        }
        chance = stat_party_find_extreme(0xf, 0, (short *)0x0);
        g_gameState.nEvtArgActor0 = g_gameState.nEvtArgStat;
        if ((s16)chance < 0x5a) {
            chance = chance + (s16)(chance * 0x1e) / 100;
            if (0x5a < (s16)chance) {
                chance = 0x5a;
            }
        }
        if ((*(u16 *)(buf + 0x18d) & 1) != 0 && spellfx_event_mask_test_bit(0) != 0) {
            chance = chance + (s16)(100 - chance) / 2;
        }
        if (RND(100) <= chance) {
            stat_party_broadcast_status_op(0xf, 1, 3);
            return;
        }
    skip_event_check:;
    }
    if (g_game_mode == 2) {
        origHeading = g_world_camera->base.orientation.yaw;
        hotspotevt_tile_rect_world_ctr(&pHotspot->bbox.minX, (s32 *)worldCtr);
        facing =
            actormotion_atan2_long(*(s32 *)worldCtr - g_world_camera->base.pos.xy.nWorld_x,
                                   *(s32 *)(worldCtr + 4) - g_world_camera->base.pos.xy.nWorld_y);

        g_world_camera->base.orientation.yaw = (u16)(facing + R3D_DEG(45)) & 0xc000;
        if (combatgrid_tiles_over_thresh() == 0) {
            g_world_camera->base.orientation.yaw = origHeading;
            g_graphics_context.wGfxBlitDstPage = g_graphics_context.wVgaPage2Base;
            world_render_scene_dispatch(0);
            return;
        }
    } else {
        if (combatgrid_tiles_over_thresh() == 0) {
            return;
        }
    }
    gstate_temp_file_read_at(&visitedTime, GAM_ENC_VISITED_TIME(*(u32 *)(buf + 2)), 4);
    if ((g_gameState.game_time - visitedTime) / 0x1e <= 0x1e) {
        if (RND(100) <= stat_party_find_extreme(0xf, 0, (short *)0x0)) {
            stat_party_broadcast_status_op(0xf, 1, 3);
            hasFired = 1;
            g_gameState.nEvtArgCount = 0;
        } else {
            g_gameState.nEvtArgCount = 1;
        }
    } else {
        g_gameState.nEvtArgCount = 2;
    }
    g_gameState.nEvtArgAux1 = *(short *)(buf + 0x3b);
    if (*(u32 *)(buf + 6) != 0) {
        dialog_play_record(*(u32 *)(buf + 6), 1);
    }
    combat_arena_actor_turn_loop((unsigned short) * (u32 *)(buf + 2), &combatStatus, hasFired);
    g_pendingTempWriteRegion = TWR_ENCOUNTER_STATE;
    gstate_temp_file_write_at((unsigned char far *)&g_gameState.game_time,
                              GAM_ENC_FOUGHT_TIME(*(u32 *)(buf + 2)), 4);
    if (combatStatus == 2) {
        *out_moved = 1;
        switch (worldmove_aabb_outcode_rotated(&pHotspot->bbox.minX)) {
        case 1:
            *(GamePositionAndHeading *)&landingX = *(GamePositionAndHeading *)(buf + 0x12);
            break;
        case 2:
            *(GamePositionAndHeading *)&landingX = *(GamePositionAndHeading *)(buf + 0x1c);
            break;
        case 4:
            *(GamePositionAndHeading *)&landingX = *(GamePositionAndHeading *)(buf + 0x26);
            break;
        case 8:
            *(GamePositionAndHeading *)&landingX = *(GamePositionAndHeading *)(buf + 0x30);
            break;
        default:
            *(GamePositionAndHeading *)&landingX = *(GamePositionAndHeading *)(buf + 0x12);
            break;
        }
        czone_get_party_tile_xy(&tileX, &tileY);
        g_world_camera->base.pos.xy.nWorld_x = (u32)tileX * 64000 + landingX;
        g_world_camera->base.pos.xy.nWorld_y = (u32)tileY * 64000 + landingY;
        g_world_camera->base.orientation.yaw = landingHeading;
        proxscan_full(g_world_widget);
        vislist_sort(g_wVisibleEntrySegment, g_pwVisibleEntryOffsets, g_pVisibleEntryDistances,
                     g_nVisibleEntryCount);
        worldmove_camera_crossing_apply();
        worldloop_party_move_done_clr();
        goto LAB_759a_0a9c;
    }
    if (combatStatus == 1) {
        if (pHotspot->wEvent_key_post != 0) {
            gstate_event_write(pHotspot->wEvent_key_post, 1);
        }
        hotspotevt_done_set();
        hotspotevt_enc_fought_set(*(u32 *)(buf + 2));
        evtcond_dispatch_key_to_handler((unsigned short) * (u32 *)(buf + 2));
    }
LAB_759a_0a9c:
    if (combatStatus != 0 && combatStatus != 3) {
        g_nSceneReloadPending = 1;
        g_nMapReloadPending = 1;
    }
    return;
}

s16 far hotspotevt_monst_load_speak(ZoneHotspot *pHotspot) {
    char buf[8];

    if (hotspotevt_available(pHotspot)) {
        hotspotevt_bak_load_indexed_rec(3, buf, pHotspot->dwDef_record_offset);
        if (*(u32 *)(buf + 2) != 0) {
            dialog_play_record(*(u32 *)(buf + 2), 1);
        }
        if (pHotspot->wEvent_key_post != 0) {
            gstate_event_write(pHotspot->wEvent_key_post, 1);
        }
        if (pHotspot->wRepeat == 0) {
            if (pHotspot->bInhibitChapter != 0) {
                hotspotevt_done_set();
            }
            hotspotevt_scout_tried_set();
        }
        return 1;
    }
    return 0;
}

void far hotspotevt_maybe_play_random_sfx(ZoneHotspot *pHotspot) {
    char buf[8];

    if (hotspotevt_available(pHotspot)) {
        hotspotevt_bak_load_indexed_rec(5, buf, pHotspot->dwDef_record_offset);
        if (RND(100) <= (unsigned char)buf[2]) {
            audio_sfx_play_n_times(*(s16 *)(buf + 3), 0, 1);
            if (pHotspot->wEvent_key_post != 0) {
                gstate_event_write(pHotspot->wEvent_key_post, 1);
            }
        }
        if (pHotspot->bInhibitChapter != 0) {
            hotspotevt_done_set();
        }
    }
}

void far hotspotevt_show_record_message(ZoneHotspot *pHotspot, s16 *out_skipped,
                                        s16 *out_confirmed) {
    unsigned char buf[22];

    if (hotspotevt_available(pHotspot) != 0) {
        hotspotevt_bak_load_indexed_rec(6, buf, pHotspot->dwDef_record_offset);
        *out_skipped = 0;
        if (*(u32 *)(buf + 6) != 0) {
            *out_confirmed = (dialog_play_record(*(u32 *)(buf + 6), 0) == 0);
        } else {
            *out_confirmed = 0;
        }
    } else {
        *out_skipped = 1;
        *out_confirmed = 0;
    }
}

void far hotspotevt_action_enter_town(ZoneHotspot *evt) {
    char buf[21];

    hotspotevt_bak_load_indexed_rec(6, buf, evt->dwDef_record_offset);
    if (buf[0x12] != '\0') {
        map_animate_camera_to_tile((TileMoveRecord *)(buf + 0xE));
    }
    townscene_main_loop(*(u16 *)(buf + 2), 1);
    if (evt->wEvent_key_post != 0) {
        gstate_event_write(evt->wEvent_key_post, 1);
    }
    if (!(s16)g_gameState.abTeleportRecord[0]) {
        g_nSceneReloadPending = 1;
        g_nMapReloadPending = 1;
    }
    return;
}

void far hotspotevt_trap_prefire_scout(s16 nReentry, ZoneHotspot *evt, s16 *pOut_ambush_just_armed,
                                       s16 *pOut_handled, s16 *pOut_should_fire) {
    DefTrapRecord trapRec;
    register s16 *pOutHandled = pOut_handled;
    register s16 *pOutFire = pOut_should_fire;

    *pOut_ambush_just_armed = 0;
    if (hotspotevt_done_read() != 0 ||
        (evt->wEvent_flag_pre2 != 0 && gstate_event_read(evt->wEvent_flag_pre2) != 0) ||
        (evt->wEvent_flag_pre1 != 0 && gstate_event_read(evt->wEvent_flag_pre1) == 0)) {
        *pOutFire = 0;
        *pOutHandled = 1;
        return;
    }
    hotspotevt_bak_load_indexed_rec(7, &trapRec, evt->dwDef_record_offset);
    if (hotspotevt_enc_fought_read(trapRec.dwCombat_index) != 0) {
        *pOutFire = 0;
        *pOutHandled = 1;
        return;
    }
    if ((trapRec.wIs_ambush & 1) != 0) {
        if (nReentry != 0) {
            *pOutFire = 0;
            *pOutHandled = 1;
            return;
        }
        *pOut_ambush_just_armed = 1;
        if (hotspotevt_scout_tried_read() == 0) {
            hotspotevt_scout_tried_set();
            if (RND(100) <= stat_party_find_extreme(0xe, 0, (short *)0)) {
                stat_party_broadcast_status_op(0xe, 1L, 3);
                if (trapRec.dwScout_dialog != 0)
                    dialog_play_record(trapRec.dwScout_dialog, 1);
                else
                    dialog_play_record(0x2fL, 1);
                hotspotevt_scouted_set();
                *pOutFire = 0;
                *pOutHandled = 0;
                return;
            }
        }
    }
    *pOutFire = 1;
    *pOutHandled = 1;
}

typedef struct {
    s32 pos_x;
    s32 pos_y;
    s32 pos_z;
} CamPos;

void far hotspotevt_trap_main_fire(ZoneHotspot *pHotspot, unsigned short *pOut_status) {
    short wOrig_heading;
    s16 combat_status;
    s16 wHas_fired;
    GamePositionAndHeading landing_chosen;
    unsigned char tileX;
    unsigned char tileY;
    s32 visitedTime;
    DefTrapRecord trapRec;
    unsigned char originalCamera[12];

    *(CamPos far *)originalCamera = *(CamPos far *)&g_world_camera->base.pos.xy.nWorld_x;
    wOrig_heading = g_world_camera->base.orientation.yaw;
    wHas_fired = 0;
    *pOut_status = 0;

    hotspotevt_bak_load_indexed_rec(7, &trapRec, pHotspot->dwDef_record_offset);

    if (hotspotevt_monst_dispatch_by_tag(trapRec.dwCombat_index) == 0) {
        if (((trapRec.wIs_ambush & 1) != 0 && hotspotevt_scouted_read() != 0) ||
            ((trapRec.wIs_ambush & 1) == 0 && spellfx_event_mask_test_bit(0) != 0)) {
            if (g_wHotspotEventEnabled != 0) {
                u16 uStat;
                if (worldmove_step_tick_get() == 0) {
                    return;
                }
                uStat = stat_party_find_extreme(0xf, 0, (short *)0);
                g_gameState.nEvtArgActor0 = g_gameState.nEvtArgStat;
                if ((s16)uStat < 0x5a) {
                    uStat = uStat + (s16)(uStat * 0x1e) / 100;
                    if (0x5a < (s16)uStat) {
                        uStat = 0x5a;
                    }
                }
                if ((trapRec.wIs_ambush & 1) != 0 && spellfx_event_mask_test_bit(0) != 0)
                    uStat = uStat + (s16)(100 - uStat) / 2;
                if (RND(100) <= uStat) {
                    stat_party_broadcast_status_op(0xf, 1, 3);
                    return;
                }
            }
        }
    }

    czone_get_party_tile_xy(&tileX, &tileY);
    g_world_camera->base.pos.xy.nWorld_x =
        (s32)(s16)(u16)tileX * 64000 + trapRec.landing_primary.nX;
    g_world_camera->base.pos.xy.nWorld_y =
        (s32)(s16)(u16)tileY * 64000 + trapRec.landing_primary.nY;
    g_world_camera->base.orientation.yaw = trapRec.landing_primary.wHeading_raw;

    if (combatgrid_tiles_over_thresh() == 0) {
        *(CamPos far *)&g_world_camera->base.pos.xy.nWorld_x = *(CamPos far *)originalCamera;
        g_world_camera->base.orientation.yaw = wOrig_heading;
        return;
    }

    gstate_temp_file_read_at(&visitedTime, GAM_ENC_VISITED_TIME(trapRec.dwCombat_index), 4);

    if ((g_gameState.game_time - visitedTime) / 0x1e <= 0x1e) {
        if (RND(100) <= stat_party_find_extreme(0xf, 0, (short *)0)) {
            stat_party_broadcast_status_op(0xf, 1, 3);
            wHas_fired = 1;
            g_gameState.nEvtArgCount = 0;
        } else {
            g_gameState.nEvtArgCount = 1;
        }
    } else {
        g_gameState.nEvtArgCount = 2;
    }

    g_gameState.nEvtArgAux1 = trapRec.pCombatants[0].wMonster_index;

    if (trapRec.dwEntry_dialog != 0)
        dialog_play_record(trapRec.dwEntry_dialog, 1);

    combat_arena_actor_turn_loop((unsigned short)trapRec.dwCombat_index, &combat_status, wHas_fired);

    if (combat_status == 2) {
        *(CamPos far *)&g_world_camera->base.pos.xy.nWorld_x = *(CamPos far *)originalCamera;
        g_world_camera->base.orientation.yaw = wOrig_heading;
    }
    g_pendingTempWriteRegion = TWR_ENCOUNTER_STATE;
    gstate_temp_file_write_at((unsigned char far *)&g_gameState.game_time,
                              GAM_ENC_FOUGHT_TIME(trapRec.dwCombat_index), 4);

    if (combat_status == 2) {
        *pOut_status = 1;
        switch (worldmove_aabb_outcode_rotated(&pHotspot->bbox.minX) - 1) {
        case 0:
            *(GamePositionAndHeading far *)&landing_chosen =
                *(GamePositionAndHeading far *)&trapRec.landing_dir1;
            break;
        case 1:
            *(GamePositionAndHeading far *)&landing_chosen =
                *(GamePositionAndHeading far *)&trapRec.landing_dir2;
            break;
        case 3:
            *(GamePositionAndHeading far *)&landing_chosen =
                *(GamePositionAndHeading far *)&trapRec.landing_dir4;
            break;
        case 7:
            *(GamePositionAndHeading far *)&landing_chosen =
                *(GamePositionAndHeading far *)&trapRec.landing_dir8;
            break;
        default:
            *(GamePositionAndHeading far *)&landing_chosen =
                *(GamePositionAndHeading far *)&trapRec.landing_dir1;
            break;
        }
        czone_get_party_tile_xy(&tileX, &tileY);
        g_world_camera->base.pos.xy.nWorld_x = (u32)tileX * 64000 + landing_chosen.nX;
        g_world_camera->base.pos.xy.nWorld_y = (u32)tileY * 64000 + landing_chosen.nY;
        g_world_camera->base.orientation.yaw = landing_chosen.wHeading_raw;
    } else if (combat_status == 1) {
        if (pHotspot->wEvent_key_post != 0)
            gstate_event_write(pHotspot->wEvent_key_post, 1);
        hotspotevt_done_set();
        hotspotevt_enc_fought_set(trapRec.dwCombat_index);
        evtcond_dispatch_key_to_handler((unsigned short)trapRec.dwCombat_index);
    }

    if ((combat_status != 0) && (combat_status != 3)) {
        proxscan_full(g_world_widget);
        vislist_sort(g_wVisibleEntrySegment, g_pwVisibleEntryOffsets, g_pVisibleEntryDistances,
                     g_nVisibleEntryCount);
        worldmove_camera_crossing_apply();
        worldloop_party_move_done_clr();
        g_nSceneReloadPending = 1;
        g_nMapReloadPending = 1;
    }
}

void far hotspotevt_action_try_enter_zone(ZoneHotspot *evt, s16 *pOut_handled,
                                          u16 *pOut_accepted) {
    char buf[20];

    if (hotspotevt_available(evt) != 0) {
        hotspotevt_bak_load_indexed_rec(8, buf, evt->dwDef_record_offset);
        *pOut_handled = 0;
        if (((ZoneEntryRecord *)buf)->dwPromptDlgKey != 0) {
            *pOut_accepted =
                (u16)(dialog_play_record(((ZoneEntryRecord *)buf)->dwPromptDlgKey, 0) == 0);
            goto done;
        }
        *pOut_accepted = 0;
    } else {
        *pOut_handled = 1;
        *pOut_accepted = 0;
    }
done:
    if (*pOut_accepted != 0) {
        g_bZoneEntryInProgress = 1;
    }
    return;
}

void far hotspotevt_action_enter_zone(ZoneHotspot *evt) {
    char buf[19];

    hotspotevt_bak_load_indexed_rec(8, buf, evt->dwDef_record_offset);
    if (((ZoneEntryRecord *)buf)->dwEntryDlgKey != 0) {
        dialog_play_record(((ZoneEntryRecord *)buf)->dwEntryDlgKey, 0);
    }
    rgnenc_zone_rectr_save_objects();

    if (buf[2] != g_gameState.nZoneId) {
        zone_world_scene_teardown();
        zone_set_plr_pos_rec(&((ZoneEntryRecord *)buf)->spawn);
        zone_load();
    } else {
        zone_combat_camera_set_world_pos(&((ZoneEntryRecord *)buf)->spawn);
    }
    if (evt->wEvent_key_post != 0) {
        gstate_event_write(evt->wEvent_key_post, 1);
    }
    if (evt->bInhibitChapter != 0) {
        hotspotevt_done_set();
    }
    g_bZoneEntryInProgress = 0;
    dialog_input_wait_with_cooldown(2);
    return;
}

void far hotspotevt_combat_play_sfx_voice(ZoneHotspot *evt) {
    char buf[8];

    if (hotspotevt_available(evt) != 0) {
        hotspotevt_bak_load_indexed_rec(9, buf, evt->dwDef_record_offset);
        if (RND(100) <= (unsigned char)buf[2]) {
            if (*(unsigned short *)(buf + 3) != 0) {
                gstate_event_write(*(unsigned short *)(buf + 3), 0);
            }
        }
        if (evt->wEvent_key_post != 0) {
            gstate_event_write(evt->wEvent_key_post, 1);
        }
        if (evt->bInhibitChapter != 0) {
            hotspotevt_done_set();
        }
    }
}

void far hotspotevt_chance_trigger(ZoneHotspot *evt) {
    char buf[8];

    if (hotspotevt_available(evt) != 0) {
        hotspotevt_bak_load_indexed_rec(10, buf, evt->dwDef_record_offset);
        if (RND(100) <= (unsigned char)buf[2]) {
            if (*(unsigned short *)(buf + 3) != 0) {
                gstate_event_write(*(unsigned short *)(buf + 3), 1);
            }
        }
        if (evt->wEvent_key_post != 0) {
            gstate_event_write(evt->wEvent_key_post, 1);
        }
        if (evt->bInhibitChapter != 0) {
            hotspotevt_done_set();
        }
    }
    return;
}

void far hotspotevt_dlg_run_msg_event(ZoneHotspot *evt, s16 *out_status) {
    char buf[8];

    if (hotspotevt_available(evt) != 0) {
        hotspotevt_bak_load_indexed_rec(0xb, buf, evt->dwDef_record_offset);
        *out_status = 0;
        if (*(u32 *)(buf + 2) != 0) {
            dialog_play_record(*(u32 *)(buf + 2), 1);
        }
        if (evt->wEvent_key_post != 0) {
            gstate_event_write(evt->wEvent_key_post, 1);
        }
        if (evt->bInhibitChapter != 0) {
            hotspotevt_done_set();
        }
    } else {
        *out_status = 1;
    }
    return;
}

ZoneHotspot *far hotspotevt_find_at_player_tile(s16 mode) {
    unsigned char gridX;
    unsigned char gridY;

    czone_world_pos_to_grid_xy((char *)&gridX, (char *)&gridY);
    return hotspotevt_find_at_point(mode, gridX, gridY);
}

ZoneHotspot *hotspotevt_find_at_point(s16 mode, unsigned char x, unsigned char y) {
    ZoneHotspot *match;
    ZoneHotspot *cur;

    match = (ZoneHotspot *)0x0;
    if ((mode == 1) || (mode == 0)) {
        g_wHotspotIterIdx = 0;
    }
    cur = g_aZoneHotspots + g_wHotspotIterIdx;
    if (mode != 0) {
        for (; (match == (ZoneHotspot *)0x0 && ((s16)g_wHotspotIterIdx < (s16)g_nHotspotCount));) {
            if ((cur->bbox.minX <= x) &&
                (((x <= cur->bbox.maxX && (y <= cur->bbox.maxY)) && (cur->bbox.minY <= y)))) {
                match = cur;
                g_wHotspotMatchIdx = g_wHotspotIterIdx;
            }
            ++g_wHotspotIterIdx;
            cur = cur + 1;
        }
    }
    return match;
}

ZoneHotspot *hotspotevt_next_entry_19byte(s16 mode) {
    ZoneHotspot *entry;

    entry = (ZoneHotspot *)0x0;
    if ((mode == 1) || (mode == 0)) {
        g_wHotspotIterCursor = 0;
    }
    if ((mode != 0) && ((s16)g_wHotspotIterCursor < (s16)g_nHotspotCount)) {
        entry = g_aZoneHotspots + g_wHotspotIterCursor;
        ++g_wHotspotIterCursor;
    }
    return entry;
}

void hotspotevt_done_reset_all(void) {
    u16 i;

    /* Every zone's 400 hotspot flags. The original counts with a near pointer, whose + 200 is
       400 bytes on. */
    for (i = 0; i < 0x12c0; i++) {
        gstate_event_write(i + 400, 0);
    }
}

void far hotspotevt_done_set(void) {
    unsigned short slot;
    slot = (g_gameState.nZoneId - 1) * 400;
    slot += (u16)g_apCombat_zone_actor_lists[0]->bRef_pair_index * 10;
    slot += g_wHotspotMatchIdx;

    gstate_event_write(slot + 400, 1);
}

void far hotspotevt_done_clear(void) {
    unsigned short slot;
    slot = (g_gameState.nZoneId - 1) * 400;
    slot += (u16)g_apCombat_zone_actor_lists[0]->bRef_pair_index * 10;
    slot += g_wHotspotMatchIdx;
    gstate_event_write(slot + 400, 0);
}

s16 far hotspotevt_done_read(void) {
    unsigned short slot;
    slot = (g_gameState.nZoneId - 1) * 400;
    slot += (u16)g_apCombat_zone_actor_lists[0]->bRef_pair_index * 10;
    slot += g_wHotspotMatchIdx;
    return gstate_event_read(slot + 400);
}

void hotspotevt_scout_tried_clear_all(void) {
    u16 i = 0;

    do {
        gstate_event_write(HOTSPOT_SCOUT_TRIED(i), 0);
        i = i + 1;
    } while (i < 10);
    return;
}

void hotspotevt_scout_tried_set(void) {
    s16 base = (s16)g_wHotspotMatchIdx;
    gstate_event_write(HOTSPOT_SCOUT_TRIED(base), 1);
    return;
}

void hotspotevt_scout_tried_clear(void) {
    s16 base = (s16)g_wHotspotMatchIdx;
    gstate_event_write(HOTSPOT_SCOUT_TRIED(base), 0);
    return;
}

unsigned short far hotspotevt_scout_tried_read(void) {
    s16 key = g_wHotspotMatchIdx;
    return gstate_event_read(HOTSPOT_SCOUT_TRIED(key));
}

void hotspotevt_scouted_clear_all(void) {
    u16 i = 0;

    do {
        gstate_event_write(HOTSPOT_SCOUTED(i), 0);
        i = i + 1;
    } while (i < 10);
    return;
}

void hotspotevt_scouted_set(void) {
    s16 key = g_wHotspotMatchIdx;
    gstate_event_write(HOTSPOT_SCOUTED(key), 1);
    return;
}

void far hotspotevt_scouted_clear(void) {
    s16 key = g_wHotspotMatchIdx;
    gstate_event_write(HOTSPOT_SCOUTED(key), 0);
    return;
}

unsigned short far hotspotevt_scouted_read(void) {
    s16 key = g_wHotspotMatchIdx;
    return gstate_event_read(HOTSPOT_SCOUTED(key));
}

void hotspotevt_enc_fought_set(u32 key) {
    s16 lo = (s16)key;

    if ((s32)key < 1000) {
        gstate_event_write(ENCOUNTER_FOUGHT(lo), 1);
    }
    return;
}

void hotspotevt_enc_fought_clear(u32 key) {
    s16 lo = (s16)key;
    if ((s32)key < 1000) {
        gstate_event_write(ENCOUNTER_FOUGHT(lo), 0);
    }
    return;
}

unsigned short far hotspotevt_enc_fought_read(u32 key) {
    s16 lo = (s16)key;
    if ((s32)key < 1000) {
        return gstate_event_read(ENCOUNTER_FOUGHT(lo));
    }
    return 1;
}

void far hotspotevt_tile_rect_world_ctr(unsigned char *tile_rect, s32 *out_world_xy) {
    unsigned char chunk_x;
    unsigned char chunk_y;

    czone_get_party_tile_xy(&chunk_x, &chunk_y);
    *out_world_xy = (s32)(s16)(u16)chunk_x * 64000 +
                    ((s32)((u32)*tile_rect * 0x640 + ((u32)tile_rect[2] + 1) * 0x640) >> 1);
    out_world_xy[1] =
        (s32)(s16)(u16)chunk_y * 64000 +
        ((s32)((u32)tile_rect[3] * 0x640 + ((u32)tile_rect[1] + 1) * 0x640) >> 1);
}

s16 far hotspotevt_monst_dispatch_by_tag(s32 tag) {
    s16 result;

    result = 0;
    switch (tag) {
    case 0x97L:
    case 0x98L:
    case 0xebL:
    case 0xf5L:
    case 0x123L:
    case 0x125L:
    case 0x14fL:
    case 0x151L:
    case 0x152L:
    case 0x177L:
    case 0x19aL:
    case 0x1adL:
    case 0x1aeL:
        result = 1;
        break;
    }
    return result;
}

void hotspotevt_bak_indexed_rec_info(s16 record_id, char **out_filename, s16 *out_record_size) {
    *out_filename = g_aDefFileNames[record_id];
    *out_record_size = g_aDefRecordSizes[record_id];
    return;
}

char far hotspotevt_bak_load_indexed_rec(s16 record_id, void *dest, s32 offset) {
    char *filename;
    s16 record_size;
    char header[4];
    char present;
    ResFile *file;

    hotspotevt_bak_indexed_rec_info(record_id, &filename, &record_size);
    file = res_fopen(filename, "rb");
    res_fread(header, 4, 1, file);
    res_fseek(file, (u32)(u16)(record_size + 1) * offset, SEEK_CUR);
    res_fread(&present, 1, 1, file);
    if (present != '\0') {
        res_fread(dest, record_size, 1, file);
    }
    res_fclose(file);
    return present;
}
