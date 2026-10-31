#include <dos.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "SRC/GAME/GMAIN.H"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/R3D/SCENE/ZONE.H"
#include "SRC/R3D/VIS/PROXSCAN.H"
#include "SRC/R3D/SCENE/CZONE.H"
#include "SRC/GAME/ENC/RGNENC.H"
#include "structs.h"
#include "SRC/COMBAT/GRID/CMBTGRID.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/SYS/MEM.H"
#include "SRC/AUDIO/ENGINE/AUDIO.H"
#include "SRC/R3D/CORE/DISTDIR.H"
#include "SRC/GFX/SPRITE/RECTSPR.H"
#include "SRC/GFX/RASTER/PIXEL.H"
#include "SRC/GAME/MAINDATA.H"
#include "SRC/R3D/SCENE/WORLDHIT.H"
#include "SRC/R3D/CORE/R3D.H"
#include "SRC/R3D/ACTOR/ACTMOTN.H"
#include "SRC/R3D/SCENE/PROXIM.H"
#include "SRC/R3D/PROJECT/PROJECT.H"
#include "SRC/R3D/FX/WORLDFX.H"
#include "SRC/R3D/TBLSTORE/SHAPETBL.H"
#include "SRC/COMBAT/ACTOR/CACTOR.H"
#include "SRC/COMBAT/ARENA/COMBAT.H"
#include "SRC/COMBAT/ENC/CBENC.H"
#include "SRC/COMBAT/AI/CMBTAI.H"
#include "SRC/COMBAT/SPELL/CSPELL.H"
#include "defines.h"

unsigned char g_bssgap_4c34[2];
GridCombatant *g_combatant_table;
short g_combatant_count;
CombatTile g_combat_tile_grid[8][13];
s16 g_nCombatTerrainWallColor;
unsigned char g_combat_move_map[8][13];

unsigned char far *g_pPalLoadedBuf = {0};
unsigned char far *g_pPalAltA = {0};
unsigned char far *g_pPalAltB = {0};
unsigned char far *g_pPalBlendScratch = {0};
unsigned short g_nPalBlendMode = 0x0000;
unsigned char far *g_pPalLastInstalled = {0};
char g_rgbDayNightCycle_4f4[15] = {0x0a, 0x14, 0x17, 0x00, 0x00, 0x00, 0x36, 0x2c,
                                   0x12, 0x03, 0x17, 0x01, 0x06, 0x0b, 0x21};
s16 g_nPaletteBlendFactorCached = 0;

unsigned char _dgroup_zero_505 = 0;

void combatgrid_cmbts_tbl_alloc(void) {
    g_combatant_table = galloc_safe_zcalloc(0x5a);
    return;
}

void combatgrid_combatants_table_free(void) {
    galloc_zfree(g_combatant_table);
    g_combatant_table = (GridCombatant *)0x0;
    return;
}

s16 combatgrid_coord_valid(s16 x, s16 y) {
    return (x >= 0 && x < 8 && y >= 0 && y < 13) ? 1 : 0;
}

void combatgrid_remove_cmbt_at_tile(s16 tile_x, s16 tile_y) {
    s16 i;

    for (i = 0; i < g_combatant_count; i = i + 1) {
        if (((char)g_combatant_table[i].tile_x == tile_x) &&
            ((char)g_combatant_table[i].tile_y == tile_y)) {
            g_combatant_count--;
            g_combatant_table[i] = g_combatant_table[g_combatant_count];
            return;
        }
    }
    return;
}

void combatgrid_kill_cmbt_at_tile(s16 tile_x, s16 tile_y) {
    s16 i;

    for (i = 0; i < g_combatant_count; i = i + 1) {
        if (((char)g_combatant_table[i].tile_x == tile_x) &&
            ((char)g_combatant_table[i].tile_y == tile_y)) {
            g_combatant_table[i].paged_id = 0x28;
            combatgrid_set_tile_effect((char)tile_x, (char)tile_y, 0xe, -1);
            return;
        }
    }
    return;
}

u16 combatgrid_tile_terrain(char tile_x, char tile_y) {
    if (combatgrid_coord_valid((s16)tile_x, (s16)tile_y) != 0) {
        return (u16)g_combat_tile_grid[tile_x][tile_y].pOccupant;
    }
    return 0;
}

GridCombatant *combatgrid_find_cmbt_at_tile(char tile_x, char tile_y) {
    s16 i;

    for (i = 0; i < g_combatant_count; i++) {
        if (g_combatant_table[i].tile_x == tile_x && g_combatant_table[i].tile_y == tile_y) {
            return &g_combatant_table[i];
        }
    }
    return (GridCombatant *)0;
}

u16 combatgrid_tile_blockd_cmbt(char tile_x, char tile_y) {
    Combatant *actor;
    actor = (Combatant *)combatgrid_tile_terrain(tile_x, tile_y);
    if (actor == (Combatant *)0 || (actor->inner->flags & CAF_DEAD) != 0)
        return 0;
    return 1;
}

u16 combatgrid_tile_is_blocked(char tile_x, char tile_y) {
    s16 blocked;
    u16 terrain;

    blocked = 0;
    terrain = combatgrid_tile_terrain_field(tile_x, tile_y);
    if (combatgrid_coord_valid((s16)tile_x, (s16)tile_y) == 0 || terrain == 2 || terrain == 7) {
        blocked = 1;
    }
    if (combatgrid_tile_terrain(tile_x, tile_y) != 0 ||
        combatgrid_find_cmbt_at_tile(tile_x, tile_y) != (GridCombatant *)0 || blocked) {
        return 1;
    }
    return 0;
}

void combatgrid_tile_set_word(char x, char y, u16 value) {
    if (combatgrid_coord_valid((s16)x, (s16)y) != 0) {
        g_combat_tile_grid[x][y].pOccupant = (Combatant *)value;
    }
    return;
}

u16 combatgrid_tile_terrain_field(char x, char y) {
    if (combatgrid_coord_valid((s16)x, (s16)y) == 0) {
        return 2;
    }
    return g_combat_tile_grid[x][y].wTerrain;
}

s16 combatgrid_tile_field4(char x, char y) {
    if (combatgrid_coord_valid((s16)x, (s16)y) == 0) {
        return -1;
    }
    return g_combat_tile_grid[x][y].nEffectTimer;
}

void far combatgrid_tick_tile_effect(char x, char y) {
    if (g_combat_tile_grid[x][y].nEffectTimer >= 0) {
        if (g_combat_tile_grid[x][y].nEffectTimer == 0) {
            g_combat_tile_grid[x][y].nEffectTimer = -1;
            if (g_combat_tile_grid[x][y].wTerrain == 9) {
                g_combat_tile_grid[x][y].wTerrain = 3;
            } else {
                g_combat_tile_grid[x][y].wTerrain = 0;
            }
        } else {
            g_combat_tile_grid[x][y].nEffectTimer--;
        }
    }
}

void combatgrid_set_tile_effect(char x, char y, unsigned short type, s16 timer) {
    g_combat_tile_grid[x][y].wTerrain = type;
    g_combat_tile_grid[x][y].nEffectTimer = timer;
}

s16 far combatgrid_prox_check_kind_set(WorldPos2 *pos) {
    ProximityScanHit hit;
    u16 kindCopy;
    u16 kind;

    if ((char)proximity_scan_list(&hit, pos, g_wVisibleEntrySegment, g_pwVisibleEntryOffsets,
                                  g_nVisibleEntryCount) != '\0') {
        kind = kindCopy = (u16)ts_get_shape((hit.pRecord)->wRecord_id)->kind;
        switch (kind) {
        case 0:
        case 1:
        case 2:
        case 14:
        case 23:
            return 1;
        }
    }
    return 0;
}

