/*
 * The 3-D scene as the software pass draws it, frame by frame, for the GPU's view of the world
 * (M7; the scene list in scene.h, the hooks in bak_port.h). R3D's producers hand over each face
 * before the near clip (port/asm/r3d.c), actorrender_entity each object (ACTRENDR.C), the sinks
 * the polygon each face became and what drew it (WORLDRND.C, SKY.C), the billboard and circle
 * renderers theirs (WORLDRND.C), and the sky renderer its bands and panorama (SKYREND.C, SKY.C).
 * Nothing is kept unless asked: scene_enable, or BAK_SCENE_CHECK.
 *
 * With BAK_SCENE_CHECK set, what it is handed is checked. A face that reached its sink whole (no
 * near clip, no viewport clip, as many corners) must be its corners projected, in order, with
 * the zone's focal length and the viewport's centre; a clipped one must keep those of its
 * corners in front of the view plane that project inside the clip rectangle. Both within two
 * pixels: the original truncates its divisions, and LOD 2's long scaler drops bits (a pixel and
 * a bit off). Every corner, its level of detail's units made world units, must lie near its
 * object's origin (within twice the object's bounding half extent), which a wrong unit would
 * break; billboards' anchors and circles' centres likewise, and they must project where the
 * software drew them. The counts and the largest deviations go to stderr at exit.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "platform.h"
#include "scene.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "gfx169d.h"
#include "SRC/GEN/GFXCTX.H"
#include "bak_game_end.h"

#define TOLERANCE 2.0 /* pixels */
#define PAGES 4       /* the pages whose last frames are kept */
#define FACE_TEXTURED 0x10

static int s_check = -1; /* BAK_SCENE_CHECK: not looked up yet */
static bool s_enabled;

static void (*s_finishedHook)(const SceneFrame *);
static SceneFrame s_build;       /* the frame being drawn */
static SceneFrame s_done[PAGES]; /* the last finished frame of each page */
static uint32_t s_serial;

static struct {
    int32_t index; /* in the frame's objects, -1 for none */
    double x, y, z, extent;
    int unitShift;
} s_object = {-1};

static struct {
    BakSceneFace face; /* the last face */
    int32_t item;      /* its item, while it may still reach a sink; -1 otherwise */
    bool atSink;
} s_pending = {.item = -1};

static struct {
    unsigned long long frames, faces, whole, partly, mismatched, unchecked, strayCorners;
    unsigned long long textured, vertical, reordered, unsteady, disagree, billboards, circles,
        offTarget, strayAnchors, strips, skyOps, skyDropped, mixedPages;
} s_counts;
static double s_worst[4]; /* the largest deviation within the tolerance, by level of detail */
static double s_worstAnchor;
static int s_shown; /* mismatches described so far */

/* ---- textures -------------------------------------------------------------------- */

enum {
    COLUMNS,
    COLUMNS_UNPACKED,
    DRIVER_ROWS
}; /* how a blit reads an image */

static SceneTexture *s_textures; /* by id - 1 */
static uint32_t s_textureCount, s_textureCap;
static uint32_t *s_byContent; /* texture ids by content hash, open addressing */
static uint32_t *s_contentHash;
static uint32_t s_contentCap;

/* The records seen, by address: what they held then and the texture made of it. */
typedef struct RecordSeen {
    const ImageRecord *record; /* NULL: empty; GONE: freed */
    const unsigned char *pixels;
    uint32_t extent;
    int16_t width, height;
    uint16_t flags;
    uint8_t layout;
    uint32_t texture;
} RecordSeen;
#define GONE ((const ImageRecord *)(uintptr_t)1)
static RecordSeen *s_records;
static uint32_t s_recordCap, s_recordUsed;

static uint32_t mix(uintptr_t p) {
    uint64_t v = (uint64_t)p * 0x9E3779B97F4A7C15ull;
    return (uint32_t)(v >> 32);
}

