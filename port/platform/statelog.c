/*
 * BAK_STATE_LOG=<file>: the game's state, once per pass of the world loop.
 *
 * A line holds the clock's time and what a change to the presentation must leave alone: hashes
 * of the game state as TEMP.GAM stores it (its pointers written as 0, as in the save format), of
 * TEMP.GAM itself and of the random generator, and the party's position and heading. A line is
 * written only when one of them changed since the last. On the virtual clock (BAK_CLOCK=virtual)
 * a scripted run writes the same log every time, so a new renderer, an input device or smoothed
 * motion can be shown to leave the game where it was.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "platform.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GAME/GMAIN.H"
#include "SRC/GAME/STATE/GSTATE.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/R3D/SCENE/ZONE.H"
#include "bak_game_end.h"

/* bak_rtl.h maps fopen to the game's DOS paths; the log is a host path. */
#undef fopen

extern u16 g_randPosJ;
extern u16 g_randPosI;
extern u16 g_randState[56];

typedef struct {
    uint32_t gameState, tempGam, random;
    int32_t x, y, z;
    uint16_t heading;
} StateLine;

/* TEMP.GAM through the game's own open stream, so its buffered writes count and no file is
 * opened (an open per pass is slow where a virus scanner inspects each one). The game seeks
 * before each access, and the position is put back. */
static uint32_t temp_gam_hash(void) {
    if (g_tempGamFP == NULL)
        return 0;
    s32 pos = res_ftell(g_tempGamFP);
    res_fseek(g_tempGamFP, 0L, SEEK_SET);
    unsigned char buf[0x4000];
    uint32_t hash = 0;
    s16 n;
    while ((n = res_fread(buf, 1, (s16)sizeof buf, g_tempGamFP)) > 0)
        hash = platform_hash(hash, buf, (size_t)n);
    res_fseek(g_tempGamFP, pos, SEEK_SET);
    return hash;
}

void bak_state_log_pass(void) {
    static FILE *log;
    static bool checked, written;
    static StateLine last;
    if (!checked) {
        checked = true;
        const char *path = SDL_getenv("BAK_STATE_LOG");
        if (path != NULL && (log = fopen(path, "w")) == NULL) {
            fprintf(stderr, "BAK_STATE_LOG: cannot write %s\n", path);
            exit(2);
        }
    }
    if (log == NULL)
        return;

    StateLine s;
    memset(&s, 0, sizeof s); /* padding too: lines are compared whole */
    unsigned char disk[GAM_SIZEOF_GAMESTATE];
    gstate_gamestate_pack(disk, &g_gameState);
    s.gameState = platform_hash(0, disk, sizeof disk);
    s.tempGam = temp_gam_hash();
    s.random = platform_hash(0, &g_randPosI, sizeof g_randPosI);
    s.random = platform_hash(s.random, &g_randPosJ, sizeof g_randPosJ);
    s.random = platform_hash(s.random, g_randState, sizeof g_randState);
    if (g_world_camera != NULL) {
        s.x = g_world_camera->base.pos.xy.nWorld_x;
        s.y = g_world_camera->base.pos.xy.nWorld_y;
        s.z = g_world_camera->base.pos.nWorld_z;
        s.heading = (uint16_t)g_world_camera->base.orientation.yaw;
    }
    if (written && memcmp(&s, &last, sizeof s) == 0)
        return;
    written = true;
    last = s;
    uint64_t ns = clock_now_ns();
    fprintf(log,
            "%" PRIu64 ".%03" PRIu64 " state=%08" PRIx32 " temp=%08" PRIx32 " random=%08" PRIx32
            " at=%" PRId32 ",%" PRId32 ",%" PRId32 " heading=%04" PRIx16 "\n",
            ns / 1000000, ns / 1000 % 1000, s.gameState, s.tempGam, s.random, s.x, s.y, s.z,
            s.heading);
    fflush(log);
}
