/**
 * @file  SHAPETBL.C
 * @brief Table Store implementation — see @ref SHAPETBL.H. Loads .TBL `DAT:` chunks into
 *        @ref ShapeTable slots and serves @ref Shape records by global index.
 *
 * Also the storage owner of the store's file-scope globals (@ref g_shapeTables,
 * @ref g_shapeTableCount, @ref g_shapeOverride,
 * @c g_shapeOverrideLen and @c g_lightDirNotInit (TBD-ref: canonical externs in GLOBALS.H, which lacks a file block)).
 */
#include <dos.h>

#include "SRC/R3D/SPRITE/WORLDRND.H"
#include "structs.h"
#include "SRC/R3D/TBLSTORE/SHAPETBL.H"
#include "SRC/SYS/FARTHUNK.H"
#include "SRC/SYS/DOSMEM.H"
#include "SRC/R3D/CORE/R3D.H"
#include "SRC/SYS/PANICF.H"
#include "SRC/STREAM/BUFLOAD/CHUNKRD.H"

/**
 * @brief Number of @ref ShapeTable slots in @ref g_shapeTables.
 *
 * Set by @ref ts_init (the clamped shapeTableCapacity) and used as the loop bound of the
 * slot walk in @ref ts_get_shape / @ref ts_set_shape; @ref ts_shutdown resets it to 0.
 */
s16 g_shapeTableCount;

/**
 * @brief Number of entries in @ref g_shapeOverride.
 *
 * Read only inside the override branch of @ref ts_get_shape / @ref ts_set_shape; like its
 * partner it is never assigned and stays 0.
 */
s16 g_shapeOverrideLen;

/**
 * @brief The table-store slot array: @ref g_shapeTableCount @ref ShapeTable slots.
 *
 * Allocated by @ref ts_init and freed by @ref ts_shutdown; each slot is filled from one .TBL
 * `DAT:` chunk by @ref ts_load_shape_tbl. @ref ts_get_shape walks these slots for every lookup
 * unless @ref g_shapeOverride is set.
 */
ShapeTable *g_shapeTables;

Shape far *far *g_shapeOverride = {0};

/**
 * @brief One-shot latch: nonzero until the first @ref ts_init has set the default R3D light direction.
 *
 * Starts at 1 so the very first @ref ts_init applies the default direction (+45°, -135°) and
 * clears this; later re-inits then leave the light untouched.
 */
unsigned char g_lightDirNotInit = 0x01;

void ts_init(s16 shapeTableCapacity, s16 spriteBankCapacity) {
    register s16 i;

    if (shapeTableCapacity < 1)
        shapeTableCapacity = 1;
    if (spriteBankCapacity < 1)
        spriteBankCapacity = 1;

    g_shapeTableCount = shapeTableCapacity;
    g_spriteBankCount = spriteBankCapacity;

    g_shapeTables = my_malloc(g_shapeTableCount * sizeof(ShapeTable));
    g_spriteBanks = my_malloc(g_spriteBankCount * sizeof(PagedArrayEntry));

    if (g_shapeTables == 0 || g_spriteBanks == 0) {
        panic("ts_init : my_malloc");
    }

    for (i = 0; i < g_shapeTableCount; i++) {
        g_shapeTables[i].records = 0;
        g_shapeTables[i].count = 0;
        g_shapeTables[i].scale = SHAPE_SCALE_UNITY;
        g_shapeTables[i].scaleClamp = SHAPE_SCALE_UNITY;
        g_shapeTables[i].spriteBank = -1;
    }

    for (i = 0; i < g_spriteBankCount; i++) {
        g_spriteBanks[i].imageRecord = 0;
        g_spriteBanks[i].nCount = 0;
    }

    if (g_lightDirNotInit != 0) {
        r3d_light_set_direction(R3D_DEG(45), R3D_DEG(-135));
        g_lightDirNotInit = 0;
    }
}

void ts_shutdown(void) {
    g_shapeTableCount = 0;
    g_spriteBankCount = 0;
    my_free(g_shapeTables);
    my_free(g_spriteBanks);
}

