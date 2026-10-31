#include "SRC/GAME/GMAIN.H"
#include "SRC/R3D/SCENE/ZONE.H"
#include "SRC/R3D/SCENE/PROXIM.H"
#include "SRC/R3D/ACTOR/ACTMOTN.H"
#include "structs.h"
#include "SRC/GAME/ENC/RGNENC.H"
#include "SRC/SYS/RAND.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/SYS/MEM.H"
#include "SRC/R3D/SPRITE/WORLDRND.H"
#include "SRC/R3D/CORE/R3D.H"
#include "SRC/R3D/ACTOR/ACTRENDR.H"
#include "SRC/GAME/STATE/GSTATE.H"
#include "SRC/GAME/ACTOR/ACTSPAWN.H"
#include "SRC/COMBAT/ACTOR/CACTOR.H"
#include "SRC/COMBAT/ENC/CBENC.H"
#include "SRC/GAME/WORLD/WORLDMOV.H"
#include "SRC/R3D/SCENE/CZONE.H"
#include "SRC/GAME/ENC/HOTSPOT.H"
#include "defines.h"

EncounterRecordTemplate *g_pEncounterRecordTemplates;
s32 g_anEncounterRecordIds[5];
WorldObject far *g_pFixed_object_entries;
WorldObject far *g_pVisible_entry_pool;
EncounterObjectState *g_pEncounterObjectState;
s16 g_nCombatTempZoneEncounterCount;
unsigned char g_bCombatTempZoneRefPair;

s16 g_nEncounter_record_count = 0;
s16 g_nFixed_object_count = 0;
s16 g_nVisible_entry_count = 0;
u16 *g_pZoneShapeIndex = {0};
u16 *g_pChapterShapeIds = {0};

void far rgnenc_alloc_3bufs_init(void) {
    WorldObject far *p;
    EncounterActorAux *pBuf;
    s16 i;

    p = g_pFixed_object_entries;

    g_pEncounterRecordTemplates = galloc_safe_zcalloc(0x69f);
    pBuf = galloc_safe_zcalloc(0x15e);
    i = 0;
    do {
        p->state.encounterAux = pBuf;
        pBuf = pBuf + 1;
        i = i + 1;
        p++;
    } while (i < 0x23);

    g_pEncounterObjectState = galloc_safe_zcalloc(0x1a4);
    g_nEncounter_record_count = g_nFixed_object_count = g_nVisible_entry_count = 0;
    rgnenc_world_objects_reset_spawn();
}

void rgnenc_free_3bufs_clear(void) {
    galloc_zfree(g_pEncounterObjectState);
    galloc_zfree(g_pFixed_object_entries->state.encounterAux);
    galloc_zfree(g_pEncounterRecordTemplates);
    g_nEncounter_record_count = g_nFixed_object_count = g_nVisible_entry_count = 0;
    return;
}

s16 far rgnenc_load_zone_shape_index(char *filename) {
    ResFile *fp;
    s16 i;

    if (g_pZoneShapeIndex != 0) {
        return 0;
    }

    g_pZoneShapeIndex = (u16 *)galloc_safe_zcalloc(0xa);
    fp = res_fopen(filename, "rb");
    res_fseek(fp, (s32)(((u16)g_gameState.nChapter - 1) << 3), SEEK_CUR);
    res_fread(g_pZoneShapeIndex, 2, 4, fp);
    res_fclose(fp);

    for (i = 4; i < 5; i++) {
        g_pZoneShapeIndex[i] = 0xffff;
    }

    for (i = 0; i < 4; i++) {
        if ((s16)g_pZoneShapeIndex[i] != -1) {
            combat_actor_bnames_load_cached(g_pZoneShapeIndex[i], 0);
        }
    }
    return 1;
}

s16 far rgnenc_rel_object_state_group(void) {
    s16 i;

    if (g_pZoneShapeIndex != (u16 *)0x0) {
        for (i = 3; i >= 0; i--) {
            if (g_pZoneShapeIndex[i] != 0xffff) {
                combat_actor_release_anim_images(g_pZoneShapeIndex[i]);
            }
        }
        galloc_zfree(g_pZoneShapeIndex);
        g_pZoneShapeIndex = (u16 *)0x0;
        return 1;
    }
    return 0;
}