void combatgrid_world_to_view_2d(WorldPos2 *origin, s16 angle, WorldPos2 far *point, s16 *out_xy) {
    s32 dx;
    s32 dy;
    s32 vx;
    s32 vy;
    s16 x;
    s16 out_y;

    dx = point->nWorld_x - origin->nWorld_x;
    dy = point->nWorld_y - origin->nWorld_y;

    vx = (r3d_imul_full32((s16)dx, r3d_tbl_cos(-angle)) -
          r3d_imul_full32((s16)dy, r3d_tbl_sin(-angle))) >>
         0xe;

    vy = (r3d_imul_full32((s16)dy, r3d_tbl_cos(-angle)) +
          r3d_imul_full32((s16)dx, r3d_tbl_sin(-angle))) >>
         0xe;

    if (vx > 0x4000 || vx < -0x4000) {
        x = -1;
    } else {
        x = ((s16)vx + 0x4b0) / g_grid_tile_size;
    }

    if (vy > 0x4000 || vy < -0x4000) {
        out_y = -1;
    } else {
        out_y = ((s16)vy - 0xc80) / g_grid_tile_size;
    }

    if (combatgrid_coord_valid(x, out_y) != 0) {
        *out_xy = x;
        out_xy[1] = out_y;
    } else {
        *out_xy = -1;
    }
    return;
}

s16 combatgrid_tile_cam_near_wedge(s16 tile_x, s16 tile_y) {
    s16 result;

    result = 0;
    tile_y -= 6;
    if (8 - tile_x > tile_y && tile_y < tile_x) {
        result = 1;
    }
    return result;
}

s16 far combatgrid_obj_kind_prox_actv(s16 kind) {
    switch (kind) {
    case 5:
    case 6:
    case 12:
    case 13:
    case 16:
    case 17:
    case 18:
    case 19:
    case 21:
    case 22:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 34:
    case 35:
    case 37:
    case 41:
        return 1;
    }
    return 0;
}

s16 far combatgrid_cmbt_block_kind(s16 kind) {
    switch (kind) {
    case 5:
    case 18:
    case 21:
    case 22:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 37:
        return 1;
    }
    return 0;
}

s16 far combatgrid_plr_prox_sweep_cnt(void) {
    s16 col;
    s16 row;
    EulerAngles savedAngle;
    WorldPos savedPos;
    s16 hitCount;

    savedPos = g_world_camera->base.pos;
    savedAngle = g_world_camera->base.orientation;

    hitCount = 0;
    g_world_camera->base.orientation.pitch = 0;
    g_world_camera->forwardVelocity = 0xc80;
    actormotion_integrate(g_world_camera);
    g_world_camera->forwardVelocity = 0x4b0 - (g_grid_tile_size >> 1);
    g_world_camera->base.orientation.yaw += R3D_DEG(90);
    actormotion_integrate(g_world_camera);
    g_world_camera->base.orientation.yaw -= R3D_UDEG(180);

    row = 0;
    do {
        col = 0;
        do {
            if (combatgrid_prox_check_kind_set(&g_world_camera->base.pos.xy) != 0) {
                hitCount++;
            }
            g_world_camera->forwardVelocity = g_grid_tile_size;
            actormotion_integrate(g_world_camera);
            col = col + 1;
        } while (col < 8);
        g_world_camera->base.orientation.yaw += R3D_UDEG(180);
        g_world_camera->forwardVelocity = g_grid_tile_size << 3;
        actormotion_integrate(g_world_camera);
        g_world_camera->base.orientation.yaw -= R3D_DEG(90);
        g_world_camera->forwardVelocity = g_grid_tile_size;
        actormotion_integrate(g_world_camera);
        g_world_camera->base.orientation.yaw -= R3D_DEG(90);
        row = row + 1;
    } while (row < 0xd);

    g_world_camera->base.pos = savedPos;
    g_world_camera->base.orientation = savedAngle;
    g_world_camera->forwardVelocity = 0;
    return hitCount;
}

s16 far combatgrid_tiles_match_ctr_px(void) {
    s16 screen_xy[3];
    u16 tileColor;
    u16 refColor;
    s16 matchCount;
    s16 tile_x;
    s16 tile_y;

    matchCount = 0;
    world_render_view(1, 1);
    g_graphics_context.wGfxBlitSrcPage = g_graphics_context.wVgaPage1Base;
    refColor = getpixel(0xa0, 0x32);
    g_graphics_context.wGfxBlitDstPage = g_graphics_context.wVgaPage1Base;
    g_graphics_context.bGfx_fill_color = g_graphics_context.bGfx_outline_color = (char)refColor + 1;
    g_graphics_context.bGfx_fill_enabled = 1;
    draw_rect_filled(0, 0, 0x140, 0x1e);
    world_render_view(1, 0);
    g_graphics_context.wGfxBlitSrcPage = g_graphics_context.wVgaPage1Base;
    tile_x = 0;
    do {
        tile_y = 0;
        do {
            project_tile_to_screen(tile_x, tile_y, screen_xy);
            tileColor = getpixel(screen_xy[0], screen_xy[1]);
            if (tileColor == refColor) {
                matchCount++;
            }
            tile_y++;
        } while (tile_y < 0xd);
        tile_x++;
    } while (tile_x < 8);
    g_graphics_context.wGfxBlitDstPage = g_graphics_context.wVgaPage2Base;
    world_render_scene_dispatch(0);
    return matchCount;
}

s16 far combatgrid_tiles_over_thresh(void) {
    s16 count;

    if (g_game_mode != 2) {
        count = combatgrid_plr_prox_sweep_cnt();
    } else {
        count = combatgrid_tiles_match_ctr_px();
    }
    return (u16)(count >= 0x18);
}

