#ifndef BAK_ACTOROVL_H
#define BAK_ACTOROVL_H

#include "structs.h"

/* The variant tables the render wrappers hand the renderer for an actor: a byte per mesh
   part group, 0FFh hiding the group; ten bytes each, as in the original's DGROUP. */
extern unsigned char g_encounterMarkerTable[10];
extern unsigned char g_render_actor_wstate_scratch[10];
extern unsigned char g_shakeJitterTable[10];
extern unsigned char g_overlaySpriteTable[10];
#define g_nEncounterMarkerColorIdx (g_encounterMarkerTable[0])
#define g_bShakeJitterX (g_shakeJitterTable[0])
#define g_bShakeJitterY (g_shakeJitterTable[1])
#define g_bOverlaySpriteIdxScratch (g_overlaySpriteTable[0])
extern unsigned short g_bShakeActorEnabled;
extern ImageRecord **g_pAssetTable2;

extern void actoroverlay_image_1ff4_load(void);
extern void actoroverlay_image_1ff4_free(void);
extern void actoroverlay_render_actor(WorldObject far *visible_entry);

#endif