void far rgnenc_savefile_init_35slot_tbl(void) {
    EncounterObjectState tbl[35];
    EncounterObjectState *p;
    s16 i;

    p = tbl;
    for (i = 0; i < 0x23; i++, p++) {
        if (i != 0)
            p->wKind_state = 0x100;
        else
            p->wKind_state = 0;
        p->pose.nWorld_x_offset = p->pose.nWorld_y_offset = 0;
        p->pose.nFacing = 0;
    }
    for (i = 0; i < 0x28; i++) {
        g_pendingTempWriteRegion = TWR_ENCOUNTER_STATE;
        gstate_temp_file_write_at((unsigned char far *)tbl,
                                  (u32)(u16)GAM_ENC_OBJ_STATE((u16)(i * 0x23)), 0x1a4);
    }
}

struct Type1BakRec {
    unsigned char hdr[2];
    s32 recId;
    unsigned char body[52];
    EncounterRecordTemplate tmpl;
    u16 flags;
};

struct Type7BakRec {
    unsigned char hdr[2];
    s32 recId;
    unsigned char body[62];
    EncounterRecordTemplate tmpl;
    u16 flags;
};

void far rgnenc_load_encounter_actors(void) {
    ZoneHotspot *pEvt;
    EncounterRecordTemplate *pTmpl;
    EncounterRecordTemplate *pTmplSrc;
    s16 *pActorDelta;
    EncounterObjectState *pObjState;
    WorldObject far *pSlot;
    s32 nRecordId;
    s32 nOriginX;
    s32 nOriginY;
    s16 nEncIdx;
    s16 nFlags;
    s16 bPlace;
    u16 nShapeCount;
    u16 aShapeIds[1];
    unsigned char bZoneY;
    unsigned char bZoneX;
    struct Type1BakRec type1_rec;
    struct Type7BakRec type7_rec;
    s16 aActorSlots[7];
    CombatantState inner;
    s16 j;
    EncounterActorAux *pAux;
    s16 v;
    s16 i;
    s16 k1;
    s16 k2;
    s16 k3;
    s16 k4;

    pEvt = hotspotevt_next_entry_19byte(1);
    pObjState = g_pEncounterObjectState;
    nEncIdx = (u16)g_apCombat_zone_actor_lists[0]->bRef_pair_index * 0x23;
    nShapeCount = 0;
    g_nEncounter_record_count = g_nFixed_object_count = 0;
    czone_get_party_tile_xy(&bZoneY, &bZoneX);
    nOriginX = (s32)(s16)(u16)bZoneY * 64000;
    nOriginY = (s32)(s16)(u16)bZoneX * 64000;
    gstate_temp_file_read_at(g_pEncounterObjectState, (u32)(u16)GAM_ENC_OBJ_STATE(nEncIdx), 0x1a4);
    if ((g_pEncounterObjectState->wKind_state & 0xff08) >> 8 == 0) {
        g_pEncounterObjectState->wKind_state = 0x100;
        while (pEvt != (ZoneHotspot *)0) {
            if (pEvt->wKind == 1 || pEvt->wKind == 7) {
                if (pEvt->wKind == 1) {
                    hotspotevt_bak_load_indexed_rec(1, &type1_rec, pEvt->dwDef_record_offset);
                    nRecordId = type1_rec.recId;
                } else {
                    hotspotevt_bak_load_indexed_rec(7, &type7_rec, pEvt->dwDef_record_offset);
                    nRecordId = type7_rec.recId;
                }
                gstate_temp_file_read_at(aActorSlots, GAM_ENC_ROSTER(nRecordId), 0xe);
                j = 0;
                while (j < 7) {
                    if (aActorSlots[j] != -1) {
                        gstate_temp_file_read_at(&inner, GAM_COMBAT_ACTOR_INNER((s32)aActorSlots[j]), 0x16);
                        /* The flags byte is read through (int)(signed char)
                           so the compiler sign-extends it (MOV AL;CWDE) and TESTs
                           the word, rather than folding to a byte-ptr TEST. The
                           result is never used beyond the mask; v is a scratch.
                           See rgnenc_mark_defended for the same idiom. */
                        if (((v = (s16)(signed char)inner.flags) & CAF_DEAD) == 0) {
                            pObjState->wKind_state = 0x200;
                        }
                    }
                    j++;
                    pObjState++;
                }
            }
            pEvt = hotspotevt_next_entry_19byte(2);
        }
    }

    for (pEvt = hotspotevt_next_entry_19byte(1); pEvt != (ZoneHotspot *)0;
         pEvt = hotspotevt_next_entry_19byte(2)) {
        if (pEvt->wKind == 1 || pEvt->wKind == 7) {
            if (pEvt->wKind == 1) {
                hotspotevt_bak_load_indexed_rec(1, &type1_rec, pEvt->dwDef_record_offset);
                pTmplSrc = &type1_rec.tmpl;
                nRecordId = type1_rec.recId;
                nFlags = type1_rec.flags & 1;
            } else {
                hotspotevt_bak_load_indexed_rec(7, &type7_rec, pEvt->dwDef_record_offset);
                pTmplSrc = &type7_rec.tmpl;
                nRecordId = type7_rec.recId;
                nFlags = type7_rec.flags & 1;
            }
            if (g_nEncounter_record_count < 5) {
                nEncIdx = g_nEncounter_record_count;
                g_anEncounterRecordIds[g_nEncounter_record_count] = nRecordId;

                pTmpl = g_pEncounterRecordTemplates + g_nEncounter_record_count;
                *pTmpl = *pTmplSrc;
                pActorDelta = &pTmpl->pActors[0].nPaged_record_delta;
                pSlot = g_pFixed_object_entries + g_nFixed_object_count;
                pObjState = g_pEncounterObjectState + nEncIdx * 7;
                for (j = 0;
                     g_nFixed_object_count < 0x23 && j < (s16)(u16)pTmpl->pActors[0].kind && j < 7;
                     pActorDelta += 0x18, pObjState++) {
                    u16 uKind = (pObjState->wKind_state & 0xff08) >> 8;
                    if (uKind == 4 || nFlags == 0) {
                        bPlace = 1;
                        switch (uKind) {
                        case 2:
                            pSlot->pos.xy.nWorld_x = nOriginX + *(s32 *)((char *)pActorDelta + 4);
                            pSlot->pos.xy.nWorld_y = nOriginY + *(s32 *)((char *)pActorDelta + 8);
                            pSlot->orientation.yaw = *(s16 *)((char *)pActorDelta + 12);
                            pObjState->wKind_state = 0x300;
                            pObjState->wKind_state |= RND(3);
                            if (RND2(2))
                                pObjState->wKind_state |= 4;
                            break;
                        case 3:
                        case 4:
                            pSlot->pos.xy.nWorld_x = nOriginX + pObjState->pose.nWorld_x_offset;
                            pSlot->pos.xy.nWorld_y = nOriginY + pObjState->pose.nWorld_y_offset;
                            pSlot->orientation.yaw = pObjState->pose.nFacing;
                            break;
                        default:
                            bPlace = 0;
                            break;
                        }
                        if (bPlace) {
                            pSlot->shapeId = g_nProximityTableCount;
                            pSlot->pos.nWorld_z = 0;
                            pSlot->orientation.pitch = pSlot->orientation.roll = 0;
                            pAux = pSlot->state.encounterAux;
                            k1 = 0;
                            while (k1 < 10) {
                                *(unsigned char *)pAux = 0xff;
                                k1++;
                                pAux = (EncounterActorAux *)((unsigned char *)pAux + 1);
                            }
                            pSlot->state.encounterAux->nRecord_idx = g_nEncounter_record_count;
                            pSlot->state.encounterAux->nSlot_idx = j;
                            pSlot->state.encounterAux->nEncounter_idx = nEncIdx * 7 + j;
                            for (k2 = 0; k2 < 4; k2++) {
                                *(s32 *)((char *)pActorDelta + k2 * 4 + 14) += nOriginX;
                                *(s32 *)((char *)pActorDelta + k2 * 4 + 30) += nOriginY;
                            }
                            if (*pActorDelta != 0) {
                                for (k3 = 0; k3 < 4; k3++) {
                                    if (g_pZoneShapeIndex[k3] == *pActorDelta)
                                        break;
                                }
                                if (k3 == 4) {
                                    for (k4 = 0; k4 < (s16)nShapeCount; k4++) {
                                        if (aShapeIds[k4] == *pActorDelta)
                                            break;
                                    }
                                    if (k4 == (s16)nShapeCount && (s16)nShapeCount < 1) {
                                        aShapeIds[nShapeCount] = *pActorDelta;
                                        nShapeCount++;
                                    }
                                }
                            }
                            g_nFixed_object_count++;
                            pSlot++;
                        }
                    }
                    j++;
                }
                g_nEncounter_record_count++;
            }
        }
    }

    for (j = 4; j >= 4; j--) {
        if (*(s16 *)&g_pZoneShapeIndex[j] != -1) {
            combat_actor_release_anim_images(g_pZoneShapeIndex[j]);
        }
    }
    for (j = 0; j < 1; j++) {
        if (j < (s16)nShapeCount) {
            g_pZoneShapeIndex[j + 4] = aShapeIds[j];
            combat_actor_bnames_load_cached(aShapeIds[j], 0);
        } else {
            g_pZoneShapeIndex[j + 4] = 0xffff;
        }
    }
}

