#include <dos.h>
#include "SRC/R3D/SCENE/ZONE.H"
#include "SRC/SCREENS/MAINMENU.H"
#include "SRC/R3D/SCENE/CZONE.H"
#include "SRC/R3D/SCENE/PROXIM.H"
#include "structs.h"
#include "SRC/R3D/VIS/PROXSCAN.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/SYS/MEM.H"
#include "SRC/R3D/CORE/DISTDIR.H"
#include "SRC/R3D/SCENE/WORLDHIT.H"
#include "SRC/COMBAT/GRID/CMBTGRID.H"
#include "SRC/R3D/TBLSTORE/SHAPETBL.H"
#include "SRC/R3D/VIS/VISLIST.H"
#include "SRC/R3D/SCENE/WORLDFRM.H"
#include "SRC/COMBAT/SPELL/SPELLFX.H"
#include "SRC/GAME/ENC/RGNENC.H"
#ifdef V102CD
#include "SRC/SYS/CFGPARSE.H"
#endif

short g_nVisibleEntryCount;
/* The entries in view. The original keeps their 16-bit offsets in the zone pool's segment,
   which natively is larger than 64 KB. */
WorldObject far **g_ppVisibleEntries;
s32 g_aFilterTable[43];
s32 *g_pVisibleEntryDistances;

typedef struct {
    s32 x;
    s32 y;
    s32 z;
} OctPos;

void far proxscan_filter_table_load(void) {
    ResFile *file;

    file = res_fopen("filter.dat", "rb");
    if (g_engine_prefs != (EnginePrefs *)0 && g_engine_prefs->detail_level != 0) {
        res_fseek(file, (u32)((u16)g_engine_prefs->detail_level * 0xac), SEEK_CUR);
    }
    res_fread((void *)g_aFilterTable, 4, 0x2b, file);
    res_fclose(file);
    return;
}

void far proxscan_scratch_alloc(void) {

    g_ppVisibleEntries = galloc_safe_zcalloc(600 * sizeof(WorldObject far *));
    return;
}

void proxscan_scratch_free(void) {
    galloc_zfree(g_ppVisibleEntries);
    return;
}

void proxscan_full(ViewContext *widget) {
    s16 i;
    s32 *party_pos;

    party_pos = &widget->camera->base.pos.xy.nWorld_x;
    g_nVisibleEntryCount = 0;
    for (i = 0; i < g_nCombat_zone_count; i = i + 1) {
        proxscan_run(g_apCombat_zone_actor_lists[i], party_pos);
    }
    proxscan_filter_by_distance(party_pos);
    proxscan_object_list(party_pos);
    return;
}

void proxscan_run(VisibleEntryList *list, s32 *party_pos) {
    short count;
    s16 i;
    WorldObject far *entry_ptr;
    Shape far *prec;
    s32 threshold;
    s32 metric;
    WorldObject far **puOff;
    OctPos local_pos;
    register s32 *plDist;

    count = list->wEntry_count;
    entry_ptr = list->pEntries;
    plDist = g_pVisibleEntryDistances + g_nVisibleEntryCount;
    puOff = g_ppVisibleEntries + g_nVisibleEntryCount;

    for (i = 0; g_nVisibleEntryCount < 600 && i < count; i++) {
        prec = ts_get_shape(entry_ptr->shapeId);
        if (prec->kind != 7 && (g_game_mode != 1 || entry_ptr->shapeId != 0xb5) &&
            (threshold = g_aFilterTable[prec->kind], threshold) != -1) {
            local_pos = *(OctPos far *)&entry_ptr->pos;
            *plDist = distdir_octagonal_distance(party_pos, (s32 *)&local_pos);
            if (threshold != 1) {
                metric = *plDist;
                metric -= (s32)((s16)prec->radius << prec->shift);
            } else {
                metric = 0;
            }
            if (g_game_mode == 2 && *plDist < 0x640 &&
                (prec->kind == 0xe || prec->kind == 0xf || prec->kind == 0x14 ||
                 prec->kind == 0x17) &&
                g_pEncounterTable != 0) {
                worldframe_enc_rec_prox(&list->bZone, i);
            }
            if (metric < threshold) {
                *puOff = entry_ptr;
                puOff++;
                plDist++;
                g_nVisibleEntryCount++;
            }
        }
        entry_ptr++;
    }
}