/* An image's palette indices row by row, as its blit reads them; false if it reads nothing. */
static bool decode(const ImageRecord *r, int layout, int16_t *w, int16_t *h, uint8_t **out) {
    int width = r->nWidth, height = r->nHeight;
    const unsigned char *src = r->pImageBase + r->wImageOff;
    size_t avail = r->nImageExtent > r->wImageOff ? r->nImageExtent - r->wImageOff : 0;
    if (layout == DRIVER_ROWS)
        width &= ~3; /* a row's last width & 3 pixels are never drawn */
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return false;
    size_t n = (size_t)width * (size_t)height;
    uint8_t *rows = calloc(n, 1), *cols = NULL;
    if (rows == NULL)
        return false;
    if (layout == DRIVER_ROWS) {
        memcpy(rows, src, SDL_min(n, avail));
    } else {
        cols = calloc(n, 1);
        if (cols == NULL) {
            free(rows);
            return false;
        }
        if (layout == COLUMNS_UNPACKED) { /* RLE: n | 80h then a byte, n copies; n, n bytes */
            size_t si = 0, o = 0;
            while (si < avail && o < n) {
                unsigned c = src[si++];
                if (c == 0)
                    break;
                if (c & 0x80) {
                    unsigned char v = si < avail ? src[si++] : 0;
                    for (c &= 0x7f; c != 0 && o < n; c--)
                        cols[o++] = v;
                } else {
                    for (; c != 0 && o < n && si < avail; c--)
                        cols[o++] = src[si++];
                }
            }
        } else {
            memcpy(cols, src, SDL_min(n, avail));
        }
        for (int x = 0; x < width; x++)
            for (int y = 0; y < height; y++)
                rows[(size_t)y * (size_t)width + (size_t)x] =
                    cols[(size_t)x * (size_t)height + (size_t)y];
        free(cols);
    }
    *w = (int16_t)width;
    *h = (int16_t)height;
    *out = rows;
    return true;
}

static bool grow(void **array, uint32_t *cap, uint32_t need, size_t size) {
    if (need <= *cap)
        return true;
    uint32_t n = *cap ? *cap : 64;
    while (n < need)
        n *= 2;
    void *p = realloc(*array, (size_t)n * size);
    if (p == NULL)
        return false;
    *array = p;
    *cap = n;
    return true;
}

/* The texture of these indices: an equal one made before, or a new one (which keeps them). */
static uint32_t texture_of(int16_t w, int16_t h, uint8_t *indices) {
    size_t n = (size_t)w * (size_t)h;
    uint32_t hash = platform_hash((uint32_t)w << 16 | (uint16_t)h, indices, n);
    if (s_textureCount * 2 + 2 > s_contentCap) { /* rehash into twice the room */
        uint32_t cap = s_contentCap ? s_contentCap * 2 : 1024;
        uint32_t *ids = calloc(cap, sizeof *ids), *hashes = calloc(cap, sizeof *hashes);
        if (ids == NULL || hashes == NULL) {
            free(ids);
            free(hashes);
            free(indices);
            return 0;
        }
        for (uint32_t i = 0; i < s_contentCap; i++) {
            if (s_byContent[i] == 0)
                continue;
            uint32_t at = s_contentHash[i] & (cap - 1);
            while (ids[at] != 0)
                at = (at + 1) & (cap - 1);
            ids[at] = s_byContent[i];
            hashes[at] = s_contentHash[i];
        }
        free(s_byContent);
        free(s_contentHash);
        s_byContent = ids;
        s_contentHash = hashes;
        s_contentCap = cap;
    }
    uint32_t at = hash & (s_contentCap - 1);
    for (; s_byContent[at] != 0; at = (at + 1) & (s_contentCap - 1)) {
        const SceneTexture *t = &s_textures[s_byContent[at] - 1];
        if (s_contentHash[at] == hash && t->width == w && t->height == h &&
            memcmp(t->indices, indices, n) == 0) {
            free(indices);
            return t->id;
        }
    }
    if (!grow((void **)&s_textures, &s_textureCap, s_textureCount + 1, sizeof *s_textures)) {
        free(indices);
        return 0;
    }
    SceneTexture *t = &s_textures[s_textureCount++];
    t->id = s_textureCount;
    t->width = w;
    t->height = h;
    t->indices = indices;
    s_byContent[at] = t->id;
    s_contentHash[at] = hash;
    return t->id;
}

static RecordSeen *record_slot(const ImageRecord *r) {
    uint32_t at = mix((uintptr_t)r) & (s_recordCap - 1);
    RecordSeen *free_ = NULL;
    for (;; at = (at + 1) & (s_recordCap - 1)) {
        RecordSeen *s = &s_records[at];
        if (s->record == r)
            return s;
        if (s->record == GONE && free_ == NULL)
            free_ = s;
        if (s->record == NULL)
            return free_ != NULL ? free_ : s;
    }
}

/* The texture an image is as its blit reads it (0 for none): the one made when the record was
 * last seen holding the same, or a new one. */