void far rgnenc_release_anim_images_slot4(void) {
    s16 i;

    for (i = 4; i >= 4; i--) {
        if (g_pZoneShapeIndex[i] != 0xffff) {
            combat_actor_release_anim_images(g_pZoneShapeIndex[i]);
            g_pZoneShapeIndex[i] = 0xffff;
        }
    }
    return;
}

void far rgnenc_zone_rectr_save_objects(void) {
    s16 zone_rec;
    EncounterObjectState *pState_w;
    WorldObject far *pObj;
    s32 dx;
    s32 dy;
    EncounterObjectState empty1;
    EncounterObjectState empty3;
    EncounterObjectState table[35];
    EncounterObjectState *p;
    s16 i;
    u16 kind;

    zone_rec = g_apCombat_zone_actor_lists[0]->bRef_pair_index * 0x23;
    pState_w = g_pEncounterObjectState;
    pObj = g_pFixed_object_entries;
    dx = (u32)g_apCombat_zone_actor_lists[0]->bParty_x * 0xfa00;
    dy = (u32)g_apCombat_zone_actor_lists[0]->bParty_y * 0xfa00;
    for (i = 0; i < g_nFixed_object_count; i++, pObj++) {
        p = g_pEncounterObjectState + pObj->state.encounterAux->nEncounter_idx;
        if (((p->wKind_state & 0xff08) >> 8) == 4) {
            p->pose.nWorld_x_offset = pObj->pos.xy.nWorld_x - dx;
            p->pose.nWorld_y_offset = pObj->pos.xy.nWorld_y - dy;
            p->pose.nFacing = pObj->orientation.yaw;
        }
    }
    empty1.pose.nWorld_x_offset = empty1.pose.nWorld_y_offset = 0;
    empty1.pose.nFacing = 0;
    empty1.wKind_state = 0x100;
    empty3.pose.nWorld_x_offset = empty3.pose.nWorld_y_offset = 0;
    empty3.pose.nFacing = 0;
    empty3.wKind_state = 0x200;
    p = table;
    for (i = 0; i < 0x23; i++, pState_w++, p++) {
        kind = (pState_w->wKind_state & 0xff08) >> 8;
        if (kind == 3)
            *p = empty3;
        else if (kind == 4)
            *p = *pState_w;
        else
            *p = empty1;
    }
    g_pendingTempWriteRegion = TWR_ENCOUNTER_STATE;
    gstate_temp_file_write_at((unsigned char far *)table, (u32)(u16)GAM_ENC_OBJ_STATE(zone_rec),
                              0x1a4);
}