Shape far *far *ts_load_shape_tbl(s16 tblIdx, char *fname) {
    union {
        u32 far *ofs; /* the entries before fixup: file offsets */
        Shape far *far *recs;   /* the same entries after fixup */
    } table;
    u32 base;

    /* The DAT: chunk opens with a 0-terminated table of 32-bit file
     * offsets to the Shape records that follow it.  The chunk buffer
     * always lands at offset 0 of a fresh segment (chunkread panics
     * otherwise), so its address read as a long is seg:0000 and plain
     * long addition relocates each entry into a far pointer in place. */
    table.recs = chunkread_read_all_far(fname, "DAT:");
    if (table.recs != 0) {
        base = (u32)table.ofs;
        g_shapeTables[tblIdx].records = table.recs;
        g_shapeTables[tblIdx].scale = SHAPE_SCALE_UNITY;
        g_shapeTables[tblIdx].scaleClamp = SHAPE_SCALE_UNITY;
        g_shapeTables[tblIdx].spriteBank = -1;
        g_shapeTables[tblIdx].count = 0;
        while (*table.ofs != 0) {
            /* An entry is the record's place as a normalized far pointer from the chunk's
             * start: paragraphs in the high word, under 16 bytes in the low one. */
            *table.recs = bak_far_add((void *)(uintptr_t)base, *table.ofs);
            table.ofs++;
            g_shapeTables[tblIdx].count++;
        }
    }
    return table.recs;
}

void far ts_free_shape_tbl(s16 tblIdx) {
    _freemem(g_shapeTables[tblIdx].records);
    g_shapeTables[tblIdx].records = 0;
    g_shapeTables[tblIdx].count = 0;
    g_shapeTables[tblIdx].scale = SHAPE_SCALE_UNITY;
    g_shapeTables[tblIdx].scaleClamp = SHAPE_SCALE_UNITY;
    g_shapeTables[tblIdx].spriteBank = -1;
}

Shape far *ts_get_shape(s16 shapeIdx) {
    s16 i;

    /* An index past every table is no shape. The original took one of 8000h and up for a
       negative index and read the far pointer before its table (zone 9's tile 10,11 has such
       records: czone_load_actors). */
    if (shapeIdx < 0)
        return NULL;
    if (g_shapeOverride != 0) {
        if (shapeIdx < g_shapeOverrideLen) {
            return g_shapeOverride[shapeIdx];
        }
    } else {
        for (i = 0; i < g_shapeTableCount; i++) {
            if (shapeIdx < g_shapeTables[i].count) {
                return g_shapeTables[i].records[shapeIdx];
            }
            shapeIdx -= g_shapeTables[i].count;
        }
    }
    return NULL;
}

Shape far *ts_set_shape(s16 shapeIdx, Shape far *shape) {
    s16 i;

    if (shapeIdx < 0)
        return NULL;
    if (g_shapeOverride != 0) {
        if (shapeIdx < g_shapeOverrideLen) {
            return g_shapeOverride[shapeIdx] = shape;
        }
    } else {
        for (i = 0; i < g_shapeTableCount; i++) {
            if (shapeIdx < g_shapeTables[i].count) {
                return g_shapeTables[i].records[shapeIdx] = shape;
            }
            shapeIdx -= g_shapeTables[i].count;
        }
    }
    return NULL;
}

ViewContext *ts_create_fullscreen_view(WorldEntity *camera) {
    register ViewContext *w;

    w = my_malloc(sizeof(ViewContext));
    if (w == NULL)
        return NULL;

    w->zoom = VIEW_ZOOM_DEFAULT;
    w->camera = camera;
    w->viewRotation.pitch = 0;
    w->viewRotation.roll = 0;
    w->viewRotation.yaw = 0;
    w->viewport.x = 0;
    w->viewport.y = 0;
    w->viewport.width = SCREEN_W_VGA;
    w->viewport.height = SCREEN_H_VGA;
    w->faceMaterials = NULL;
    w->colorRemap = NULL;
    return w;
}

void far ts_my_free(void *ptr) {
    my_free(ptr);
}

s16 ts_get_anim_group_count(s16 shapeIdx) {
    Shape far *p = ts_get_shape(shapeIdx);
    if (!p)
        return 0;
    return p->animGroupCount;
}

s16 far ts_get_anim_frame_count(s16 shapeIdx, register s16 groupId) {
    Shape far *p;
    unsigned char far *d;

    p = ts_get_shape(shapeIdx);
    if (!p)
        return 0;
    if (groupId < 0 || p->animGroupCount <= groupId)
        return 0;
    d = MK_FP(FP_SEG_NORM(p), p->animFrameCounts);
    return d[groupId];
}

s32 far ts_get_scaled_radius(s16 shapeIdx) {
    Shape far *p;

    p = ts_get_shape(shapeIdx);
    if (!p)
        return 0;
    return (s32)p->radius << p->shift;
}

s16 ts_get_shift(s16 shapeIdx) {
    Shape far *p;

    p = ts_get_shape(shapeIdx);
    if (!p)
        return 0;
    return p->shift;
}
