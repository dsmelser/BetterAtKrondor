/*
 * The scripted runs' teleport (BAK_INPUT's teleport step). It fills in the game's own pending
 * teleport, the record that TELEPORT.DAT's destinations and the story's moves fill in: the world
 * loop carries it out at its next pass (modalscreen_pending_scene_trans), reloading the zone's
 * actors and encounters around the new place, and the party goes on from there. A record may
 * name a town scene too (its kind and index, GDS<kind><index as a letter>.DAT), which the game
 * enters before the move; with zone 255 it enters the scene and moves nobody, as TELEPORT.DAT's
 * own records for the scenes do. Scenarios use it to start next to what they test, and it only
 * acts once a game is under way.
 */
#include <string.h>

#include "platform.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GAME/GMAIN.H"
#include "bak_game_end.h"

void game_request_teleport(uint8_t zone, uint8_t tileX, uint8_t tileY, uint8_t cellX, uint8_t cellY,
                           uint16_t heading, uint16_t sceneKind, uint16_t sceneIndex) {
    PlayerSpawnRecord spawn = {zone, tileX, tileY, cellX, cellY, heading};
    unsigned char *rec = g_gameState.abTeleportRecord;
    memset(rec, 0, sizeof g_gameState.abTeleportRecord);
    memcpy(rec, &spawn, sizeof spawn);
    memcpy(rec + 7, &sceneKind, sizeof sceneKind);
    memcpy(rec + 9, &sceneIndex, sizeof sceneIndex);
}