void far rgnenc_persist_zone_snapshot(void) {
    s16 record_base;
    WorldObject far *pCurEntry;
    s32 party_x_off;
    s32 party_y_off;
    s16 nFixedIdx;
    EncounterObjectState *pState;
    u16 kind;

    record_base = g_apCombat_zone_actor_lists[0]->bRef_pair_index * 0x23;
    pCurEntry = g_pFixed_object_entries;
    party_x_off = (u32)g_apCombat_zone_actor_lists[0]->bParty_x * 0xfa00;
    party_y_off = (u32)g_apCombat_zone_actor_lists[0]->bParty_y * 0xfa00;
    for (nFixedIdx = 0; nFixedIdx < g_nFixed_object_count; nFixedIdx++, pCurEntry++) {
        pState = g_pEncounterObjectState + pCurEntry->state.encounterAux->nEncounter_idx;
        kind = (pState->wKind_state & 0xff08) >> 8;
        if (kind == 3 || kind == 4) {
            pState->pose.nWorld_x_offset = pCurEntry->pos.xy.nWorld_x - party_x_off;
            pState->pose.nWorld_y_offset = pCurEntry->pos.xy.nWorld_y - party_y_off;
            pState->pose.nFacing = pCurEntry->orientation.yaw;
        }
    }
    g_pendingTempWriteRegion = TWR_ENCOUNTER_STATE;
    gstate_temp_file_write_at((unsigned char far *)g_pEncounterObjectState,
                              (u32)(u16)GAM_ENC_OBJ_STATE(record_base), 0x1a4);
    g_nCombatTempZoneEncounterCount = g_nEncounter_record_count;
    g_bCombatTempZoneRefPair = g_apCombat_zone_actor_lists[0]->bRef_pair_index;
}