static uint32_t texture_id(const ImageRecord *r, int layout) {
    if (r == NULL)
        return 0;
    if ((s_recordUsed + 1) * 2 > s_recordCap) { /* rehash, dropping the freed */
        uint32_t cap = s_recordCap ? s_recordCap * 2 : 1024;
        RecordSeen *old = s_records;
        uint32_t oldCap = s_recordCap;
        s_records = calloc(cap, sizeof *s_records);
        if (s_records == NULL) {
            s_records = old;
            return 0;
        }
        s_recordCap = cap;
        s_recordUsed = 0;
        for (uint32_t i = 0; i < oldCap; i++) {
            if (old[i].record == NULL || old[i].record == GONE)
                continue;
            *record_slot(old[i].record) = old[i];
            s_recordUsed++;
        }
        free(old);
    }
    RecordSeen *s = record_slot(r);
    uint16_t flags = r->wUnk4;
    if (s->record == r && s->pixels == r->pImageBase + r->wImageOff &&
        s->extent == r->nImageExtent && s->width == r->nWidth && s->height == r->nHeight &&
        s->flags == flags && s->layout == layout)
        return s->texture;
    int16_t w, h;
    uint8_t *indices;
    uint32_t id = decode(r, layout, &w, &h, &indices) ? texture_of(w, h, indices) : 0;
    if (s->record != r)
        s_recordUsed++;
    *s = (RecordSeen){r,
                      r->pImageBase + r->wImageOff,
                      r->nImageExtent,
                      r->nWidth,
                      r->nHeight,
                      flags,
                      (uint8_t)layout,
                      id};
    return id;
}

void bak_scene_images_freed(struct ImageRecord *const *table) {
    if (table == NULL || s_recordCap == 0)
        return;
    for (; *table != NULL; table++) {
        RecordSeen *s = record_slot(*table);
        if (s->record == *table)
            s->record = GONE;
    }
}

const SceneTexture *scene_texture(uint32_t id) {
    return id >= 1 && id <= s_textureCount ? &s_textures[id - 1] : NULL;
}

/* ---- frames ---------------------------------------------------------------------- */

static void report(void);

bool bak_scene_capturing(void) {
    if (s_check < 0) {
        const char *env = SDL_getenv("BAK_SCENE_CHECK");
        s_check = env != NULL && env[0] != '\0' && env[0] != '0';
        if (s_check)
            atexit(report);
    }
    return s_check == 1 || s_enabled;
}

void scene_enable(bool on) {
    s_enabled = on;
}

static void copy_camera(SceneFrame *to, const SceneFrame *from) {
    memcpy(to->camPos, from->camPos, sizeof to->camPos);
    memcpy(to->camAngles, from->camAngles, sizeof to->camAngles);
    memcpy(to->clip, from->clip, sizeof to->clip);
    to->centerX = from->centerX;
    to->centerY = from->centerY;
    to->screenShift = from->screenShift;
}

/* Empty, keeping the arrays' room. */
static void frame_clear(SceneFrame *f) {
    SceneItem *items = f->items;
    SceneVertex *vertices = f->vertices;
    SceneObject *objects = f->objects;
    uint32_t itemCap = f->itemCap, vertexCap = f->vertexCap, objectCap = f->objectCap;
    memset(f, 0, sizeof *f);
    f->items = items;
    f->itemCap = itemCap;
    f->vertices = vertices;
    f->vertexCap = vertexCap;
    f->objects = objects;
    f->objectCap = objectCap;
}

static void frame_free(SceneFrame *f) {
    free(f->items);
    free(f->vertices);
    free(f->objects);
    memset(f, 0, sizeof *f);
}

/* A face still waiting for its sink never reached one. */
static void settle_pending(void) {
    if (s_pending.item < 0)
        return;
    SceneItem *it = &s_build.items[s_pending.item];
    if (!s_pending.atSink)
        it->fate = (it->flags & FACE_TEXTURED) && s_pending.face.frontMask != 0 ? SCENE_DROPPED
                                                                                : SCENE_CLIPPED;
    s_pending.item = -1;
}

void scene_finish(void) {
    settle_pending();
    s_object.index = -1;
    if (s_build.itemCount == 0 && s_build.skyCount == 0)
        return;
    unsigned slot = 0;
    for (unsigned i = 0; i < PAGES; i++) {
        if (s_done[i].serial != 0 && s_done[i].page == s_build.page) {
            slot = i;
            break;
        }
        if (s_done[i].serial < s_done[slot].serial)
            slot = i;
    }
    s_counts.frames++;
    s_counts.mixedPages += s_build.mixedPages;
    SceneFrame done = s_done[slot];
    s_done[slot] = s_build;
    s_build = done;
    frame_clear(&s_build);
    copy_camera(&s_build, &s_done[slot]);
    if (s_finishedHook != NULL)
        s_finishedHook(&s_done[slot]);
}

void scene_set_finished_hook(void (*hook)(const SceneFrame *frame)) {
    s_finishedHook = hook;
}