void far combatgrid_build_combatant_list(WorldPos2 *pWorldPos, short *pViewState) {
    s16 dupIdx;
    s16 i;
    WorldObject far *entry;
    Shape far *shape;
    s16 iPhantom;
    s16 tileXY[2];
    short entryCount;
    s32 dist;
    s32 maxDist;
    s32 coords[3];

    (void)&iPhantom;
    entry = g_apCombat_zone_actor_lists[0]->pEntries;
    entryCount = g_apCombat_zone_actor_lists[0]->wEntry_count;
    combatgrid_tile_fx_init_pass();
    g_combatant_count = 0;
    i = 0;
    if (i < (s16)entryCount) {
        do {
            shape = ts_get_shape(entry->shapeId);
            if (combatgrid_obj_kind_prox_actv(shape->kind) != 0) {
                *(WorldPos *)coords = *(WorldPos far *)&entry->pos;
                combatgrid_world_to_view_2d(pWorldPos, pViewState[2], (WorldPos2 far *)coords,
                                            tileXY);
                if (tileXY[0] != -1 && (combatgrid_cmbt_block_kind(shape->kind) == 0 ||
                                        combatgrid_tile_cam_near_wedge(tileXY[0], tileXY[1]) == 0 ||
                                        shape->kind == 0x1d)) {
                    dupIdx = 0;
                    if (dupIdx < g_combatant_count) {
                        do {
                            if ((char)g_combatant_table[dupIdx].tile_x == tileXY[0] &&
                                (char)g_combatant_table[dupIdx].tile_y == tileXY[1])
                                break;
                            dupIdx++;
                        } while (dupIdx < g_combatant_count);
                    }
                    if (dupIdx == g_combatant_count) {
                        g_combatant_table[g_combatant_count].tile_x = (unsigned char)tileXY[0];
                        g_combatant_table[g_combatant_count].tile_y = (unsigned char)tileXY[1];
                        g_combatant_table[g_combatant_count].paged_id = entry->shapeId;
                        if (combatgrid_cmbt_block_kind(shape->kind) != 0) {
                            combatgrid_set_tile_effect((char)tileXY[0], (char)tileXY[1], 2, -1);
                        }
                        if (++g_combatant_count >= 0xf)
                            break;
                    }
                }
            }
            entry++;
            i++;
        } while (i < (s16)entryCount);
    }
    entryCount = g_nVisible_entry_count;
    entry = (WorldObject far *)g_pVisible_entry_pool;
    maxDist = (s32)(g_grid_tile_size * 0xd + 0xc80);
    i = 0;
    if (i < (s16)entryCount) {
        do {
            *(WorldPos *)coords = *(WorldPos far *)&entry->pos;
            dist = distdir_octagonal_distance((s32 *)pWorldPos, coords);
            shape = ts_get_shape(entry->shapeId);
            if (dist < maxDist && combatgrid_obj_kind_prox_actv(shape->kind) != 0) {
                combatgrid_world_to_view_2d(pWorldPos, pViewState[2], (WorldPos2 far *)coords,
                                            tileXY);
                if (tileXY[0] != -1 && (combatgrid_cmbt_block_kind(shape->kind) == 0 ||
                                        combatgrid_tile_cam_near_wedge(tileXY[0], tileXY[1]) == 0 ||
                                        shape->kind == 0x1d)) {
                    dupIdx = 0;
                    if (dupIdx < g_combatant_count) {
                        do {
                            if ((char)g_combatant_table[dupIdx].tile_x == tileXY[0] &&
                                (char)g_combatant_table[dupIdx].tile_y == tileXY[1])
                                break;
                            dupIdx++;
                        } while (dupIdx < g_combatant_count);
                    }
                    if (dupIdx == g_combatant_count) {
                        g_combatant_table[g_combatant_count].tile_x = (unsigned char)tileXY[0];
                        g_combatant_table[g_combatant_count].tile_y = (unsigned char)tileXY[1];
                        g_combatant_table[g_combatant_count].paged_id = entry->shapeId;
                        if (combatgrid_cmbt_block_kind(shape->kind) != 0) {
                            combatgrid_set_tile_effect((char)tileXY[0], (char)tileXY[1], 2, -1);
                        }
                        if (++g_combatant_count >= 0xf) {
                            return;
                        }
                    }
                }
            }
            entry++;
            i++;
        } while (i < (s16)entryCount);
    }
}

void combatgrid_tile_to_world_rotated(WorldPos2 *p_out, s16 tile_x, s16 tile_y) {
    s32 rotated_x;
    s32 rotated_y;
    s16 neg_heading;

    p_out->nWorld_x = (s32)(tile_x * g_grid_tile_size + (g_grid_tile_size >> 1) + -0x4b0);
    p_out->nWorld_y = (s32)(tile_y * g_grid_tile_size + (g_grid_tile_size >> 1) + 0xc80);
    neg_heading = -g_world_camera->base.orientation.yaw;
    rotated_x = (r3d_imul_full32((s16)p_out->nWorld_x, r3d_tbl_cos(-neg_heading)) -
                 r3d_imul_full32((s16)p_out->nWorld_y, r3d_tbl_sin(-neg_heading))) >>
                14;
    rotated_y = (r3d_imul_full32((s16)p_out->nWorld_y, r3d_tbl_cos(-neg_heading)) +
                 r3d_imul_full32((s16)p_out->nWorld_x, r3d_tbl_sin(-neg_heading))) >>
                14;
    p_out->nWorld_x = rotated_x + g_world_camera->base.pos.xy.nWorld_x;
    p_out->nWorld_y = rotated_y + g_world_camera->base.pos.xy.nWorld_y;
    p_out->nWorld_x -= (g_world_camera->base.pos.xy.nWorld_x / 64000) * 64000;
    p_out->nWorld_y -= (g_world_camera->base.pos.xy.nWorld_y / 64000) * 64000;
}

void far combatgrid_actor_set_grid_pos(s16 actor_idx, s16 new_x, s16 new_y) {
    combatgrid_tile_set_word((g_combat_actors_A[actor_idx].inner)->gridX,
                             (g_combat_actors_A[actor_idx].inner)->gridY, 0);
    (g_combat_actors_A[actor_idx].inner)->gridX = (unsigned char)new_x;
    (g_combat_actors_A[actor_idx].inner)->gridY = (unsigned char)new_y;
    combat_actor_place_on_free_tile(&g_combat_actors_A[actor_idx]);
    return;
}

void combatgrid_load_traps_dat(void) {
    s16 skip;
    ResFile *file;
    s16 actor_idx;
    unsigned char tileX;
    unsigned char tileY;
    s16 recordCount;
    s16 i;
    short recordId;
    short start_idx;
    unsigned short effectType;
    s32 offset;

    recordCount = 0;
    g_traps_loaded_flag = 1;
    file = res_fopen("traps.dat", "rb");
    offset = (s32)((s16)g_encounter_id * 0x3e);
    res_fseek(file, offset, SEEK_SET);
    res_fread(&recordCount, 2, 1, file);
    start_idx = g_combatant_count;
    i = 0;
    while (i < recordCount) {
        skip = 0;
        res_fread(&recordId, 2, 1, file);
        res_fread(&tileX, 1, 1, file);
        res_fread(&tileY, 1, 1, file);
        if (recordId >= 0) {
            g_combatant_table[g_combatant_count].paged_id = recordId;
            g_combatant_table[g_combatant_count].tile_x = tileX;
            g_combatant_table[g_combatant_count].tile_y = tileY;
            switch (recordId) {
            case 7:
            case 8:
                effectType = 3;
                break;
            case 9:
            case 10:
                effectType = 5;
                break;
            default:
                skip = 1;
            }
            if (!skip) {
                combatgrid_set_tile_effect(tileX, tileY, effectType, -1);
                g_combatant_count++;
            }
        } else {

            recordId =
                (recordId < 0) ? ((recordId == (short)0x8000) ? 0x7fff : -recordId) : recordId;
            if (recordId == 10 || recordId == 0xb || recordId == 0xc || recordId == 0xd) {
                combatgrid_set_tile_effect(tileX, tileY, recordId, -1);
                g_combatant_table[g_combatant_count].paged_id = 0xb;
                g_combatant_table[g_combatant_count].tile_x = tileX;
                g_combatant_table[g_combatant_count].tile_y = tileY;
                g_combatant_count++;
            } else if (recordId == 0xf || recordId == 0x10 || recordId == 0x11) {
                actor_idx = (s16)recordId - 0xf;
                if (actor_idx < g_combat_count_A) {
                    combatgrid_actor_set_grid_pos(actor_idx, (s16)(char)tileX, (s16)(char)tileY);
                    combatgrid_set_tile_effect(g_combat_actors_A[actor_idx].inner->gridX,
                                               g_combat_actors_A[actor_idx].inner->gridY, recordId,
                                               -1);
                }
            } else if (recordId == 0x12) {
                g_traps_loaded_flag = 0;
            } else {
                combatgrid_set_tile_effect(tileX, tileY, recordId, -1);
            }
        }
        i++;
    }
    res_fclose(file);
    combatgrid_apply_engage_links(start_idx);
}

void combatgrid_clear_combatants(void) {
    g_combatant_count = 0;
}