s16 far rgnenc_persist_actor_placed(s32 record_id, s16 slot_index,
                                    EncounterObjectState *src_partial) {
    s16 nRecordIdx;
    s16 nFileIdx;
    EncounterObjectState record;

    for (nRecordIdx = 0; nRecordIdx < g_nCombatTempZoneEncounterCount; nRecordIdx++) {
        if (g_anEncounterRecordIds[nRecordIdx] == record_id) {
            nFileIdx = (u16)g_bCombatTempZoneRefPair * 0x23 + nRecordIdx * 7 + slot_index;
            if (g_game_mode == 2)
                gstate_temp_file_read_at(&record, (u32)(u16)GAM_ENC_OBJ_STATE(nFileIdx), 0xc);
            else
                record.pose = src_partial->pose;
            record.wKind_state = 0x400;
            g_pendingTempWriteRegion = TWR_ENCOUNTER_STATE;
            gstate_temp_file_write_at((unsigned char far *)&record,
                                      (u32)(u16)GAM_ENC_OBJ_STATE(nFileIdx), 0xc);
            return 1;
        }
    }
    return 0;
}

s16 far rgnenc_persist_actor_removed(s32 record_id, s16 slot_index) {
    s16 nRecordIdx;
    s16 nFileIdx;
    EncounterObjectState removalRecord;

    for (nRecordIdx = 0; nRecordIdx < g_nCombatTempZoneEncounterCount; nRecordIdx++) {
        if (g_anEncounterRecordIds[nRecordIdx] == record_id) {
            nFileIdx = (u16)g_bCombatTempZoneRefPair * 0x23 + nRecordIdx * 7 + slot_index;
            removalRecord.pose.nWorld_x_offset = removalRecord.pose.nWorld_y_offset = 0;
            removalRecord.pose.nFacing = 0;
            removalRecord.wKind_state = 0x100;
            g_pendingTempWriteRegion = TWR_ENCOUNTER_STATE;
            gstate_temp_file_write_at((unsigned char far *)&removalRecord,
                                      (u32)(u16)GAM_ENC_OBJ_STATE(nFileIdx), 0xc);
            return 1;
        }
    }
    return 0;
}

void far rgnenc_reset_and_save(void) {
    s16 record_base;
    WorldObject far *pCurEntry;
    s16 nFixedIdx;
    EncounterObjectState *pState;

    record_base = g_apCombat_zone_actor_lists[0]->bRef_pair_index * 0x23;
    pCurEntry = g_pFixed_object_entries;
    for (nFixedIdx = 0; nFixedIdx < g_nFixed_object_count; nFixedIdx++, pCurEntry++) {
        pState = g_pEncounterObjectState + pCurEntry->state.encounterAux->nEncounter_idx;
        if ((pState->wKind_state & 0xff08) >> 8 == 3) {
            pState->pose.nWorld_x_offset = pState->pose.nWorld_y_offset = 0;
            pState->pose.nFacing = 0;
            pState->wKind_state = 0x200;
        }
    }
    g_pendingTempWriteRegion = TWR_ENCOUNTER_STATE;
    gstate_temp_file_write_at((unsigned char far *)g_pEncounterObjectState,
                              (u32)(u16)GAM_ENC_OBJ_STATE(record_base), 0x1a4);
    rgnenc_load_encounter_actors();
}

void far rgnenc_mark_defended(u32 filter_encounter_id) {
    s16 i, j, base;
    u32 enc_id;
    u16 kind_hi;
    s16 v;
    s16 actor_ids[7];
    CombatantState inner;

    for (i = 0; i < g_nEncounter_record_count; i++) {
        enc_id = g_anEncounterRecordIds[i];
        if (filter_encounter_id != 0 && filter_encounter_id != enc_id)
            continue;

        hotspotevt_enc_fought_set(enc_id);

        base = i * 7;
        for (j = 0; j < 7; j++) {
            kind_hi = (g_pEncounterObjectState[base + j].wKind_state & 0xff08) >> 8;
            if (kind_hi == 3)
                g_pEncounterObjectState[base + j].wKind_state = 0x400;
        }

        gstate_temp_file_read_at(actor_ids, GAM_ENC_ROSTER(enc_id), 0xe);

        for (j = 0; j < 7; j++) {
            if (actor_ids[j] == -1)
                continue;
            gstate_temp_file_read_at(&inner, GAM_COMBAT_ACTOR_INNER((s32)actor_ids[j]), 0x16);
            /* The flags byte is read through (int)(signed char)
               so the compiler sign-extends it (MOV AL;CWDE) and TESTs
               the word, rather than folding to a byte-ptr TEST. The
               result is never used beyond the mask; v is a scratch.
               See rgnenc_load_encounter_actors for the same idiom. */
            if ((v = (s16)(signed char)inner.flags) & CAF_DEAD)
                continue;
            inner.flags |= CAF_DEAD;
            g_pendingTempWriteRegion = TWR_COMBAT_ACTOR_INNER;
            gstate_temp_file_write_at((unsigned char far *)&inner,
                                      GAM_COMBAT_ACTOR_INNER((s32)actor_ids[j]), 0x16);
        }
    }
}

