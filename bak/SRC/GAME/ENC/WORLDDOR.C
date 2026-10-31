#include "SRC/GAME/GMAIN.H"
#include "SRC/R3D/ACTOR/ACTOROVL.H"
#include "structs.h"
#include "SRC/GAME/ENC/WORLDDOR.H"
#include "SRC/R3D/ACTOR/ACTRENDR.H"
#include "SRC/GAME/STATE/GSTATE.H"
#include "SRC/GAME/ACTOR/ACTSPAWN.H"
#include "SRC/GAME/ACTOR/ACTORREC.H"

void far worlddoor_pref_slots_clear_all(void) {
    u16 i;

    i = 0;
    do {
        gstate_event_write(DOOR_OPEN(i), 0);
        i = i + 1;
    } while (i < 0x100);
    return;
}

void far worlddoor_load_door_records(VisibleEntryList *list) {
    WorldObject far *cur;
    register s16 i;
    register s16 count;
    Actor far *actor;
    ActorSubrecord far *sub20;
    ActorSubrecord far *sub1;
    s16 door_variant;
    u16 flags_val;
    u16 stateWord;
    u16 event_result;

    cur = list->pEntries;
    count = list->wEntry_count;
    i = 0;
    if (i < count) {
        do {
            if (cur->shapeId == 0x5c || cur->shapeId == 0x5d) {
                actor = actorspawn_objfixed(g_gameState.nZoneId, cur->pos.xy.nWorld_x,
                                            cur->pos.xy.nWorld_y);
                if (actor != 0 && actor->bResidence == RES_FIXED_OBJECT) {
                    if ((sub20 = actorrec_get_subrecord(actor, SUBREC_DOOR_VARIANT)) != 0) {
                        sub1 = actorrec_get_subrecord(actor, SUBREC_PARAMS);
                        door_variant = sub20->door_variant.nDoor_variant;
                        flags_val = (sub1 != 0) ? sub1->interact_msg.bFlags : 0;
                        actorspawn_destroy_and_persist(actor);
                        event_result = gstate_event_read(DOOR_OPEN(door_variant));
                        if (event_result != 0) {
                            cur->shapeId = 0x5d;
                            stateWord = 7;
                        } else {
                            cur->shapeId = 0x5c;
                            stateWord = 0;
                        }
                        stateWord |= (u16)door_variant << 3;
                        if (event_result != 0) {
                            stateWord |= 0x800;
                        }
                        cur->state.stateBits = stateWord;
                        cur->orientation.pitch = flags_val;
                        goto next_iter;
                    }
                }
                cur->state.stateBits = 0;
            }
        next_iter:
            i++;
            cur++;
        } while (i < count);
    }
}

void far worlddoor_rndr_enc_mark_actor(WorldObject far *entry) {
    s16 saved_variant;
    u16 state;
    unsigned char color_idx;

    saved_variant = entry->orientation.pitch;
    state = entry->state.stateBits;
    if (state != 0) {
        color_idx = state & 7;
        if (state & 0x800) {
            entry->shapeId = 0x5d;
        } else {
            entry->shapeId = 0x5c;
        }
        g_nEncounterMarkerColorIdx = color_idx;
        entry->state.stateBits = (u16)&g_nEncounterMarkerColorIdx;
        entry->orientation.pitch = 0;
        actorrender_entity(entry);
        entry->state.stateBits = state;
        entry->orientation.pitch = saved_variant;
        return;
    }
    actorrender_entity(entry);
}