const uint8_t *scene_remap_tables(void) {
    return g_polyrastTables;
}

void bak_scene_end(void) {
    if (bak_scene_capturing())
        scene_finish();
}

void scene_page_flipped(uint16_t page) {
    if (bak_scene_capturing() && s_build.page == page)
        scene_finish();
}

const SceneFrame *scene_latest(uint16_t page) {
    for (unsigned i = 0; i < PAGES; i++)
        if (s_done[i].serial != 0 && s_done[i].page == page)
            return &s_done[i];
    return NULL;
}

void scene_reset(void) {
    frame_free(&s_build);
    for (unsigned i = 0; i < PAGES; i++)
        frame_free(&s_done[i]);
    for (uint32_t i = 0; i < s_textureCount; i++)
        free(s_textures[i].indices);
    free(s_textures);
    free(s_byContent);
    free(s_contentHash);
    free(s_records);
    s_textures = NULL;
    s_byContent = s_contentHash = NULL;
    s_records = NULL;
    s_textureCount = s_textureCap = s_contentCap = s_recordCap = s_recordUsed = 0;
    s_serial = 0;
    s_object.index = -1;
    s_pending.item = -1;
}

void bak_scene_begin(const BakSceneCamera *camera) {
    if (!bak_scene_capturing())
        return;
    scene_finish();
    frame_clear(&s_build); /* a frame that drew nothing may still hold a horizon */
    memcpy(s_build.camPos, camera->pos, sizeof s_build.camPos);
    memcpy(s_build.camAngles, camera->angles, sizeof s_build.camAngles);
    memcpy(s_build.clip, camera->clip, sizeof s_build.clip);
    s_build.centerX = camera->centerX;
    s_build.centerY = camera->centerY;
    s_build.screenShift = camera->screenShift;
}

/* Something is drawn into the destination page. */
static void note_page(void) {
    uint16_t page = g_graphics_context.wGfxBlitDstPage;
    if (s_build.page == 0) {
        s_build.page = page;
        s_build.background = page == g_graphics_context.wVgaPage1Base;
    }
    else if (s_build.page != page)
        s_build.mixedPages = true;
    if (s_build.serial == 0)
        s_build.serial = ++s_serial;
}

static SceneItem *new_item(uint8_t type) {
    if (!grow((void **)&s_build.items, &s_build.itemCap, s_build.itemCount + 1,
              sizeof *s_build.items))
        return NULL;
    note_page();
    SceneItem *it = &s_build.items[s_build.itemCount++];
    memset(it, 0, sizeof *it);
    it->type = type;
    it->remap = -1;
    it->object = s_object.index >= 0 ? (uint16_t)s_object.index : SCENE_NO_OBJECT;
    it->firstVertex = s_build.vertexCount;
    return it;
}

static bool add_vertex(double x, double y, double z) {
    if (!grow((void **)&s_build.vertices, &s_build.vertexCap, s_build.vertexCount + 1,
              sizeof *s_build.vertices))
        return false;
    s_build.vertices[s_build.vertexCount++] = (SceneVertex){(float)x, (float)y, (float)z};
    return true;
}

/* The remap table's row, from the blit's offset (0: none). */
static int8_t remap_row(int16_t offset) {
    if (offset == 0)
        return -1;
    return (int8_t)(((uint16_t)offset - FOG_REMAP_TAB_OFF) >> 8);
}

/* Whether a point near the current object (world units) lies within twice its extent. */
static bool near_object(double x, double y, double z, double slack) {
    double dx = x - s_object.x, dy = y - s_object.y, dz = z - s_object.z;
    return sqrt(dx * dx + dy * dy + dz * dz) <= 2.0 * s_object.extent + slack + 16.0;
}

/* ---- the hooks -------------------------------------------------------------------- */

void bak_scene_object(int32_t x, int32_t y, int32_t z, int32_t halfExtent, int unitShift,
                      uint8_t priority, const void *shape) {
    if (!bak_scene_capturing())
        return;
    settle_pending();
    s_object.x = x;
    s_object.y = y;
    s_object.z = z;
    s_object.extent = halfExtent;
    s_object.unitShift = unitShift;
    s_object.index = -1;
    if (!grow((void **)&s_build.objects, &s_build.objectCap, s_build.objectCount + 1,
              sizeof *s_build.objects) ||
        s_build.objectCount >= SCENE_NO_OBJECT)
        return;
    s_object.index = (int32_t)s_build.objectCount;
    s_build.objects[s_build.objectCount++] =
        (SceneObject){{(float)x, (float)y, (float)z}, (float)halfExtent, priority, shape};
}