void proxscan_filter_by_distance(s32 *origin) {
    short max_count;
    s16 i;
    WorldObject far *entry_ptr;
    Shape far *prec;
    s32 threshold;
    s32 metric;
    OctPos local_pos;
    register s32 *plDist;
    register WorldObject far **puOff;

    max_count = g_nVisible_entry_count;
    entry_ptr = (WorldObject far *)g_pVisible_entry_pool;
    plDist = g_pVisibleEntryDistances + g_nVisibleEntryCount;
    puOff = g_ppVisibleEntries + g_nVisibleEntryCount;
    for (i = 0; g_nVisibleEntryCount < 600 && i < max_count; i++) {
        prec = ts_get_shape(entry_ptr->shapeId);
        threshold = g_aFilterTable[prec->kind];
        if (threshold != -1) {
            local_pos = *(OctPos far *)&entry_ptr->pos;
            *plDist = distdir_octagonal_distance(origin, (s32 *)&local_pos);
            if (threshold != 1) {
                metric = *plDist;
                metric -= (s32)((s16)prec->radius << prec->shift);
            } else {
                metric = 0;
            }
            if (metric < threshold) {
                *puOff = entry_ptr;
                puOff++;
                plDist++;
                g_nVisibleEntryCount++;
            }
        }
        entry_ptr++;
    }
}

void proxscan_object_list(s32 *origin) {
    short max_count;
    s16 i;
    WorldObject far *record_ptr;
    s32 score;
    s32 metric;
    OctPos local_pos;
    register s32 *plDist;
    register WorldObject far **puOff;

    max_count = g_nFixed_object_count;
    record_ptr = g_pFixed_object_entries;
    plDist = g_pVisibleEntryDistances + g_nVisibleEntryCount;
    score = g_aFilterTable[16];
    puOff = g_ppVisibleEntries + g_nVisibleEntryCount;
    for (i = 0; g_nVisibleEntryCount < 600 && i < max_count; i++) {
        if (score != -1) {
            local_pos = *(OctPos far *)&record_ptr->pos.xy.nWorld_x;
            *plDist = distdir_octagonal_distance(origin, (s32 *)&local_pos);
            if (score != 1) {
                metric = *plDist;
            } else {
                metric = 0;
            }
            if (metric < score) {
                *puOff = record_ptr;
                puOff++;
                plDist++;
                g_nVisibleEntryCount++;
            }
        }
        record_ptr++;
    }
}

void proxscan_all_zones_for_encounter(ViewContext *pContext) {
    s32 *party_pos;
    s16 i;

    party_pos = &pContext->camera->base.pos.xy.nWorld_x;
    g_nVisibleEntryCount = 0;
    for (i = 0; i < g_nCombat_zone_count; i = i + 1) {
        proxscan_encounter_records(g_apCombat_zone_actor_lists[i], party_pos);
    }
    return;
}

void proxscan_encounter_records(VisibleEntryList *list, s32 *party_pos) {
    short count;
    s16 i;
    WorldObject far *entry_ptr;
    Shape far *prec;
    s32 threshold;
    s32 metric;
    WorldObject far **puOff;
    OctPos local_pos;
    register s32 *plDist;

    count = list->wEntry_count;
    entry_ptr = list->pEntries;
    plDist = g_pVisibleEntryDistances + g_nVisibleEntryCount;
    puOff = g_ppVisibleEntries + g_nVisibleEntryCount;
    for (i = 0; g_nVisibleEntryCount < 600 && i < count; i++) {
        prec = ts_get_shape(entry_ptr->shapeId);
        if ((prec->kind <= 4 || prec->kind == 7 || prec->kind == 10 || prec->kind == 0xe ||
             prec->kind == 0xf || prec->kind == 0x14 || prec->kind == 0x17 || prec->kind == 0x26 ||
             prec->kind == 0x27) &&
            (threshold = g_aFilterTable[prec->kind], threshold) != -1) {
            local_pos = *(OctPos far *)&entry_ptr->pos;
            *plDist = distdir_octagonal_distance(party_pos, (s32 *)&local_pos);
            if (threshold != 1) {
                metric = *plDist;
                metric -= (s32)((s16)prec->radius << prec->shift);
            } else {
                metric = 0;
            }
            if (g_game_mode == 2 && *plDist < 0x640 &&
                (prec->kind == 0xe || prec->kind == 0xf || prec->kind == 0x14 ||
                 prec->kind == 0x17) &&
                g_pEncounterTable != 0) {
                worldframe_enc_rec_prox(&list->bZone, i);
            }
            if (metric < threshold) {
                *puOff = entry_ptr;
                puOff++;
                plDist++;
                g_nVisibleEntryCount++;
            }
        }
        entry_ptr++;
    }
}

