#ifndef BAK_ACTSPAWN_H
#define BAK_ACTSPAWN_H

#include "structs.h"
#include "SRC/GAME/ACTOR/ACTOR.H"

extern Actor far *actorspawn_enc_location(s16 kind, s32 world_x, s32 world_y);
extern Actor far *actorspawn_objfixed(s16 kind, s32 world_x, s32 world_y);
extern void actorspawn_destroy_and_persist(Actor far *actor);
extern void actorspawn_persist_to_temp(Actor far *actor);
extern void actorspawn_npc_prox_periodic(Actor far *actor);
extern s16 actorspawn_for_location_by_time(s16 location_id);
extern s32 actorspawn_location_lookup(s16 location_id);

#endif