void far rgnenc_complete_consume(u32 encounter_id) {
    s16 hit_count;
    s16 bSearching;
    u16 saved_match_idx;
    ZoneHotspot *sub_entry;
    s16 key_index;

    hit_count = 0;
    bSearching = 1;
    for (key_index = 0; bSearching && key_index < g_nEncounter_record_count; key_index++) {
        if (g_anEncounterRecordIds[key_index] == encounter_id) {
            sub_entry = hotspotevt_next_entry_19byte(1);
            while (bSearching && sub_entry != (ZoneHotspot *)0) {
                if (sub_entry->wKind == 1 || sub_entry->wKind == 7) {
                    if (hit_count == key_index) {
                        saved_match_idx = g_wHotspotMatchIdx;
                        g_wHotspotMatchIdx = g_wHotspotIterCursor - 1;
                        hotspotevt_done_clear();
                        hotspotevt_scout_tried_clear();
                        hotspotevt_scouted_clear();
                        g_wHotspotMatchIdx = saved_match_idx;
                        hotspotevt_enc_fought_clear(encounter_id);
                        combatenc_chap_load_party_grn((s16)encounter_id);
                        bSearching = 0;
                    } else {
                        hit_count++;
                    }
                }
                sub_entry = hotspotevt_next_entry_19byte(2);
            }
        }
    }
}

void far rgnenc_render_object(WorldObject far *entry, s16 bAnimTick) {
    s16 record_idx;
    s16 actor_idx;
    s16 state_idx;
    s16 dx;
    s16 dy;
    unsigned char animFrame;
    unsigned char animToggle;
    char entityKind;
    ImageRecord **sprite_table;
    EncounterObjectState *pState;
    unsigned char spriteDir;
    s16 i;
    unsigned char *state_bytes;
    s16 *pRecordDelta;
    ImageRecord **saved_table;

    record_idx = *(s16 *)(entry->state.stateBits + 2);
    actor_idx = *(s16 *)(entry->state.stateBits + 4);
    state_idx = *(s16 *)(entry->state.stateBits + 6);
    pState = g_pEncounterObjectState + state_idx;
    state_bytes = (unsigned char *)entry->state.stateBits;
    animFrame = (unsigned char)pState->wKind_state & 3;
    animToggle = (unsigned char)pState->wKind_state & 4;
    entityKind = (char)((pState->wKind_state & 0xff08) >> 8);
    if (entityKind != 3 && entityKind != 4)
        return;
    i = 0;
    do {
        *state_bytes = 0xff;
        i++;
        state_bytes++;
    } while (i < 10);

    g_nActorSpriteFlip = 0;
    dx = g_world_camera->base.pos.xy.nWorld_x - entry->pos.xy.nWorld_x;
    dy = g_world_camera->base.pos.xy.nWorld_y - entry->pos.xy.nWorld_y;
    spriteDir =
        (unsigned char)(~((r3d_tbl_atan2(dx, dy) - 0x4000) - entry->orientation.yaw) + 0x1000U >>
                        0xd);
    if (entityKind == 3) {
        switch (spriteDir) {
        case 1:
            spriteDir = 3;
            break;
        case 2:
            spriteDir = 6;
            break;
        case 3:
            spriteDir = 9;
            break;
        case 4:
            spriteDir = 0xc;
            break;
        case 5:
            g_nActorSpriteFlip = 2;
            spriteDir = 9;
            break;
        case 6:
            g_nActorSpriteFlip = 2;
            break;
        case 7:
            g_nActorSpriteFlip = 2;
            spriteDir = 3;
            break;
        }
        if (bAnimTick != 0) {
            if (animToggle != 0) {
                if (animFrame < 2) {
                    animFrame++;
                } else {
                    animToggle = 0;
                    animFrame--;
                }
            } else {
                if (animFrame != 0) {
                    animFrame--;
                } else {
                    animToggle = 1;
                    animFrame++;
                }
            }
            pState->wKind_state = (unsigned char)entityKind << 8;
            pState->wKind_state |= animFrame;
            if (animToggle != 0)
                pState->wKind_state |= 4;
        }
        *(char *)entry->state.stateBits = spriteDir + animFrame;
    } else {
        switch ((s16)(u16)spriteDir >> 1) {
        case 0:
            spriteDir = 3;
            break;
        case 1:
            spriteDir = 7;
            break;
        case 2:
            spriteDir = 0xb;
            break;
        case 3:
            g_nActorSpriteFlip = 2;
            spriteDir = 7;
            break;
        }
        *(unsigned char *)(entry->state.stateBits + 1) = spriteDir;
    }
    pRecordDelta = &g_pEncounterRecordTemplates[record_idx].pActors[actor_idx].nPaged_record_delta;
    sprite_table = combat_actor_rsrc_cache_val(*pRecordDelta, 0);
    if (sprite_table != 0) {
        saved_table = worldrender_table_swap(0, sprite_table);
        entry->shapeId = entry->shapeId + *pRecordDelta;
        actorrender_entity(entry);
        entry->shapeId = entry->shapeId - *pRecordDelta;
        worldrender_table_swap(0, saved_table);
    }
    g_nActorSpriteFlip = 0;
    *(s16 *)(entry->state.stateBits + 2) = record_idx;
    *(s16 *)(entry->state.stateBits + 4) = actor_idx;
    *(s16 *)(entry->state.stateBits + 6) = state_idx;
}