void bak_scene_face(const BakSceneFace *face) {
    if (!bak_scene_capturing())
        return;
    settle_pending();
    s_pending.face = *face;
    s_pending.atSink = false;
    s_counts.faces++;
    SceneItem *it = new_item(SCENE_FACE);
    if (it == NULL)
        return;
    it->corners = face->corners;
    it->lod = face->lod;
    it->flags = face->flags;
    it->filled = face->filled;
    it->fill = face->fill;
    it->outline = face->outline;
    it->fate = SCENE_CLIPPED;
    it->id = face->id;
    for (unsigned i = 0; i < 4; i++)
        it->imageCorner[i] = (uint8_t)i;
    s_pending.item = (int32_t)(it - s_build.items);
    double scale = ldexp(1.0, s_object.unitShift);
    bool stray = false;
    for (unsigned i = 0; i < face->corners; i++) {
        double x = face->cam[i][0] * scale, y = face->cam[i][1] * scale,
               z = face->cam[i][2] * scale;
        add_vertex(x, y, z);
        /* The extent bounds the mesh about its origin; the corners' units round them. */
        if (s_object.index >= 0 && !near_object(x, y, z, 2.0 * scale))
            stray = true;
    }
    s_counts.strayCorners += stray;
}

static void mismatch(const BakSceneFace *f, unsigned i, double px, double py, unsigned n,
                     const int16_t *xs, const int16_t *ys) {
    s_counts.mismatched++;
    if (s_shown++ >= 8)
        return;
    fprintf(stderr,
            "scene: a LOD %u face (%s%s) of %u corners, corner %u (%ld, %ld, %ld) projects to "
            "(%.1f, %.1f); its polygon:",
            f->lod, f->nearClipped ? "near-clipped" : "in front",
            f->viewportClipped ? ", viewport-clipped" : "", f->corners, i, (long)f->cam[i][0],
            (long)f->cam[i][1], (long)f->cam[i][2], px, py);
    for (unsigned k = 0; k < n && k < 10; k++)
        fprintf(stderr, " (%d, %d)", xs[k], ys[k]);
    fprintf(stderr, "\n");
}

/* How far a projected point is from a pixel, in pixels (the larger axis). */
static double deviation(double px, double py, int16_t x, int16_t y) {
    return SDL_max(fabs(px - x), fabs(py - y));
}

static void check_polygon(unsigned n, const int16_t *xs, const int16_t *ys, bool whole) {
    const BakSceneFace *f = &s_pending.face;
    double focal = ldexp(1.0, f->screenShift);
    unsigned checked = 0;
    for (unsigned i = 0; i < f->corners; i++) {
        if (!(f->frontMask & 1u << i) || f->cam[i][1] <= 0)
            continue;
        double depth = f->cam[i][1];
        double px = f->centerX + f->cam[i][0] * focal / depth;
        double py = f->centerY - f->cam[i][2] * focal / depth;
        double off;
        if (whole) {
            off = deviation(px, py, xs[i], ys[i]);
        } else {
            if (px < f->clip[0] + 1 || px > f->clip[2] - 1 || py < f->clip[1] + 1 ||
                py > f->clip[3] - 1)
                continue;
            off = 1e9;
            for (unsigned k = 0; k < n; k++)
                off = SDL_min(off, deviation(px, py, xs[k], ys[k]));
        }
        if (off > TOLERANCE) {
            mismatch(f, i, px, py, n, xs, ys);
            return;
        }
        if (off > s_worst[f->lod & 3])
            s_worst[f->lod & 3] = off;
        checked++;
    }
    if (whole)
        s_counts.whole++;
    else if (checked)
        s_counts.partly++;
    else
        s_counts.unchecked++;
}

void bak_scene_polygon(unsigned n, const int16_t *xs, const int16_t *ys, _Bool textured,
                       uint8_t filled, uint8_t fill, uint8_t outline) {
    if (!bak_scene_capturing() || s_pending.item < 0 || s_pending.atSink)
        return;
    const BakSceneFace *f = &s_pending.face;
    SceneItem *it = &s_build.items[s_pending.item];
    bool whole = !f->nearClipped && !f->viewportClipped && n == f->corners;
    s_pending.atSink = true;
    it->whole = whole;
    it->rect[0] = it->rect[2] = n > 0 ? xs[0] : 0;
    it->rect[1] = it->rect[3] = n > 0 ? ys[0] : 0;
    for (unsigned i = 1; i < n; i++) {
        it->rect[0] = SDL_min(it->rect[0], xs[i]);
        it->rect[1] = SDL_min(it->rect[1], ys[i]);
        it->rect[2] = SDL_max(it->rect[2], xs[i]);
        it->rect[3] = SDL_max(it->rect[3], ys[i]);
    }
    if (textured) {
        it->fate = SCENE_FOGGED; /* until its image comes */
    } else {
        it->fate = SCENE_DRAWN;
        it->filled = filled;
        it->fill = fill;
        it->outline = outline;
    }
    if (s_check == 1)
        check_polygon(n, xs, ys, whole);
}