void combatgrid_tile_fx_init_walk(void) {
    EulerAngles savedAngle;
    s16 col;
    s16 row;
    WorldPos savedPos;

    savedPos = g_world_camera->base.pos;
    savedAngle = g_world_camera->base.orientation;

    g_world_camera->base.orientation.pitch = 0;
    g_world_camera->forwardVelocity = 0xc80;
    actormotion_integrate(g_world_camera);

    g_world_camera->forwardVelocity = 0x4b0 - (g_grid_tile_size >> 1);
    g_world_camera->base.orientation.yaw += R3D_DEG(90);
    actormotion_integrate(g_world_camera);

    g_world_camera->base.orientation.yaw -= R3D_UDEG(180);

    row = 0;
    do {
        col = 0;
        do {
            if (combatgrid_prox_check_kind_set(&g_world_camera->base.pos.xy) != 0)
                combatgrid_set_tile_effect((char)col, (char)row, 0, -1);
            else
                combatgrid_set_tile_effect((char)col, (char)row, 2, -1);
            g_combat_tile_grid[col][row].pOccupant = (Combatant *)0;
            g_world_camera->forwardVelocity = g_grid_tile_size;
            actormotion_integrate(g_world_camera);
            col++;
        } while (col < 8);

        g_world_camera->base.orientation.yaw += R3D_UDEG(180);
        g_world_camera->forwardVelocity = g_grid_tile_size << 3;
        actormotion_integrate(g_world_camera);

        g_world_camera->base.orientation.yaw -= R3D_DEG(90);
        g_world_camera->forwardVelocity = g_grid_tile_size;
        actormotion_integrate(g_world_camera);

        g_world_camera->base.orientation.yaw -= R3D_DEG(90);
        row++;
    } while (row < 0xd);

    g_world_camera->base.pos = savedPos;
    g_world_camera->base.orientation = savedAngle;
    g_world_camera->forwardVelocity = 0;
}

void far combatgrid_classify_tiles_px(void) {
    s16 screen_xy[3];
    s16 row;
    s16 col;
    u16 tile_color;
    u16 ref_color;

    world_render_view(1, 1);
    g_graphics_context.wGfxBlitSrcPage = g_graphics_context.wVgaPage1Base;
    ref_color = getpixel(0xa0, 0x32);
    g_graphics_context.wGfxBlitDstPage = g_graphics_context.wVgaPage1Base;
    g_graphics_context.bGfx_fill_color = g_graphics_context.bGfx_outline_color =
        (char)ref_color + 1;
    g_graphics_context.bGfx_fill_enabled = 1;
    draw_rect_filled(0, 0, 0x140, 0x1e);
    world_render_view(1, 0);
    g_graphics_context.wGfxBlitSrcPage = g_graphics_context.wVgaPage1Base;
    row = 0;
    do {
        col = 0;
        do {
            project_tile_to_screen(row, col, screen_xy);
            tile_color = getpixel(screen_xy[0], screen_xy[1]);
            if (tile_color == ref_color)
                combatgrid_set_tile_effect((char)row, (char)col, 0, -1);
            else
                combatgrid_set_tile_effect((char)row, (char)col, 2, -1);
            g_combat_tile_grid[row][col].pOccupant = (Combatant *)0;
            col++;
        } while (col < 0xd);
        row++;
    } while (row < 8);
}

void far combatgrid_tile_fx_init_pass(void) {
    s16 col;
    s16 row;

    s16 tile_count = 0;
    if (g_game_mode != 2) {
        combatgrid_tile_fx_init_walk();
    } else {
        combatgrid_classify_tiles_px();
    }
    col = 0;
    do {
        row = 0;
        do {
            if (combatgrid_tile_is_blocked((char)col, (char)row) == 0) {
                tile_count++;
            }
            row++;
        } while (row < 0xd);
        col++;
    } while (col < 8);
    col = 0;
    do {
        row = 0;
        do {
            if (combatgrid_tile_is_blocked((char)col, (char)row) == 0) {
                if (combatgrid_tile_has_mobility(col, row, tile_count) == 0) {
                    combatgrid_set_tile_effect((char)col, (char)row, 2, -1);
                    tile_count--;
                }
            }
            row++;
        } while (row < 0xd);
        col++;
    } while (col < 8);
}

void combatgrid_load_and_init(void) {
    ResFile *file;
    s16 col;
    s16 row;

    file = res_fopen("grid.dat", "rb");
    res_fseek(file, (s32)(u16)((g_gameState.nZoneId - 1) * 2), SEEK_SET);
    res_fread(&g_nCombatTerrainWallColor, 2, 1, file);
    res_fclose(file);
    combatgrid_set_tile_effect('\0', '\0', 2, -1);
    combatgrid_set_tile_effect('\a', '\0', 2, -1);
    if (g_game_mode == 2) {
        for (row = 7; row < 0xd; row++) {
            col = 0;
            do {
                combatgrid_set_tile_effect((char)col, (char)row, 2, -1);
                col++;
            } while (col < 8);
        }
    }
    combatgrid_load_traps_dat();
    combat_actor_place_all_free();
    combatenc_place_b_free_tiles();
}

void combatgrid_draw_terrain_walls(void) {
    s16 run_start;
    s16 col;
    s16 row;

    g_nPolygonTextureMode = 0;
    row = 0;
    do {
        col = 0;
        do {
            for (run_start = col; combatgrid_tile_terrain_field((char)col, (char)row) != 2 &&
                                  combatgrid_tile_terrain_field((char)col, (char)row) != 7;
                 col++) {
            }
            if (col != run_start) {
                worldfx_draw_world_line_color(run_start, row, col, row, g_nCombatTerrainWallColor);
                worldfx_draw_world_line_color(run_start, row - 1, col, row - 1,
                                              g_nCombatTerrainWallColor);
            } else {
                col++;
            }
        } while (col < 8);
        row++;
    } while (row < 0xd);
    col = 0;
    do {
        row = 0;
        do {
            for (run_start = row; combatgrid_tile_terrain_field((char)col, (char)row) != 2 &&
                                  combatgrid_tile_terrain_field((char)col, (char)row) != 7;
                 row++) {
            }
            if (row != run_start) {
                worldfx_draw_world_line_color(col, run_start - 1, col, row - 1,
                                              g_nCombatTerrainWallColor);
                worldfx_draw_world_line_color(col + 1, run_start - 1, col + 1, row - 1,
                                              g_nCombatTerrainWallColor);
            } else {
                row++;
            }
        } while (row < 0xd);
        col++;
    } while (col < 8);
    row = 0;
    do {
        col = 0;
        do {
            col++;
        } while (col < 8);
        row++;
    } while (row < 0xd);
    return;
}

void far combatgrid_apply_tile_status_fx(s16 tile_x, s16 tile_y, s16 target_actor) {
    s16 effect_slot;
    Combatant tempActor;
    CombatantState tempInner;

    if (target_actor != 0) {
        combatgrid_tile_set_word(tile_x, tile_y, (u16)&tempActor);
    }
    tempInner.gridX = tile_x;
    tempInner.gridY = tile_y;
    tempInner.dmgFloatValue = '\0';
    tempInner.dmgFloatFrames = '\0';
    tempActor.inner = &tempInner;
    tempActor.inner->creatureType = -1;
    if (target_actor != 0) {
        effect_slot = cspell_status_effect_add(&tempActor, 4, 0, 0, '\0');
        g_nVfxParticleColor = 0x6f;
    }
    audio_play(0x1d);
    worldfx_combat_damage_ptcl_burst(&tempActor, 0x19);
    if (target_actor != 0) {
        cspell_status_effect_remove(&tempActor, effect_slot);
        combatgrid_tile_set_word(tile_x, tile_y, 0);
    }
    return;
}