void far rgnenc_corpse_tbl_iterate_22byte(void) {
    WorldObject far *slot;
    s16 nRecord_idx;
    s16 nSlot_idx;
    s16 nEncounter_idx;
    s16 i;
    s16 facing_delta;
    s16 out_target;
    s16 nTemplate_off;
    EncounterObjectState *pState;
    s16 out_dir;
    WorldPos pos_buf;
    char *pActorPoly;
    s16 vertex_count;

    slot = g_pFixed_object_entries;
    for (i = 0; i < g_nFixed_object_count; i++, slot++) {
        nRecord_idx = slot->state.encounterAux->nRecord_idx;
        nSlot_idx = slot->state.encounterAux->nSlot_idx;
        nEncounter_idx = slot->state.encounterAux->nEncounter_idx;

        nTemplate_off = (s16)(g_pEncounterRecordTemplates + nRecord_idx);
        pActorPoly = (char *)nTemplate_off + nSlot_idx * 48 + 1;

        pState = g_pEncounterObjectState + nEncounter_idx;

        if ((pState->wKind_state & 0xff08) >> 8 == 3) {

            switch (*(s16 *)(pActorPoly + 2)) {
            case 1:
                vertex_count = 2;
                facing_delta = R3D_UDEG(180);
                break;
            case 2:
                vertex_count = 4;
                facing_delta = R3D_UDEG(270);
                break;
            case 3:
                vertex_count = 4;
                facing_delta = R3D_DEG(90);
                break;
            }

            switch (*(s16 *)(pActorPoly + 2)) {
            case 1:
            case 2:
            case 3:
                pos_buf = *(WorldPos far *)&slot->pos.xy.nWorld_x;
                worldmove_crossing_apply_offset(&pos_buf, slot->orientation.yaw, 0x190);
                *(WorldPos far *)&slot->pos.xy.nWorld_x = pos_buf;
                {
                    s16 dx;
                    for (dx = 0; dx < vertex_count; dx++) {
                        if (*(s32 *)(pActorPoly + 0xe + dx * 4) == pos_buf.xy.nWorld_x &&
                            *(s32 *)(pActorPoly + 0x1e + dx * 4) == pos_buf.xy.nWorld_y) {
                            slot->orientation.yaw += (s16)facing_delta;
                            break;
                        }
                    }
                }
                continue;
            case 4:
                pos_buf = *(WorldPos far *)&slot->pos.xy.nWorld_x;
                if (worldmove_crossing_check_8dir(&pos_buf, slot->orientation.yaw, 0x190L, 1,
                                                  &out_dir, &out_target)) {
                    s16 dx;
                    *(WorldPos far *)&slot->pos.xy.nWorld_x = pos_buf;
                    for (dx = 0; dx < 2; dx++) {
                        if (*(s32 *)(pActorPoly + 0xe + dx * 4) == pos_buf.xy.nWorld_x &&
                            *(s32 *)(pActorPoly + 0x1e + dx * 4) == pos_buf.xy.nWorld_y) {
                            slot->orientation.yaw =
                                (s16)((u16)slot->orientation.yaw + R3D_UDEG(180));
                            break;
                        }
                    }
                    if (dx == 2) {
                        if (out_dir == 2 || out_dir == 3) {
                            slot->orientation.yaw = (s16)out_target;
                        }
                    }
                } else {
                    slot->orientation.yaw =
                        (s16)((u16)slot->orientation.yaw + R3D_UDEG(180));
                }
                continue;
            }
        }
    }
}