/* The faces' orders the textured quads were drawn in, by face, for the check: whether a face
 * keeps one order from view to view. */
static struct {
    const void **ids;
    uint8_t *orders; /* the first order seen; 0xff once another came */
    uint32_t cap, used;
} s_orders;

static void note_order(const void *id, uint8_t order) {
    if ((s_orders.used + 1) * 2 > s_orders.cap) {
        uint32_t cap = s_orders.cap ? s_orders.cap * 2 : 1024;
        const void **ids = calloc(cap, sizeof *ids);
        uint8_t *orders = calloc(cap, 1);
        if (ids == NULL || orders == NULL) {
            free(ids);
            free(orders);
            return;
        }
        for (uint32_t i = 0; i < s_orders.cap; i++) {
            if (s_orders.ids[i] == NULL)
                continue;
            uint32_t at = mix((uintptr_t)s_orders.ids[i]) & (cap - 1);
            while (ids[at] != NULL)
                at = (at + 1) & (cap - 1);
            ids[at] = s_orders.ids[i];
            orders[at] = s_orders.orders[i];
        }
        free(s_orders.ids);
        free(s_orders.orders);
        s_orders.ids = ids;
        s_orders.orders = orders;
        s_orders.cap = cap;
    }
    uint32_t at = mix((uintptr_t)id) & (s_orders.cap - 1);
    for (; s_orders.ids[at] != NULL; at = (at + 1) & (s_orders.cap - 1)) {
        if (s_orders.ids[at] != id)
            continue;
        if (s_orders.orders[at] != order && s_orders.orders[at] != 0xff) {
            s_orders.orders[at] = 0xff;
            s_counts.unsteady++;
        }
        return;
    }
    s_orders.ids[at] = id;
    s_orders.orders[at] = order;
    s_orders.used++;
}

/*
 * The order emsimg_polygon_quad_canonicalize puts a quad's corners in, by the idx of the corner
 * each place takes: a quad whose sides 0-1 and 2-3 are vertical turns a corner on, and one with
 * vertical sides 0-3 and 1-2 then takes the upper end of each first. Corner i is then image
 * corner i. A side is vertical where both ends have the same a (and b); corner i is below
 * corner j where down[i] > down[j].
 */
static bool canonical_order(const int64_t *a, const int64_t *b, const int64_t *down, uint8_t *idx) {
    int64_t ka[4], kb[4], d[4];
    for (int i = 0; i < 4; i++) {
        ka[i] = a[i];
        kb[i] = b[i];
        d[i] = down[i];
        idx[i] = (uint8_t)i;
    }
#define VERTICAL(i, j) (ka[i] == ka[j] && kb[i] == kb[j])
#define SWAP(i, j)                                                                                 \
    do {                                                                                           \
        int64_t t;                                                                                 \
        uint8_t u;                                                                                 \
        t = ka[i], ka[i] = ka[j], ka[j] = t;                                                       \
        t = kb[i], kb[i] = kb[j], kb[j] = t;                                                       \
        t = d[i], d[i] = d[j], d[j] = t;                                                           \
        u = idx[i], idx[i] = idx[j], idx[j] = u;                                                   \
    } while (0)
    if (VERTICAL(0, 1) && VERTICAL(2, 3)) { /* turn a corner on: 1 2 3 0 */
        SWAP(0, 1);
        SWAP(1, 2);
        SWAP(2, 3);
    }
    if (!(VERTICAL(0, 3) && VERTICAL(1, 2)))
        return false;
    if (d[0] > d[3])
        SWAP(0, 3);
    if (d[1] > d[2])
        SWAP(1, 2);
    return true;
#undef VERTICAL
#undef SWAP
}