void combatgrid_mark_engagement_line(s16 x1, s16 y1, s16 x2, s16 y2, short paged_id,
                                     s16 do_register) {
    s16 sx;
    s16 sy;

    if (do_register != 0) {
        if (combatgrid_find_cmbt_at_tile((unsigned char)x1, (unsigned char)y1) != 0)
            combatgrid_remove_cmbt_at_tile(x1, y1);
        g_combatant_table[g_combatant_count].tile_x = (unsigned char)x1;
        g_combatant_table[g_combatant_count].tile_y = (unsigned char)y1;
        g_combatant_table[g_combatant_count].paged_id = paged_id;
        combatgrid_set_tile_effect((unsigned char)x1, (unsigned char)y1, 3, -1);
        g_combatant_count++;

        if (combatgrid_find_cmbt_at_tile((unsigned char)x2, (unsigned char)y2) != 0)
            combatgrid_remove_cmbt_at_tile(x2, y2);
        g_combatant_table[g_combatant_count].tile_x = (unsigned char)x2;
        g_combatant_table[g_combatant_count].tile_y = (unsigned char)y2;
        g_combatant_table[g_combatant_count].paged_id = paged_id;
        combatgrid_set_tile_effect((unsigned char)x2, (unsigned char)y2, 3, -1);
        g_combatant_count++;
    }

    if (x2 > x1)
        sx = 1;
    else if (x2 < x1)
        sx = -1;
    else
        sx = 0;

    if (y2 > y1)
        sy = 1;
    else if (y2 < y1)
        sy = -1;
    else
        sy = 0;

    x1 += sx;
    y1 += sy;
    while (x1 != x2 || y1 != y2) {
        combatgrid_set_tile_effect((unsigned char)x1, (unsigned char)y1, 3, -1);
        x1 += sx;
        y1 += sy;
    }
}

s16 far combatgrid_place_actor_on_tile(s16 tile_x, s16 tile_y, s16 new_tile_x, s16 new_tile_y) {
    GridCombatant *cmbt;

    if (combatgrid_tile_is_blocked((unsigned char)new_tile_x, (unsigned char)new_tile_y) != 0)
        return 0;
    cmbt = combatgrid_find_cmbt_at_tile((unsigned char)tile_x, (unsigned char)tile_y);
    if (cmbt == (GridCombatant *)0)
        return 0;
    cmbt->tile_x = (unsigned char)new_tile_x;
    cmbt->tile_y = (unsigned char)new_tile_y;
    combatgrid_set_tile_effect((unsigned char)tile_x, (unsigned char)tile_y, 0, -1);
    if (combatgrid_tile_terrain_field((unsigned char)new_tile_x, (unsigned char)new_tile_y) == 3) {
        combatgrid_spread_tile_fx_line(new_tile_x, new_tile_y);
        combat_actor_play_short_cine((Combatant *)0, 0);
        combatgrid_line_effect_propagate(new_tile_x, new_tile_y);
        combatgrid_apply_tile_status_fx(new_tile_x, new_tile_y, 1);
        cmbt->tile_x = 0xff;
        cmbt->tile_y = 0xff;
    } else {
        combatgrid_set_tile_effect((unsigned char)new_tile_x, (unsigned char)new_tile_y, 5, -1);
    }
    return 1;
}

void combatgrid_find_adj_pass_tile(s16 x, s16 y, u16 terrain_mask, s16 *out_dx, s16 *out_dy) {
    s16 dx;
    s16 dy;
    s16 found;
    GridCombatant *combatant;
    u16 terrain;

    found = 0;
    combatant = combatgrid_find_cmbt_at_tile((unsigned char)x, (unsigned char)y);
    if ((combatant != 0) && ((combatant->paged_id == 7) || (combatant->paged_id == 8))) {
        dx = -1;
        do {
            dy = -1;
            do {
                combatant = combatgrid_find_cmbt_at_tile((unsigned char)x + (char)dx, (unsigned char)y + (char)dy);
                if (((dx != 0) || (dy != 0)) &&
                    (combatgrid_tile_terrain_field((unsigned char)x + (char)dx, (unsigned char)y + (char)dy) ==
                     terrain_mask) &&
                    ((combatant == 0) ||
                     ((combatant->paged_id != 7) && (combatant->paged_id != 8)))) {
                    *out_dx = dx;
                    *out_dy = dy;
                    dx = dy = 2;
                    found = 1;
                }
                dy++;
            } while (dy < 2);
            dx++;
        } while (dx < 2);
    } else {
        dx = -1;
        do {
            dy = -1;
            do {
                if (((dx != 0) || (dy != 0)) &&
                    (combatgrid_tile_terrain_field((unsigned char)x + (char)dx, (unsigned char)y + (char)dy) ==
                     terrain_mask)) {
                    combatant =
                        combatgrid_find_cmbt_at_tile((unsigned char)x + (char)dx, (unsigned char)y + (char)dy);
                    if ((combatant == 0) || (combatant->paged_id == 7) ||
                        (combatant->paged_id == 8)) {
                        if ((combatgrid_tile_terrain_field((unsigned char)x - (char)dx,
                                                           (unsigned char)y - (char)dy) == terrain_mask) ||
                            (combatgrid_find_cmbt_at_tile((unsigned char)x, (unsigned char)y) != 0)) {
                            *out_dx = dx;
                            *out_dy = dy;
                            dx = dy = 2;
                            found = 1;
                        }
                    }
                }
                dy++;
            } while (dy < 2);
            dx++;
        } while (dx < 2);
    }
    if (!found) {
        if ((combatgrid_find_cmbt_at_tile((unsigned char)x + 1, (unsigned char)y) != 0) &&
            (combatgrid_find_cmbt_at_tile((unsigned char)x - 1, (unsigned char)y) != 0)) {
            *out_dx = 1;
            *out_dy = 0;
        } else {
            *out_dx = 0;
            *out_dy = 1;
        }
    }
}

void combatgrid_spread_tile_fx_line(s16 x, s16 y) {
    s16 step_x, step_y;

    combatgrid_find_adj_pass_tile(x, y, 3, &step_x, &step_y);
    while (combatgrid_coord_valid(x, y) &&
           combatgrid_tile_terrain_field((unsigned char)x - (char)step_x, (unsigned char)y - (char)step_y) == 3) {
        x -= step_x;
        y -= step_y;
    }
    while (combatgrid_coord_valid(x, y) && combatgrid_find_cmbt_at_tile((unsigned char)x, (unsigned char)y) == 0) {
        x += step_x;
        y += step_y;
    }
    while (combatgrid_coord_valid(x, y) && combatgrid_tile_terrain_field((unsigned char)x, (unsigned char)y) == 3) {
        combatgrid_set_tile_effect((unsigned char)x, (unsigned char)y, 4, -1);
        x += step_x;
        y += step_y;
    }
}

void far combatgrid_line_effect_propagate(s16 x, s16 y) {
    s16 dx, dy;
    combatgrid_find_adj_pass_tile(x, y, 4, &dx, &dy);
    while (combatgrid_coord_valid(x, y) &&
           combatgrid_tile_terrain_field((char)x - (char)dx, (char)y - (char)dy) == 4) {
        x -= dx;
        y -= dy;
    }
    while (combatgrid_coord_valid(x, y) && combatgrid_find_cmbt_at_tile((unsigned char)x, (unsigned char)y) == 0) {
        x += dx;
        y += dy;
    }
    while (combatgrid_coord_valid(x, y) && combatgrid_tile_terrain_field((char)x, (char)y) == 4) {
        combatgrid_set_tile_effect((char)x, (char)y, 3, -1);
        x += dx;
        y += dy;
    }
}

