#include "SRC/R3D/SCENE/ZONE.H"
#include "structs.h"
#include "SRC/R3D/ACTOR/ACTOROVL.H"
#include "SRC/GFX/SPRITE/RESBLIT.H"
#include "SRC/R3D/SPRITE/WORLDRND.H"
#include "SRC/R3D/ACTOR/ACTRENDR.H"
#include "SRC/SYS/EMSIMG.H"

#define TABLE_HIDDEN {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}

unsigned char g_encounterMarkerTable[10] = TABLE_HIDDEN;
unsigned char g_render_actor_wstate_scratch[10] = TABLE_HIDDEN;

unsigned short g_bShakeActorEnabled = 0x0000;

unsigned char g_shakeJitterTable[10] = TABLE_HIDDEN;

ImageRecord **g_pAssetTable2 = {0};

unsigned char g_overlaySpriteTable[10] = TABLE_HIDDEN;

void actoroverlay_image_1ff4_load(void) {
    g_pAssetTable2 = resblit_load_asset_table("boom.bmp", 2);
    return;
}

void actoroverlay_image_1ff4_free(void) {
    emsimg_free_paged(g_pAssetTable2);
    g_pAssetTable2 = 0;
    return;
}

void actoroverlay_render_actor(WorldObject far *visible_entry) {
    unsigned short savedRecordId;
    WorldObjectState savedState;
    ImageRecord **new_table_ptr;

    if (visible_entry->state.stateBits != 0) {
        savedRecordId = visible_entry->shapeId;
        savedState = visible_entry->state;
        visible_entry->state.visTable = NULL;
        actorrender_entity(visible_entry);
        if (g_game_mode == 2) {
            visible_entry->shapeId = 0x8e;
        } else {
            visible_entry->shapeId = 0xb6;
        }
        g_bOverlaySpriteIdxScratch = (unsigned char)savedState.stateBits;
        g_bOverlaySpriteIdxScratch--;
        visible_entry->state.visTable = g_overlaySpriteTable;
        new_table_ptr = worldrender_table_swap(0, g_pAssetTable2);
        actorrender_entity(visible_entry);
        worldrender_table_swap(0, new_table_ptr);
        visible_entry->shapeId = savedRecordId;
        visible_entry->state = savedState;
    } else {
        actorrender_entity(visible_entry);
    }
    return;
}