s16 proxscan_vis_rec_kind_0e3e(void) {
    WorldObject far *entry_ptr;
    WorldObject far **puOff;
    s32 *plDist;
    register s16 i;

    puOff = g_ppVisibleEntries;
    plDist = g_pVisibleEntryDistances;
    proxscan_full(g_world_widget);
    vislist_sort(g_ppVisibleEntries, g_pVisibleEntryDistances,
                 g_nVisibleEntryCount);
    i = 0;
    if (i < g_nVisibleEntryCount) {
        do {
            entry_ptr = *puOff;
            if (g_game_mode != 2 || *plDist <= 0x2134) {
                if (entry_ptr->shapeId == g_nProximityTableCount) {
                    if (rgnenc_slot_actor_kind_eq_placed(entry_ptr) != 0) {
                        return 1;
                    }
                }
            }
            i++;
            puOff++;
            plDist++;
        } while (i < g_nVisibleEntryCount);
    }
    return 0;
}

s16 far proxscan_paged_find_next_type0f(WorldObject far **out_entry) {
    s16 i;

    for (i = g_nVisibleEntryCount - 1; i >= 0; i--) {
        *out_entry = g_ppVisibleEntries[i];
        if (ts_get_shape((*out_entry)->shapeId)->kind == 0x0f) {
            return 1;
        }
    }
    return 0;
}

void proxscan_visibility(ViewContext *pViewContext) {
    s16 i;
    s32 *party_pos;

    party_pos = &pViewContext->camera->base.pos.xy.nWorld_x;
    g_nVisibleEntryCount = 0;
    for (i = 0; i < g_nCombat_zone_count; i = i + 1) {
        proxscan_region_bucket(g_apCombat_zone_actor_lists[i], party_pos);
    }
    proxscan_world_objects(party_pos);
    return;
}

void proxscan_region_bucket(VisibleEntryList *region, s32 *party_pos) {
    short count;
    s16 i;
    s16 keep;
    WorldObject far *entry_ptr;
    Shape far *prec;
    s32 threshold;
    s32 metric;
    s16 view_xy[3];
    OctPos local_pos;
    register s32 *plDist;
    register WorldObject far **puOff;

    count = region->wEntry_count;
    entry_ptr = region->pEntries;
    plDist = g_pVisibleEntryDistances + g_nVisibleEntryCount;
    puOff = g_ppVisibleEntries + g_nVisibleEntryCount;
    for (i = 0; g_nVisibleEntryCount < 600 && i < count; i++) {
        prec = ts_get_shape(entry_ptr->shapeId);
        local_pos = *(OctPos far *)&entry_ptr->pos;
        keep = 1;
        if (combatgrid_obj_kind_prox_actv(prec->kind) != 0) {
            combatgrid_world_to_view_2d(&g_world_camera->base.pos.xy,
                                        g_world_camera->base.orientation.yaw,
                                        (WorldPos2 far *)&local_pos, view_xy);
            if (view_xy[0] != -1 ||
                distdir_octagonal_distance(party_pos, (s32 *)&local_pos) < 4000)
                keep = 0;
        } else {
            if (prec->kind == 0x2a || entry_ptr->shapeId == 0x7f)
                keep = 0;
        }
        if (keep != 0 && (threshold = g_aFilterTable[prec->kind], threshold) != -1) {
            *plDist = distdir_octagonal_distance(party_pos, (s32 *)&local_pos);
            if (threshold != 1) {
                metric = *plDist;
                metric -= (s32)((s16)prec->radius << prec->shift);
            } else {
                metric = 0;
            }
            if (metric < threshold) {
                *puOff = entry_ptr;
                puOff++;
                plDist++;
                g_nVisibleEntryCount++;
            }
        }
        entry_ptr++;
    }
}