s16 far combatgrid_tile_walkable_kind(s16 x, s16 y, s16 kind) {
    s16 ok;
    GridCombatant *cmbt;

    cmbt = combatgrid_find_cmbt_at_tile(x, y);
    if (kind == -1) {
        ok = !(cmbt != (GridCombatant *)0 && cmbt->paged_id != 7 && cmbt->paged_id != 8);
    } else {
        ok = !(cmbt != (GridCombatant *)0 && cmbt->paged_id != kind);
    }
    if (combatgrid_tile_terrain_field(x, y) == 3 && ok) {
        return 1;
    }
    return 0;
}

s16 far combatgrid_nbr_probe_diag(s16 x, s16 y, s16 kind) {
    if (combatgrid_nbr_probe_orthogonal(x, y, kind) ||
        combatgrid_tile_walkable_kind(x + 1, y + 1, kind) ||
        combatgrid_tile_walkable_kind(x + 1, y - 1, kind) ||
        combatgrid_tile_walkable_kind(x - 1, y + 1, kind) ||
        combatgrid_tile_walkable_kind(x - 1, y - 1, kind))
        return 1;
    return 0;
}

void combatgrid_kill_adj_cmbt_team(s16 tile_x, s16 tile_y, s16 team) {
    GridCombatant *pCmbt;
    s16 dy;
    s16 dx;

    dy = -1;
    do {
        dx = -1;
        do {
            pCmbt = combatgrid_find_cmbt_at_tile((char)tile_x + (char)dy, (char)tile_y + (char)dx);
            if ((pCmbt != (GridCombatant *)0x0) && (pCmbt->paged_id == team)) {
                combatgrid_kill_cmbt_at_tile(tile_x + dy, tile_y + dx);
                return;
            }
            dx++;
        } while (dx < 2);
        dy++;
    } while (dy < 2);
}

#pragma option -O-b
void combatgrid_push_back_actor(s16 tile_x, s16 tile_y) {
    s16 dx;
    s16 dy;
    GridCombatant *cmbt;
    s16 origX;
    s16 origY;

    cmbt = combatgrid_find_cmbt_at_tile((unsigned char)tile_x, (unsigned char)tile_y);
    origX = tile_x;
    origY = tile_y;
    combatgrid_find_adj_pass_tile(tile_x, tile_y, 3, &dx, &dy);
    if (combatgrid_find_cmbt_at_tile((unsigned char)tile_x + (char)dx, (unsigned char)tile_y + (char)dy) != 0) {
        dx = -dx;
        dy = -dy;
    }
    if ((dx == 0) && (dy == 0)) {
        combatgrid_kill_cmbt_at_tile(tile_x, tile_y);
        combatgrid_kill_adj_cmbt_team(tile_x, tile_y, cmbt->paged_id);
        return;
    }
    while ((tile_x += dx, tile_y += dy, combatgrid_coord_valid(tile_x, tile_y)) &&
           combatgrid_find_cmbt_at_tile((unsigned char)tile_x, (unsigned char)tile_y) == 0) {
        combatgrid_set_tile_effect((unsigned char)tile_x, (unsigned char)tile_y, 0, -1);
    }
    if (combatgrid_nbr_probe_diag(origX, origY, cmbt->paged_id) == 0) {
        combatgrid_kill_cmbt_at_tile(origX, origY);
    }
    if (combatgrid_nbr_probe_diag(tile_x, tile_y, cmbt->paged_id) == 0) {
        combatgrid_kill_cmbt_at_tile(tile_x, tile_y);
    }
}
#pragma option -Ob

void far combatgrid_shove_until_unblocked(s16 tile_x, s16 tile_y) {
    GridCombatant *combatant;

    combatant = combatgrid_find_cmbt_at_tile((unsigned char)tile_x, (unsigned char)tile_y);
    combatgrid_apply_tile_status_fx(tile_x, tile_y, 0);
    while (combatgrid_nbr_probe_diag(tile_x, tile_y, combatant->paged_id) != 0) {
        combatgrid_push_back_actor(tile_x, tile_y);
    }
    return;
}

s16 combatgrid_nbr_probe_orthogonal(s16 x, s16 y, s16 kind) {
    if (combatgrid_tile_walkable_kind(x + 1, y, kind) != 0 ||
        combatgrid_tile_walkable_kind(x - 1, y, kind) != 0 ||
        combatgrid_tile_walkable_kind(x, y + 1, kind) != 0 ||
        combatgrid_tile_walkable_kind(x, y - 1, kind) != 0) {
        return 1;
    }
    return 0;
}

s16 combatgrid_is_combatant_type(s16 paged_id) {
    return ((((paged_id == 7) || (paged_id == 8)) || (paged_id == 10)) ||
            (((paged_id == 9 || (paged_id == 0x28)) || (paged_id == 0xb))))
               ? 1
               : 0;
}

void combatgrid_clear_tile_effects(void) {
    s16 i;
    s16 x;
    s16 y;

    i = 0;
    if (i < g_combatant_count) {
        do {
            if (combatgrid_is_combatant_type(g_combatant_table[i].paged_id) != 0) {
                g_combatant_count = i;
                break;
            }
            i = i + 1;
        } while (i < g_combatant_count);
    }
    x = 0;
    do {
        y = 0;
        do {
            if (2 < (s16)combatgrid_tile_terrain_field((char)x, (char)y)) {
                combatgrid_set_tile_effect((char)x, (char)y, 0, -1);
            }
            y++;
        } while (y < 0xd);
        x++;
    } while (x < 8);
    return;
}

void combatgrid_save_traps_terr(s16 encounter_idx) {
    ResFile *file;
    s16 i;
    char tileX;
    char tileY;
    s16 writeCount;
    s16 firstIdx;
    u16 terrain;
    s32 offset;

    writeCount = 0;
    firstIdx = -1;
    file = res_fopen("traps.dat", "r+b");
    offset = (s32)(encounter_idx * 0x3e);
    res_fseek(file, offset, SEEK_SET);
    for (i = 0; i < g_combatant_count; i++) {
        if (combatgrid_is_combatant_type(g_combatant_table[i].paged_id) != 0) {
            writeCount++;
            if (firstIdx == -1) {
                firstIdx = i;
            }
        }
    }
    if (firstIdx == -1) {
        firstIdx = g_combatant_count;
    }
    tileX = '\0';
    do {
        tileY = '\0';
        do {
            terrain = combatgrid_tile_terrain_field(tileX, tileY);
            if (((((terrain == 6) || (terrain == 10)) || (terrain == 0xb)) ||
                 ((terrain == 0xc || (terrain == 0xd)))) ||
                ((terrain == 0xf || ((terrain == 0x10 || (terrain == 0x11)))))) {
                writeCount++;
            }
            tileY++;
        } while (tileY < '\r');
        tileX++;
    } while (tileX < '\b');
    if (g_traps_loaded_flag == 0) {
        writeCount++;
    }
    res_fwrite(&writeCount, 2, 1, file);
    for (i = firstIdx; i < g_combatant_count; i++) {
        if (g_combatant_table[i].paged_id != 0xb) {
            res_fwrite(&g_combatant_table[i].paged_id, 2, 1, file);
            res_fwrite(&g_combatant_table[i].tile_x, 1, 1, file);
            res_fwrite(&g_combatant_table[i].tile_y, 1, 1, file);
        }
    }
    tileX = '\0';
    do {
        tileY = '\0';
        do {
            terrain = combatgrid_tile_terrain_field(tileX, tileY);
            if ((((terrain == 6) || (terrain == 10)) || (terrain == 0xb)) ||
                (((terrain == 0xc || (terrain == 0xd)) ||
                  ((terrain == 0xf || ((terrain == 0x10 || (terrain == 0x11)))))))) {
                terrain = -terrain;
                res_fwrite(&terrain, 2, 1, file);
                res_fwrite(&tileX, 1, 1, file);
                res_fwrite(&tileY, 1, 1, file);
            }
            tileY++;
        } while (tileY < '\r');
        tileX++;
    } while (tileX < '\b');
    if (g_traps_loaded_flag == 0) {
        terrain = 0xffee;
        res_fwrite(&terrain, 2, 1, file);
    }
    res_fclose(file);
    return;
}