void bak_scene_texture(const struct ImageRecord *image, int16_t remap, const int16_t *xs,
                       const int16_t *ys) {
    if (!bak_scene_capturing() || s_pending.item < 0 || !s_pending.atSink)
        return;
    SceneItem *it = &s_build.items[s_pending.item];
    const BakSceneFace *f = &s_pending.face;
    it->fate = SCENE_DRAWN;
    it->textured = 1;
    it->texture = texture_id(image, COLUMNS);
    it->remap = remap_row(remap);
    /* The software's order, from the polygon it drew: the screen's x, its y down. */
    int64_t sx[4], none[4] = {0}, sy[4];
    uint8_t drawn[4];
    for (int i = 0; i < 4; i++) {
        sx[i] = xs[i];
        sy[i] = ys[i];
    }
    it->vertical = canonical_order(sx, none, sy, drawn);
    /* The same rule from the face's corners, which a clip does not move and a view does not
     * round: a side is vertical where its ends keep x and depth, and z is up. With the camera
     * level, as the world's is, its sides are vertical on screen exactly when they are here. */
    if (f->corners == 4) {
        int64_t cx[4], cy[4], down[4];
        uint8_t mine[4];
        for (int i = 0; i < 4; i++) {
            cx[i] = f->cam[i][0];
            cy[i] = f->cam[i][1];
            down[i] = -(int64_t)f->cam[i][2];
        }
        canonical_order(cx, cy, down, mine);
        for (int i = 0; i < 4; i++)
            it->imageCorner[mine[i]] = (uint8_t)i;
        if (s_check == 1 && it->whole && memcmp(drawn, mine, sizeof mine) != 0)
            s_counts.disagree++;
    }
    if (s_check == 1) {
        uint8_t order = (uint8_t)(drawn[0] | drawn[1] << 2 | drawn[2] << 4 | drawn[3] << 6);
        s_counts.textured++;
        s_counts.vertical += it->vertical;
        s_counts.reordered += order != 0xe4;
        if (it->whole)
            note_order(it->id, order);
    }
}

void bak_scene_strip(uint16_t siBase, int16_t stride, uint16_t rowSub, uint16_t rowCount) {
    if (!bak_scene_capturing() || s_pending.item < 0 || !s_pending.atSink)
        return;
    SceneItem *it = &s_build.items[s_pending.item];
    if (it->fate != SCENE_DRAWN || it->textured)
        return;
    it->striped = 1;
    it->strip = (SceneStrip){siBase, stride, rowSub, rowCount};
    s_counts.strips++;
}

/* Where a camera-space point projects (any units: the projection keeps none). */
static void project(const int16_t p[3], double *x, double *y) {
    double focal = ldexp(1.0, s_build.screenShift);
    *x = s_build.centerX + p[0] * focal / p[1];
    *y = s_build.centerY - p[2] * focal / p[1];
}

/* A billboard's anchor or a circle's centre, checked: where it projects, and near its object. */
static void check_point(const int16_t p[3], int unitShift, int16_t x, int16_t y) {
    double scale = ldexp(1.0, unitShift), px, py;
    if (p[1] <= 0)
        return;
    project(p, &px, &py);
    double off = deviation(px, py, x, y);
    if (off > TOLERANCE) {
        if (s_counts.offTarget++ < 8)
            fprintf(stderr,
                    "scene: a point (%d, %d, %d) projects to (%.1f, %.1f), drawn at "
                    "(%d, %d)\n",
                    p[0], p[1], p[2], px, py, x, y);
    } else if (off > s_worstAnchor) {
        s_worstAnchor = off;
    }
    if (s_object.index >= 0 && !near_object(p[0] * scale, p[1] * scale, p[2] * scale, 2.0 * scale))
        s_counts.strayAnchors++;
}

void bak_scene_billboard(const struct ImageRecord *image, const int16_t anchor[3], int unitShift,
                         int16_t anchorX, int16_t anchorY, int16_t x, int16_t y, int16_t w,
                         int16_t h, uint16_t flip, int16_t remap) {
    if (!bak_scene_capturing())
        return;
    settle_pending();
    SceneItem *it = new_item(SCENE_BILLBOARD);
    if (it == NULL)
        return;
    double scale = ldexp(1.0, unitShift);
    it->fate = SCENE_DRAWN;
    it->texture =
        texture_id(image, image != NULL && (image->wUnk4 & 0x80) ? COLUMNS_UNPACKED : COLUMNS);
    it->flip = (uint8_t)(flip & 3);
    it->remap = remap_row(remap);
    it->rect[0] = x;
    it->rect[1] = y;
    it->rect[2] = w;
    it->rect[3] = h;
    add_vertex(anchor[0] * scale, anchor[1] * scale, anchor[2] * scale);
    s_counts.billboards++;
    if (s_check == 1)
        check_point(anchor, unitShift, anchorX, anchorY);
}