void proxscan_world_objects(s32 *p_pos) {
    register s32 *plDist;
    register WorldObject far **puOff;
    short count;
    s16 i;
    s16 keep;
    WorldObject far *entry_ptr;
    Shape far *prec;
    s32 threshold;
    s32 metric;
    s16 view_xy[3];
    OctPos local_pos;

    count = g_nVisible_entry_count;
    entry_ptr = (WorldObject far *)g_pVisible_entry_pool;
    plDist = g_pVisibleEntryDistances + g_nVisibleEntryCount;
    puOff = g_ppVisibleEntries + g_nVisibleEntryCount;
    for (i = 0; g_nVisibleEntryCount < 600 && i < count; i++) {
        prec = ts_get_shape(entry_ptr->shapeId);
        local_pos = *(OctPos far *)&entry_ptr->pos;
        keep = 1;
        if (combatgrid_obj_kind_prox_actv(prec->kind) != 0) {
            combatgrid_world_to_view_2d(&g_world_camera->base.pos.xy,
                                        g_world_camera->base.orientation.yaw,
                                        (WorldPos2 far *)&local_pos, view_xy);
            if (view_xy[0] != -1 || distdir_octagonal_distance(p_pos, (s32 *)&local_pos) < 4000)
                keep = 0;
        } else {
            if (prec->kind == 0x2a || entry_ptr->shapeId == 0x7f)
                keep = 0;
        }
        if (keep != 0) {
            threshold = g_aFilterTable[prec->kind];
            if (threshold != -1) {
                *plDist = distdir_octagonal_distance(p_pos, (s32 *)&local_pos);
                if (threshold != 1) {
                    metric = *plDist;
                    metric -= (s32)((s16)prec->radius << prec->shift);
                } else {
                    metric = 0;
                }
                if (metric < threshold) {
                    *puOff = entry_ptr;
                    puOff++;
                    plDist++;
                    g_nVisibleEntryCount++;
                }
            }
            entry_ptr++;
        }
    }
}

void proxscan_paged_dispatch_all(void) {
    s16 i;
    s32 *party_pos;
#ifdef V102CD
    short saved_heading;
#endif

    party_pos = &g_world_camera->base.pos.xy.nWorld_x;
#ifdef V102CD
    saved_heading = g_world_camera->base.orientation.yaw;
    if (g_cfgNonRotatingMap) {
        g_world_camera->base.orientation.yaw = 0;
    }
#endif
    for (i = 0; i < g_nCombat_zone_count; i++) {
        proxscan_paged_list_dispatch(g_apCombat_zone_actor_lists[i], party_pos);
    }
    proxscan_paged_dispatch_by_type(party_pos);
#ifdef V102CD
    g_world_camera->base.orientation.yaw = saved_heading;
#endif
}

void proxscan_paged_list_dispatch(VisibleEntryList *list, s32 *party_pos) {
    WorldObject far *entry_ptr;
    Shape far *prec;
    s32 metric;
    WorldPos local_pos;

    register s16 i;
    s16 count;

    count = (s16)list->wEntry_count;
    entry_ptr = list->pEntries;
    i = 0;
    if (i < count) {
        do {
            prec = ts_get_shape(entry_ptr->shapeId);
            switch ((u16)prec->kind) {
            case 0x06:
            case 0x0a:
            case 0x11:
            case 0x1d:
            case 0x1e:
            case 0x1f:
            case 0x21:
            case 0x24:
            case 0x29:
                local_pos = *(WorldPos far *)&entry_ptr->pos;
                metric = distdir_octagonal_distance(party_pos, (s32 *)&local_pos) -
                         ((s16)prec->radius << prec->shift);
                if (metric < 64000) {
                    world_render_record_marker_dot(entry_ptr);
                }
                break;
            }
            entry_ptr++;
            i++;
        } while (i < count);
    }
}