s16 far rgnenc_slot_actor_kind_eq_placed(WorldObject far *pSlot) {
    s16 idx = pSlot->state.encounterAux->nEncounter_idx;
    s16 state = (g_pEncounterObjectState[idx].wKind_state & 0xff08) >> 8;
    return state == 3;
}

void far rgnenc_world_objects_reset_spawn(void) {
    g_nVisible_entry_count = 0;
    actorspawn_for_location_by_time((u16)g_gameState.nZoneId);
}

s16 far rgnenc_visible_pool_append_spawn(unsigned char far *pEntry) {
    WorldObject far *cursor;

    if (g_nVisible_entry_count < 0x5d) {
        if (g_gameState.nZoneId != 7 || *(s16 far *)(pEntry + 2) != 0xdd ||
            g_gameState.nChapter == 7) {
            cursor = (WorldObject far *)g_pVisible_entry_pool + g_nVisible_entry_count;
            g_nVisible_entry_count++;
            cursor->shapeId = *(s16 far *)(pEntry + 2);
            cursor->pos.xy.nWorld_x = *(s32 far *)(pEntry + 4);
            cursor->pos.xy.nWorld_y = *(s32 far *)(pEntry + 8);
            cursor->pos.nWorld_z = 0;
            cursor->orientation.pitch = cursor->orientation.roll = cursor->orientation.yaw = 0;
            cursor->state.stateBits = 0;
            if (g_gameState.nZoneId == 7 && *(s16 far *)(pEntry + 2) == 0xdd)
                cursor->orientation.yaw = 0xc200;
            return 1;
        }
    }
    return 0;
}

void far rgnenc_vis_pool_remove_matching(unsigned char far *pKey) {
    s16 i;
    WorldObject far *cursor;
    WorldObject far *last;

    cursor = (WorldObject far *)g_pVisible_entry_pool;
    if (*pKey != g_gameState.nZoneId)
        return;
    for (i = 0; i < g_nVisible_entry_count; i++, cursor++) {
        if (cursor->shapeId == *(s16 far *)(pKey + 2) &&
            cursor->pos.xy.nWorld_x == *(s32 far *)(pKey + 4) &&
            cursor->pos.xy.nWorld_y == *(s32 far *)(pKey + 8))
            break;
    }
    if (i < g_nVisible_entry_count) {
        g_nVisible_entry_count--;
        last = (WorldObject far *)g_pVisible_entry_pool + g_nVisible_entry_count;
        cursor->shapeId = last->shapeId;
        cursor->pos = last->pos;
        cursor->orientation = last->orientation;
        cursor->state.stateBits = last->state.stateBits;
    }
}

s16 far rgnenc_chap_shp_init(void) {
    ResFile *file;
    s16 i;
    s16 j;

    if (g_pChapterShapeIds != (u16 *)0) {
        return 0;
    }
    g_pChapterShapeIds = galloc_safe_zcalloc(6);
    file = res_fopen("chap_shp.dat", "rb");
    res_fseek(file, (u32)((g_gameState.nChapter - 1) * 6), SEEK_CUR);
    res_fread(g_pChapterShapeIds, 2, 3, file);
    res_fclose(file);
    i = 0;
    do {
        if (g_pChapterShapeIds[i] != -1) {
            j = 0;
            do {
                combat_actor_bnames_load_cached(g_pChapterShapeIds[i], j);
                j = j + 1;
            } while (j < 3);
        }
        i = i + 1;
    } while (i < 3);
    return 1;
}

s16 far rgnenc_release_sfx_slot_table(void) {
    s16 i;

    if (g_pChapterShapeIds != (u16 *)0x0) {
        for (i = 2; i >= 0; i--) {
            if (g_pChapterShapeIds[i] != 0xffff) {
                combat_actor_release_anim_images(g_pChapterShapeIds[i]);
            }
        }
        galloc_zfree(g_pChapterShapeIds);
        g_pChapterShapeIds = (u16 *)0x0;
        return 1;
    }
    return 0;
}

s16 far rgnenc_global_flag_tbl_contains(u16 id) {
    s16 i;

    i = 0;
    do {
        if (g_pChapterShapeIds[i] == id) {
            return 1;
        }
        i = i + 1;
    } while (i < 3);
    return 0;
}
