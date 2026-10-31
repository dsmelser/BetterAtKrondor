#ifndef BAK_ACTMOTN_H
#define BAK_ACTMOTN_H

#include "structs.h"

extern s16 g_nActorKnockbackColorIdx;
extern s16 g_nActorSpriteFlip;
extern u16 g_nActorShakeX;
extern u16 g_nActorShakeY;
extern u16 g_bForceOverlay;

extern void *actormotion_xzmalloc(u16 size);
extern void actormotion_xzfree(void *ptr);
extern WorldEntity *actormotion_vec_alloc(u16 type_tag);
extern u16 *actormotion_alloc_24b_bit800(u16 tag);
extern void actormotion_integrate(WorldEntity *actor);
extern s16 actormotion_atan2_long(s32 y, s32 x);
extern s16 actormotion_clamp_signed_delta(s16 a, s16 b, s16 max_abs);

#endif