void proxscan_paged_dispatch_by_type(s32 *p_coords) {
    WorldObject far *entry_ptr;
    Shape far *prec;
    s32 metric;
    OctPos local_pos;
    s16 i;

    entry_ptr = (WorldObject far *)g_pVisible_entry_pool;
    i = 0;
    if (i < g_nVisible_entry_count) {
        do {
            prec = ts_get_shape(entry_ptr->shapeId);
            switch ((u16)prec->kind) {
            case 6:
            case 10:
            case 17:
            case 29:
            case 30:
            case 31:
            case 33:
            case 36:
            case 41:
                local_pos = *(OctPos far *)&entry_ptr->pos;
                metric = distdir_octagonal_distance(p_coords, (s32 *)&local_pos) -
                         (s32)((s16)prec->radius << prec->shift);
                if (metric < 64000) {
                    world_render_record_marker_dot(entry_ptr);
                }
                break;
            }
            entry_ptr++;
            i++;
        } while (i < g_nVisible_entry_count);
    }
}

void far proxscan_draw_cmap_inset_markers(void) {
    s32 *position;
    s16 i;
#ifdef V102CD
    short saved_heading;
#endif

    position = &g_world_camera->base.pos.xy.nWorld_x;
#ifdef V102CD
    saved_heading = g_world_camera->base.orientation.yaw;
    if (g_cfgNonRotatingMap) {
        g_world_camera->base.orientation.yaw = 0;
    }
#endif
    for (i = 0; i < g_nCombat_zone_count; i++) {
        proxscan_draw_zone_vis_marks(g_apCombat_zone_actor_lists[i], position);
    }
    proxscan_draw_visible_markers(position);
    proxscan_draw_fixed_object_marks(position);
#ifdef V102CD
    g_world_camera->base.orientation.yaw = saved_heading;
#endif
    return;
}

void far proxscan_draw_zone_vis_marks(VisibleEntryList *list, s32 *position) {
    OctPos local_pos;
    WorldObject far *entry_ptr;
    Shape far *prec;
    s32 dist;
    s16 count;
    s16 i;

    count = (s16)list->wEntry_count;
    entry_ptr = list->pEntries;
    i = 0;
    if (i < count) {
        do {
            prec = ts_get_shape(entry_ptr->shapeId);
            switch (prec->kind) {
            case 6:
            case 10:
            case 12:
            case 16:
            case 17:
            case 26:
            case 27:
            case 28:
            case 30:
            case 31:
            case 33:
            case 35:
            case 41:
                local_pos = *(OctPos far *)&entry_ptr->pos.xy.nWorld_x;
                dist = distdir_octagonal_distance(position, (s32 *)&local_pos) -
                       (s32)((s16)prec->radius << prec->shift);
                if (dist < 64000) {
                    if (spellfx_actor_has_ration(entry_ptr) != 0) {
                        world_render_record_marker_dot(entry_ptr);
                    }
                }
                break;
            }
            entry_ptr++;
            i++;
        } while (i < count);
    }
    return;
}

void far proxscan_draw_visible_markers(s32 *p_position) {
    WorldObject far *entry_ptr;
    Shape far *prec;
    s32 dist;
    OctPos local_pos;
    s16 i;

    entry_ptr = (WorldObject far *)g_pVisible_entry_pool;
    i = 0;
    if (i < g_nVisible_entry_count) {
        do {
            prec = ts_get_shape(entry_ptr->shapeId);
            switch (prec->kind) {
            case 6:
            case 10:
            case 12:
            case 16:
            case 17:
            case 26:
            case 27:
            case 28:
            case 30:
            case 31:
            case 33:
            case 35:
            case 41:
                local_pos = *(OctPos far *)&entry_ptr->pos.xy.nWorld_x;
                dist = distdir_octagonal_distance(p_position, (s32 *)&local_pos) -
                       (s32)((s16)prec->radius << prec->shift);
                if (dist < 64000) {
                    if (spellfx_actor_has_ration(entry_ptr) != 0) {
                        world_render_record_marker_dot(entry_ptr);
                    }
                }
                break;
            }
            entry_ptr++;
            i++;
        } while (i < g_nVisible_entry_count);
    }
    return;
}

void far proxscan_draw_fixed_object_marks(s32 *p_position) {
    OctPos local_pos;
    WorldObject far *record_ptr;
    s16 i;

    record_ptr = g_pFixed_object_entries;
    for (i = 0; i < g_nFixed_object_count; i++) {
        local_pos = *(OctPos far *)&record_ptr->pos.xy.nWorld_x;
        if (distdir_octagonal_distance(p_position, (s32 *)&local_pos) < 64000) {
            if (spellfx_actor_has_ration((WorldObject far *)record_ptr) != 0) {
                world_render_record_marker_dot((WorldObject far *)record_ptr);
            }
        }
        record_ptr++;
    }
    return;
}