s16 combatgrid_can_engage_same_kind(s16 idx_a, s16 idx_b) {
    s16 samePaged;
    s16 bothType;
    s16 dx;
    s16 dy;

    samePaged = (g_combatant_table[idx_a].paged_id == g_combatant_table[idx_b].paged_id);

    bothType =
        ((g_combatant_table[idx_a].paged_id == 7 || g_combatant_table[idx_a].paged_id == 8) &&
         (g_combatant_table[idx_b].paged_id == 7 || g_combatant_table[idx_b].paged_id == 8));

    dx = (s16)(char)g_combatant_table[idx_b].tile_x - (s16)(char)g_combatant_table[idx_a].tile_x;
    dy = (s16)(char)g_combatant_table[idx_b].tile_y - (s16)(char)g_combatant_table[idx_a].tile_y;

    if (samePaged && bothType) {
        if (dx != 0 && dy != 0) {
            if (((dx < 0) ? (dx == -0x8000 ? 0x7fff : -dx) : dx) !=
                ((dy < 0) ? (dy == -0x8000 ? 0x7fff : -dy) : dy))
                return 0;
        }
        return 1;
    }
    return 0;
}

void combatgrid_apply_engage_links(s16 start_idx) {
    s16 idx_a;
    s16 idx_b;

    for (idx_a = start_idx; idx_a < g_combatant_count; idx_a++) {
        for (idx_b = start_idx; idx_b < g_combatant_count; idx_b++) {
            if (idx_a != idx_b && combatgrid_can_engage_same_kind(idx_a, idx_b) != 0) {
                combatgrid_mark_engagement_line((s16)(char)g_combatant_table[idx_a].tile_x,
                                                (s16)(char)g_combatant_table[idx_a].tile_y,
                                                (s16)(char)g_combatant_table[idx_b].tile_x,
                                                (s16)(char)g_combatant_table[idx_b].tile_y,
                                                g_combatant_table[idx_a].paged_id, 0);
            }
        }
    }
}

void far combatgrid_place_tile_fx_at_cur(unsigned short type, s16 timer) {
    u16 terrain;

    while (combat_arena_wait_confirm_cancel() != 0)
        ;
    terrain = combatgrid_tile_terrain_field((unsigned char)g_cursor_tile_x, (unsigned char)g_cursor_tile_y);
    if ((combatgrid_find_cmbt_at_tile((unsigned char)g_cursor_tile_x, (unsigned char)g_cursor_tile_y) ==
         (GridCombatant *)0x0) &&
        (terrain == 0)) {
        combatgrid_set_tile_effect((unsigned char)g_cursor_tile_x, (unsigned char)g_cursor_tile_y, type, timer);
    }
}

void far combatgrid_clear_terr_6_effects(void) {
    char x;
    char y;
    u16 terrain;

    for (x = 0; x < 8; x++) {
        for (y = 0; y < 13; y++) {
            terrain = combatgrid_tile_terrain_field(x, y);
            if (terrain == 6) {
                combatgrid_set_tile_effect(x, y, 0, -1);
            }
        }
    }
}

s16 combatgrid_actor_past_terr6_row(void) {
    char x;
    char y;
    char foundY;
    s16 i;

    foundY = '\0';
    x = '\0';
    do {
        y = '\0';
        do {
            if (combatgrid_tile_terrain_field(x, y) == 6) {
                foundY = y;
                break;
            }
            y++;
        } while (y < '\r');
        x++;
    } while (x < '\b');

    i = 0;
    if (i < g_combat_count_A) {
        do {
            if (g_combat_actors_A[i].charSlot != '\0' &&
                g_combat_actors_A[i].inner->gridY >= foundY) {
                return 1;
            }
            i++;
        } while (i < g_combat_count_A);
    }
    return 0;
}

s16 combatgrid_any_terrain_6(void) {
    char x;
    char y;

    x = '\0';
    do {
        y = '\0';
        do {
            if (combatgrid_tile_terrain_field(x, y) == 6) {
                return 1;
            }
            y++;
        } while (y < '\r');
        x++;
    } while (x < '\b');
    return 0;
}

void far combatgrid_cur_tile_world(void) {
    s32 world_xyz[3];

    project_cursor_to_world_tile(world_xyz);
    g_cursor_tile_x = (world_xyz[0] + 0x4b0) / g_grid_tile_size;
    g_cursor_tile_y = (world_xyz[1] + -0xc80) / g_grid_tile_size;
}

s16 combatgrid_chebyshev_distance(char x1, char y1, char x2, char y2) {
    s16 t;
    s16 dx;
    s16 dy;
    s16 result;

    if (x1 > x2) {
        t = x2;
        x2 = x1;
        x1 = t;
    }
    if (y1 > y2) {
        t = y2;
        y2 = y1;
        y1 = t;
    }
    dx = (s16)x2 - (s16)x1;
    dy = (s16)y2 - (s16)y1;
    if (dx > dy) {
        result = dx;
    } else {
        result = dy;
    }
    return result;
}

s16 combatgrid_actors_ortho_adj(Combatant *actor_a, Combatant *actor_b) {
    s16 dist;

    dist = combatgrid_chebyshev_distance(actor_a->inner->gridX, actor_a->inner->gridY,
                                         actor_b->inner->gridX, actor_b->inner->gridY);
    return (dist == 1) && (combatgrid_is_pure_diagonal(actor_a, (s16)actor_b->inner->gridX,
                                                       (s16)actor_b->inner->gridY) == 0);
}

s16 combatgrid_is_pure_diagonal(Combatant *actor, s16 target_x, s16 target_y) {
    s16 ax;
    s16 ay;
    s16 t;
    s16 dx;
    s16 dy;

    ax = (s16)actor->inner->gridX;
    ay = (s16)actor->inner->gridY;
    if (ax > target_x) {
        t = target_x;
        target_x = ax;
        ax = t;
    }
    if (ay > target_y) {
        t = target_y;
        target_y = ay;
        ay = t;
    }
    dx = target_x - ax;
    dy = target_y - ay;
    return (u16)(dx == dy);
}

s16 far combatgrid_actor_try_step_tile(Combatant *actor, s16 tx, s16 ty) {
    s16 result;

    if (combatgrid_tile_terrain_field((unsigned char)tx, (unsigned char)ty) == 5) {
        if (combatgrid_chebyshev_distance(actor->inner->gridX, actor->inner->gridY, (unsigned char)tx,
                                          (unsigned char)ty) == 1) {
            result = 1;
        } else {
            result = 0;
        }
    } else {
        actor->inner->destX = (unsigned char)tx;
        actor->inner->destY = (unsigned char)ty;
        result = combataipath_actor_walk_path(actor, 1);
    }
    return result;
}