void bak_scene_circle(const int16_t centre[3], int unitShift, int16_t x, int16_t y, int16_t radius,
                      uint8_t colour) {
    if (!bak_scene_capturing())
        return;
    settle_pending();
    SceneItem *it = new_item(SCENE_CIRCLE);
    if (it == NULL)
        return;
    double scale = ldexp(1.0, unitShift);
    it->fate = SCENE_DRAWN;
    it->fill = colour;
    it->rect[0] = x;
    it->rect[1] = y;
    it->rect[2] = radius;
    add_vertex(centre[0] * scale, centre[1] * scale, centre[2] * scale);
    s_counts.circles++;
    if (s_check == 1)
        check_point(centre, unitShift, x, y);
}

/* ---- the sky ---------------------------------------------------------------------- */

static SceneSkyOp *sky_op(uint8_t kind) {
    if (!bak_scene_capturing())
        return NULL;
    note_page();
    if (s_build.skyCount >= SCENE_SKY_MAX) {
        s_counts.skyDropped++;
        return NULL;
    }
    s_counts.skyOps++;
    SceneSkyOp *op = &s_build.sky[s_build.skyCount++];
    memset(op, 0, sizeof *op);
    op->kind = kind;
    return op;
}

void bak_scene_horizon(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t reject,
                       uint16_t heading) {
    if (!bak_scene_capturing())
        return;
    s_build.hasHorizon = true;
    s_build.horizon[0] = x0;
    s_build.horizon[1] = y0;
    s_build.horizon[2] = x1;
    s_build.horizon[3] = y1;
    s_build.horizonReject = reject;
    s_build.heading = heading;
}

void bak_scene_sky_band(int16_t y, int16_t rows, uint8_t colour) {
    SceneSkyOp *op = sky_op(SCENE_SKY_BAND);
    if (op != NULL)
        *op = (SceneSkyOp){SCENE_SKY_BAND, colour, 13, y, 294, rows};
}

void bak_scene_sky_shaded(int16_t lastRowOffset, int16_t rows) {
    SceneSkyOp *op = sky_op(SCENE_SKY_SHADED);
    if (op != NULL)
        *op = (SceneSkyOp){
            SCENE_SKY_SHADED, 0, 13, (int16_t)(lastRowOffset / 0x50 - rows + 1), 294, rows};
}

void bak_scene_sky_sprite(const struct ImageRecord *image, int16_t x, int16_t y) {
    SceneSkyOp *op = sky_op(SCENE_SKY_SPRITE);
    if (op == NULL)
        return;
    op->x = x;
    op->y = y;
    op->texture = texture_id(image, DRIVER_ROWS);
    const SceneTexture *t = scene_texture(op->texture);
    if (t != NULL) {
        op->w = t->width;
        op->h = t->height;
    }
}

void bak_scene_sky_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t colour) {
    SceneSkyOp *op = sky_op(SCENE_SKY_RECT);
    if (op != NULL)
        *op = (SceneSkyOp){SCENE_SKY_RECT, colour, x, y, w, h};
}

void bak_scene_ground(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t siBase, int16_t stride,
                      uint16_t rowSub, uint16_t rowCount) {
    SceneSkyOp *op = sky_op(SCENE_SKY_GROUND);
    if (op != NULL)
        *op = (SceneSkyOp){SCENE_SKY_GROUND, 0, x, y, w, h, 0, {siBase, stride, rowSub, rowCount}};
}

/* ---- the report ------------------------------------------------------------------- */

static void report(void) {
    scene_finish();
    fprintf(stderr,
            "scene: %llu frames (%llu drawn into two pages); %llu faces; %llu drawn whole and "
            "%llu clipped compared with their polygons, %llu of them more than two pixels off; "
            "%llu clipped with no corner to compare; %llu with a corner far from their object; "
            "the largest deviation by LOD: %.2f %.2f %.2f %.2f\n",
            s_counts.frames, s_counts.mixedPages, s_counts.faces, s_counts.whole, s_counts.partly,
            s_counts.mismatched, s_counts.unchecked, s_counts.strayCorners, s_worst[0], s_worst[1],
            s_worst[2], s_worst[3]);
    fprintf(stderr,
            "scene: %llu textured quads drawn, %llu by the vertical-sided path, %llu with their "
            "corners reordered; %llu faces drawn whole in more than one order; %llu drawn whole "
            "in another order than their corners give; %llu faces from the ground's strip; %llu "
            "billboards and %llu circles, %llu off their place (the largest deviation %.2f), "
            "%llu far from their object; %llu sky operations (%llu past the room); %u "
            "textures\n",
            s_counts.textured, s_counts.vertical, s_counts.reordered, s_counts.unsteady,
            s_counts.disagree, s_counts.strips, s_counts.billboards, s_counts.circles,
            s_counts.offTarget, s_worstAnchor, s_counts.strayAnchors, s_counts.skyOps,
            s_counts.skyDropped, (unsigned)s_textureCount);
}