void far proxscan_broadcast_scene_events(void) {
    s32 *position;
    s16 i;
#ifdef V102CD
    short saved_heading;
#endif

    position = &g_world_camera->base.pos.xy.nWorld_x;
#ifdef V102CD
    saved_heading = g_world_camera->base.orientation.yaw;
    if (g_cfgNonRotatingMap) {
        g_world_camera->base.orientation.yaw = 0;
    }
#endif
    for (i = 0; i < g_nCombat_zone_count; i = i + 1) {
        proxscan_rndr_zone_recs_near(g_apCombat_zone_actor_lists[i], position);
    }
    proxscan_draw_vis_marks_near(position);
    proxscan_notify_actors_near(position);
#ifdef V102CD
    g_world_camera->base.orientation.yaw = saved_heading;
#endif
    return;
}

void far proxscan_rndr_zone_recs_near(VisibleEntryList *list, s32 *position) {
    OctPos local_pos;
    WorldObject far *entry_ptr;
    Shape far *prec;
    s32 dist;
    s16 count;
    s16 i;

    count = (s16)list->wEntry_count;
    entry_ptr = list->pEntries;
    i = 0;
    if (i < count) {
        do {
            prec = ts_get_shape(entry_ptr->shapeId);
            switch (prec->kind) {
            case 6:
            case 9:
            case 10:
            case 12:
            case 16:
            case 17:
            case 24:
            case 26:
            case 27:
            case 28:
            case 29:
            case 30:
            case 31:
            case 33:
            case 35:
            case 37:
            case 41:
                local_pos = *(OctPos far *)&entry_ptr->pos.xy.nWorld_x;
                dist = distdir_octagonal_distance(position, (s32 *)&local_pos) -
                       (s32)((s16)prec->radius << prec->shift);
                if (dist < 64000) {
                    if ((prec->kind == 0x1d || prec->kind == 9 || prec->kind == 0x25) ||
                        spellfx_actor_has_special_item(entry_ptr) != 0) {
                        world_render_record_marker_dot(entry_ptr);
                    }
                }
                break;
            }
            entry_ptr++;
            i++;
        } while (i < count);
    }
    return;
}

void far proxscan_draw_vis_marks_near(s32 *position) {
    OctPos local_pos;
    WorldObject far *entry_ptr;
    Shape far *prec;
    s32 dist;
    s16 i;

    entry_ptr = (WorldObject far *)g_pVisible_entry_pool;
    i = 0;
    if (i < g_nVisible_entry_count) {
        do {
            prec = ts_get_shape(entry_ptr->shapeId);
            switch (prec->kind) {
            case 6:
            case 9:
            case 10:
            case 12:
            case 16:
            case 17:
            case 24:
            case 26:
            case 27:
            case 28:
            case 29:
            case 30:
            case 31:
            case 33:
            case 35:
            case 37:
            case 41:
                local_pos = *(OctPos far *)&entry_ptr->pos.xy.nWorld_x;
                dist = distdir_octagonal_distance(position, (s32 *)&local_pos) -
                       (s32)((s16)prec->radius << prec->shift);
                if (dist < 64000) {
                    if ((prec->kind == 0x1d || prec->kind == 9 || prec->kind == 0x25) ||
                        spellfx_actor_has_special_item(entry_ptr) != 0) {
                        world_render_record_marker_dot(entry_ptr);
                    }
                }
                break;
            }
            entry_ptr++;
            i++;
        } while (i < g_nVisible_entry_count);
    }
    return;
}

void far proxscan_notify_actors_near(s32 *position) {
    OctPos local_pos;
    WorldObject far *record_ptr;
    s16 i;

    record_ptr = g_pFixed_object_entries;
    for (i = 0; i < g_nFixed_object_count; i++) {
        local_pos = *(OctPos far *)&record_ptr->pos.xy.nWorld_x;
        if (distdir_octagonal_distance(position, (s32 *)&local_pos) < 64000) {
            if (spellfx_actor_has_special_item((WorldObject far *)record_ptr) != 0) {
                world_render_record_marker_dot((WorldObject far *)record_ptr);
            }
        }
        record_ptr++;
    }
    return;
}