void far combatgrid_build_move_attack_map(Combatant *actor) {
    Combatant *target;
    s16 col;
    s16 row;

    col = 0;
    do {
        row = 0;
        do {
            g_combat_move_map[col][row] = '\0';
            if (combatgrid_actor_try_step_tile(actor, col, row) != 0) {
                g_combat_move_map[col][row] |= 1;
            }
            target = (Combatant *)combatgrid_tile_terrain((char)col, (char)row);
            if (target != (Combatant *)0x0) {
                if (combat_actor_trace_proj_path(actor, target, 0) != 0) {
                    g_combat_move_map[col][row] |= 2;
                }
            }
            ++row;
        } while (row < 0xd);
        ++col;
    } while (col < 8);
    return;
}

s16 combatgrid_cursor_tile_movable(void) {
    return (signed char)g_combat_move_map[g_cursor_tile_x][g_cursor_tile_y] & 1;
}

s16 combatgrid_tile_has_terr_bit2(Combatant *actor) {
    if (actor != (Combatant *)0 &&
        combatgrid_coord_valid((s16)actor->inner->gridX, (s16)actor->inner->gridY) != 0) {
        return (s16)(char)g_combat_move_map[actor->inner->gridX][actor->inner->gridY] & 2;
    }
    return 0;
}

void far combatgrid_actor_step_to_tile(Combatant *mover, s16 tile_x, s16 tile_y) {
    audio_play(1);
    g_nCombatTileX = tile_x;
    g_nCombatTileY = tile_y;
    cspell_apply_step_tile_spell(mover, 4, 0x14, -2);
    return;
}

void far combatgrid_pathfind_from_tile(s16 tx, s16 ty, s16 dest) {
    Combatant tempActor;
    CombatantState tempInner;

    tempActor.inner = &tempInner;
    tempInner.creatureType = -1;
    tempInner.dmgFloatValue = '\0';
    tempInner.dmgFloatFrames = '\0';
    tempInner.flags = '\x01';
    tempInner.gridX = (unsigned char)tx;
    tempInner.gridY = (unsigned char)ty;
    tempInner.statusHead = -1;
    combatgrid_tile_set_word((unsigned char)tx, (unsigned char)ty, (u16)&tempActor);
    combatgrid_step_search(&tempActor, dest);
    combatgrid_tile_set_word((unsigned char)tx, (unsigned char)ty, 0);
    return;
}

s16 combatgrid_tile_blockd_kind10(Combatant *actor) {
    GridCombatant *cmbt;

    cmbt = combatgrid_find_cmbt_at_tile(actor->inner->gridX, actor->inner->gridY);
    if (cmbt != 0 && cmbt->paged_id == 10)
        return 0;
    return 1;
}

s16 far combatgrid_tile_passable_check(s16 required_terrain, s16 tile_x, s16 tile_y,
                                       Combatant *mover) {
    u16 terrain;
    GridCombatant *cmbt;

    terrain = combatgrid_tile_terrain_field((char)tile_x, (char)tile_y);
    if (terrain == required_terrain) {
        combatgrid_actor_step_to_tile(mover, tile_x, tile_y);
        return 1;
    }

    cmbt = combatgrid_find_cmbt_at_tile((unsigned char)tile_x, (unsigned char)tile_y);
    if (combatgrid_tile_blockd_cmbt((char)tile_x, (char)tile_y) != 0)
        return 1;
    if (cmbt == (GridCombatant *)0)
        return 0;
    if (cmbt->paged_id != 10)
        return 1;
    return 0;
}

void combatgrid_step_search(Combatant *actor, s16 preferred_dir) {
    s16 tile_x;
    s16 tile_y;

    if ((actor->inner->flags & CAF_DEAD) != 0)
        return;
    tile_x = (s16)actor->inner->gridX;
    tile_y = (s16)actor->inner->gridY;
    if ((preferred_dir == 0xb) || (preferred_dir < 0)) {
        while (tile_x >= 0) {
            tile_x--;
            if (combatgrid_tile_passable_check(0xb, tile_x, tile_y, actor) != 0)
                break;
        }
        tile_x = (s16)actor->inner->gridX;
    }
    if ((preferred_dir == 10) || (preferred_dir < 0)) {
        while (tile_x < 8) {
            tile_x++;
            if (combatgrid_tile_passable_check(10, tile_x, tile_y, actor) != 0)
                break;
        }
        tile_x = (s16)actor->inner->gridX;
    }
    if ((preferred_dir == 0xc) || (preferred_dir < 0)) {
        while (tile_y >= 0) {
            tile_y--;
            if (combatgrid_tile_passable_check(0xc, tile_x, tile_y, actor) != 0)
                break;
        }
        tile_y = (s16)actor->inner->gridY;
    }
    if ((preferred_dir == 0xd) || (preferred_dir < 0)) {
        while (tile_y < 0xd) {
            tile_y++;
            if (combatgrid_tile_passable_check(0xd, tile_x, tile_y, actor) != 0)
                return;
        }
    }
    return;
}

void far combatgrid_rndr_engagement_lines(void) {
    s16 i;
    s16 j;

    i = 0;
    if (i < g_combatant_count) {
        do {
            j = 0;
            if (j < g_combatant_count) {
                do {
                    if (i != j) {
                        if (combatgrid_can_engage_same_kind(i, j) != 0) {
                            worldfx_draw_world_line((s16)(char)g_combatant_table[i].tile_x,
                                                    (s16)(char)g_combatant_table[i].tile_y,
                                                    (s16)(char)g_combatant_table[j].tile_x,
                                                    (s16)(char)g_combatant_table[j].tile_y);
                        }
                    }
                    j = j + 1;
                } while (j < g_combatant_count);
            }
            i = i + 1;
        } while (i < g_combatant_count);
    }
    return;
}

void combatgrid_unhighlight_terr_type(unsigned short terrain) {
    s16 x;
    s16 y;

    x = 0;
    do {
        y = 0;
        do {
            if (combatgrid_tile_terrain_field((char)x, (char)y) == terrain) {
                combatgrid_set_tile_effect((char)x, (char)y, 0, -1);
            }
            ++y;
        } while (y < 0xd);
        ++x;
    } while (x < 8);
}

s16 far combatgrid_tile_has_mobility(s16 col, s16 row, s16 tile_count) {
    s16 reachCount;
    s16 hasMobility;
    Combatant tempActor;
    CombatantState tempInner;
    s16 c;
    s16 r;

    reachCount = 0;
    hasMobility = 0;
    if (combatgrid_coord_valid(col, row) == 0) {
        return 0;
    } else {
        g_acting_actor_speed = 0x34;
        tempActor.inner = &tempInner;
        tempInner.gridX = (unsigned char)col;
        tempInner.gridY = (unsigned char)row;
        c = 0;
        do {
            r = 0;
            do {
                if ((c != col) || (r != row)) {
                    if (combatgrid_tile_is_blocked((unsigned char)c, (unsigned char)r) == 0) {
                        tempInner.destX = (unsigned char)c;
                        tempInner.destY = (unsigned char)r;
                        if (combataipath_actor_walk_path(&tempActor, 1) != 0) {
                            reachCount = reachCount + 1;
                            if (tile_count >> 1 < reachCount) {
                                hasMobility = 1;
                                break;
                            }
                        }
                    }
                }
                ++r;
            } while (r < 0xd);
            if (hasMobility != 0)
                break;
            ++c;
        } while (c < 8);
    }
    return hasMobility;
}
