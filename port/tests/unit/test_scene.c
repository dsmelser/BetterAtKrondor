/* The 3-D scene's frames (port/platform/scene.c): what the hooks hand over becomes items in world
 * units with what the software did with them, images become textures of their own, and frames
 * finish by pass and by page. */
#include <stdint.h>
#include <string.h>

#include "unit.h"

#include "bak_video.h"
#include "../../platform/scene.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "gfx169d.h"
#include "SRC/GEN/GFXCTX.H"
#include "bak_game_end.h"

#define PAGE_A 0xA000
#define PAGE_B 0xA400
#define PAGE_C 0xA800

static void start(uint16_t page) {
    BakSceneCamera c = {{1000, 2000, 300}, {0, 0, 0x4000}, {13, 11, 306, 111}, 160, 61, 9};
    scene_reset();
    scene_enable(true);
    g_graphics_context.wGfxBlitDstPage = page;
    bak_scene_begin(&c);
}

static void stop(void) {
    scene_reset();
    scene_enable(false);
}

/* A square across the view 1250 units ahead, its corners in three orders: from its top left
 * round to the right; from its bottom left up; from its bottom left to the right. */
static const int32_t s_square[4][3] = {
    {-50, 1250, 50}, {50, 1250, 50}, {50, 1250, -50}, {-50, 1250, -50}};
static const int32_t s_turned[4][3] = {
    {-50, 1250, -50}, {-50, 1250, 50}, {50, 1250, 50}, {50, 1250, -50}};
static const int32_t s_upsideDown[4][3] = {
    {-50, 1250, -50}, {50, 1250, -50}, {50, 1250, 50}, {-50, 1250, 50}};

static BakSceneFace face(const int32_t corners[4][3], uint8_t flags, uint32_t frontMask,
                         uint8_t nearClipped) {
    BakSceneFace f;
    memset(&f, 0, sizeof f);
    memcpy(f.cam, corners, 4 * sizeof corners[0]);
    f.corners = 4;
    f.textured = (flags & 0x10) != 0;
    f.nearClipped = nearClipped;
    f.frontMask = frontMask;
    f.clip[0] = 13;
    f.clip[1] = 11;
    f.clip[2] = 306;
    f.clip[3] = 111;
    f.centerX = 160;
    f.centerY = 61;
    f.screenShift = 9;
    f.flags = flags;
    return f;
}

static bool vertex_is(const SceneVertex *v, float x, float y, float z) {
    return v->x == x && v->y == y && v->z == z;
}

/* A face's corners come in its level of detail's units and go on in world units; a face that
 * reaches the flat sink is drawn with the sink's colours, a textured one the near clip left
 * without four corners is dropped, and one with no corner in front is clipped. */
void unit_scene_faces_come_in_world_units(void) {
    static const char shape;
    static const int16_t xs[4] = {140, 180, 180, 140}, ys[4] = {41, 41, 81, 81};
    start(PAGE_A);
    bak_scene_object(0, 5000, 0, 400, 2, 7, &shape);
    BakSceneFace a = face(s_square, 0x80, 0xf, 0);
    bak_scene_face(&a);
    bak_scene_polygon(4, xs, ys, 0, 1, 7, 8);
    BakSceneFace b = face(s_square, 0x10, 0x3, 1);
    bak_scene_face(&b);
    BakSceneFace c = face(s_square, 0x80, 0x0, 1);
    bak_scene_face(&c);
    scene_finish();

    const SceneFrame *f = scene_latest(PAGE_A);
    UNIT_CHECK(f != NULL);
    if (f == NULL)
        return;
    UNIT_CHECK(f->page == PAGE_A && f->centerX == 160 && f->screenShift == 9);
    UNIT_CHECK(f->camPos[0] == 1000 && f->camAngles[2] == 0x4000);
    UNIT_CHECK(f->itemCount == 3 && f->vertexCount == 12 && f->objectCount == 1);
    UNIT_CHECK(f->objects[0].origin.y == 5000 && f->objects[0].extent == 400);
    UNIT_CHECK(f->objects[0].priority == 7 && f->objects[0].shape == &shape);
    const SceneItem *it = f->items;
    UNIT_CHECK(it[0].type == SCENE_FACE && it[0].fate == SCENE_DRAWN && it[0].whole);
    UNIT_CHECK(it[0].filled == 1 && it[0].fill == 7 && it[0].outline == 8);
    UNIT_CHECK(it[0].corners == 4 && it[0].firstVertex == 0 && it[0].object == 0);
    UNIT_CHECK(vertex_is(&f->vertices[0], -200, 5000, 200));
    UNIT_CHECK(vertex_is(&f->vertices[2], 200, 5000, -200));
    UNIT_CHECK(it[1].fate == SCENE_DROPPED && it[1].firstVertex == 4);
    UNIT_CHECK(it[2].fate == SCENE_CLIPPED);
    stop();
}

/* A textured quad takes its image as the textured blit reads it (columns), its remap table's
 * row, and the image corner each of its corners takes. The blit turns a quad whose sides 0-1
 * and 2-3 are vertical on screen a corner on, and takes each vertical side's upper end first;
 * the capture applies the same rule to the face's own corners (vertical: the same x and depth),
 * so a clipped quad keeps its corners' images. Equal images are one texture; a record freed and
 * filled again is another. */
void unit_scene_textured_quads_keep_their_images_corners(void) {
    static unsigned char pixels[8] = {1, 5, 2, 6, 3, 7, 4, 8}, again[8] = {1, 5, 2, 6, 3, 7, 4, 8};
    /* the turned and the upside-down squares as drawn, and the latter clipped */
    static const int16_t xs[4] = {140, 140, 180, 180}, ys[4] = {81, 41, 41, 81};
    static const int16_t xs2[4] = {140, 180, 180, 140}, ys2[4] = {81, 81, 41, 41};
    static const int16_t xs3[4] = {150, 180, 180, 150};
    ImageRecord rec = {pixels, sizeof pixels, 0, 0, 4, 2};
    ImageRecord copy = {again, sizeof again, 0, 0, 4, 2};
    start(PAGE_A);
    bak_scene_object(0, 5000, 0, 400, 0, 0, NULL);
    BakSceneFace q = face(s_turned, 0x10, 0xf, 0);
    bak_scene_face(&q);
    bak_scene_polygon(4, xs, ys, 1, 0, 0, 0);
    bak_scene_texture(&rec, FOG_REMAP_TAB_OFF + 0x200, xs, ys);
    q = face(s_upsideDown, 0x10, 0xf, 0);
    bak_scene_face(&q);
    bak_scene_polygon(4, xs2, ys2, 1, 0, 0, 0);
    bak_scene_texture(&copy, 0, xs2, ys2);
    q = face(s_upsideDown, 0x10, 0x6, 1);
    bak_scene_face(&q);
    bak_scene_polygon(4, xs3, ys2, 1, 0, 0, 0);
    bak_scene_texture(&copy, 0, xs3, ys2);
    bak_scene_face(&q);
    bak_scene_polygon(4, xs2, ys2, 1, 0, 0, 0); /* past the fog: no image */
    ImageRecord *table[2] = {&rec, NULL};
    bak_scene_images_freed(table);
    pixels[0] = 9;
    q = face(s_turned, 0x10, 0xf, 0);
    bak_scene_face(&q);
    bak_scene_polygon(4, xs, ys, 1, 0, 0, 0);
    bak_scene_texture(&rec, 0, xs, ys);
    scene_finish();

    const SceneFrame *f = scene_latest(PAGE_A);
    UNIT_CHECK(f != NULL && f->itemCount == 5);
    if (f == NULL || f->itemCount != 5)
        return;
    const SceneItem *it = f->items;
    UNIT_CHECK(it[0].fate == SCENE_DRAWN && it[0].textured && it[0].vertical && it[0].whole);
    UNIT_CHECK(it[0].remap == 2);
    UNIT_CHECK(it[0].imageCorner[0] == 3 && it[0].imageCorner[1] == 0);
    UNIT_CHECK(it[0].imageCorner[2] == 1 && it[0].imageCorner[3] == 2);
    UNIT_CHECK(it[1].remap == -1 && it[1].vertical);
    UNIT_CHECK(it[1].imageCorner[0] == 3 && it[1].imageCorner[1] == 2);
    UNIT_CHECK(it[1].imageCorner[2] == 1 && it[1].imageCorner[3] == 0);
    UNIT_CHECK(it[1].texture == it[0].texture);
    UNIT_CHECK(!it[2].whole && it[2].fate == SCENE_DRAWN);
    UNIT_CHECK(it[2].imageCorner[0] == 3 && it[2].imageCorner[1] == 2);
    UNIT_CHECK(it[2].imageCorner[2] == 1 && it[2].imageCorner[3] == 0);
    UNIT_CHECK(it[3].fate == SCENE_FOGGED && it[3].texture == 0);
    UNIT_CHECK(it[4].texture != 0 && it[4].texture != it[0].texture);
    const SceneTexture *t = scene_texture(it[0].texture);
    static const uint8_t rows[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    UNIT_CHECK(t != NULL && t->width == 4 && t->height == 2);
    UNIT_CHECK(t != NULL && memcmp(t->indices, rows, sizeof rows) == 0);
    t = scene_texture(it[4].texture);
    UNIT_CHECK(t != NULL && t->indices[0] == 9);
    stop();
}

/* A frame ends at the next pass or when its page is flipped to the front, and is its page's
 * last; what comes after a flip keeps the camera. A pass that draws nothing leaves none. */
void unit_scene_frames_finish_by_pass_and_page(void) {
    start(PAGE_A);
    bak_scene_sky_band(11, 20, 0xd7);
    scene_page_flipped(PAGE_B);
    UNIT_CHECK(scene_latest(PAGE_A) == NULL);
    scene_page_flipped(PAGE_A);
    const SceneFrame *a = scene_latest(PAGE_A);
    UNIT_CHECK(a != NULL && a->skyCount == 1 && a->sky[0].kind == SCENE_SKY_BAND);
    uint32_t first = a != NULL ? a->serial : 0;
    bak_scene_sky_band(31, 20, 0xd8);
    BakSceneCamera c = {{0}, {0}, {13, 11, 306, 111}, 160, 61, 8};
    bak_scene_begin(&c);
    a = scene_latest(PAGE_A);
    UNIT_CHECK(a != NULL && a->serial > first && a->sky[0].colour == 0xd8);
    UNIT_CHECK(a != NULL && a->centerX == 160 && a->screenShift == 9);
    bak_scene_begin(&c); /* drew nothing */
    UNIT_CHECK(scene_latest(PAGE_B) == NULL);
    g_graphics_context.wGfxBlitDstPage = PAGE_B;
    bak_scene_sky_rect(13, 11, 294, 101, 0x40, 0);
    bak_scene_begin(&c);
    const SceneFrame *b = scene_latest(PAGE_B);
    UNIT_CHECK(b != NULL && b->screenShift == 8 && b->sky[0].kind == SCENE_SKY_RECT);
    UNIT_CHECK(scene_latest(PAGE_A) == a && a->sky[0].colour == 0xd8);
    stop();
}

/* Billboards and circles come in world units with their places on screen; the sky's panorama
 * is read as the VGA driver reads it (rows a multiple of four long), a billboard's packed
 * image as the sprite blit unpacks it; the ground's band keeps its strip. */
void unit_scene_billboards_circles_and_the_sky(void) {
    static unsigned char packed[] = {0x82, 5, 0x02, 6, 7, 0};
    static unsigned char panorama[12] = {11, 12, 13, 14, 15, 16, 17, 18, 0, 0, 0, 0};
    ImageRecord sprite = {packed, sizeof packed, 0, 0x80, 2, 2};
    ImageRecord sky = {panorama, sizeof panorama, 0, 0, 6, 2};
    static const int16_t anchor[3] = {10, 300, -20}, centre[3] = {0, 100, 0};
    start(PAGE_A);
    bak_scene_horizon(13, 60, 306, 62, 0, 0x4000);
    bak_scene_sky_sprite(&sky, 64, 31);
    bak_scene_ground(13, 62, 294, 12, 0x1234, 0x4f, 62, 0);
    bak_scene_billboard(&sprite, anchor, 3, 177, 27, 150, 30, 20, 24, 2, 0);
    bak_scene_circle(centre, 1, 160, 61, 5, 0x6f);
    scene_finish();

    const SceneFrame *f = scene_latest(PAGE_A);
    UNIT_CHECK(f != NULL && f->itemCount == 2 && f->skyCount == 2);
    if (f == NULL || f->itemCount != 2 || f->skyCount != 2)
        return;
    UNIT_CHECK(f->hasHorizon && f->horizon[1] == 60 && f->heading == 0x4000);
    const SceneSkyOp *op = f->sky;
    UNIT_CHECK(op[0].kind == SCENE_SKY_SPRITE && op[0].x == 64 && op[0].y == 31);
    UNIT_CHECK(op[0].w == 4 && op[0].h == 2);
    const SceneTexture *t = scene_texture(op[0].texture);
    static const uint8_t skyRows[8] = {11, 12, 13, 14, 15, 16, 17, 18};
    UNIT_CHECK(t != NULL && t->width == 4 && memcmp(t->indices, skyRows, 8) == 0);
    UNIT_CHECK(op[1].kind == SCENE_SKY_GROUND && op[1].h == 12 && op[1].strip.siBase == 0x1234);
    UNIT_CHECK(op[1].strip.stride == 0x4f && op[1].strip.rowSub == 62);
    const SceneItem *it = f->items;
    UNIT_CHECK(it[0].type == SCENE_BILLBOARD && it[0].flip == 2 && it[0].remap == -1);
    UNIT_CHECK(it[0].rect[0] == 150 && it[0].rect[3] == 24);
    UNIT_CHECK(vertex_is(&f->vertices[it[0].firstVertex], 80, 2400, -160));
    t = scene_texture(it[0].texture);
    static const uint8_t spriteRows[4] = {5, 6, 5, 7};
    UNIT_CHECK(t != NULL && t->width == 2 && memcmp(t->indices, spriteRows, 4) == 0);
    UNIT_CHECK(it[1].type == SCENE_CIRCLE && it[1].fill == 0x6f && it[1].rect[2] == 5);
    UNIT_CHECK(vertex_is(&f->vertices[it[1].firstVertex], 0, 200, 0));
    stop();
}

/* A view's frame has a tag, which the draws its hooks took in carry, each from its hook to
 * bak_scene_drawn, and nothing else: not a polygon no face waits for, nor anything a pass that
 * is no view draws. However many views come after it, none takes the tag of a frame kept. */
void unit_scene_views_tag_only_their_draws(void) {
    static unsigned char packed[] = {0x82, 5, 0};
    ImageRecord sprite = {packed, sizeof packed, 0, 0x80, 2, 1};
    static const int16_t xs[4] = {140, 180, 180, 140}, ys[4] = {41, 41, 81, 81};
    static const int16_t anchor[3] = {10, 300, -20};
    start(PAGE_A);
    bak_scene_view();
    UNIT_CHECK(g_bakVramTagNow == 0);
    bak_scene_sky_band(11, 20, 0xd7);
    uint8_t tag = g_bakVramTagNow;
    UNIT_CHECK(tag != 0);
    bak_scene_drawn();
    UNIT_CHECK(g_bakVramTagNow == 0);
    bak_scene_object(0, 5000, 0, 400, 2, 0, NULL);
    BakSceneFace a = face(s_square, 0x80, 0xf, 0);
    bak_scene_face(&a);
    bak_scene_polygon(4, xs, ys, 0, 1, 7, 8);
    UNIT_CHECK(g_bakVramTagNow == tag);
    bak_scene_drawn();
    bak_scene_polygon(4, xs, ys, 0, 1, 7, 8); /* the face's sink came already */
    UNIT_CHECK(g_bakVramTagNow == 0);
    bak_scene_billboard(&sprite, anchor, 3, 177, 27, 150, 30, 20, 24, 0, 0);
    UNIT_CHECK(g_bakVramTagNow == tag);
    bak_scene_drawn();
    scene_finish();
    const SceneFrame *f = scene_latest(PAGE_A);
    UNIT_CHECK(f != NULL && f->tag == tag && scene_frame_tagged(tag) == f);

    BakSceneCamera c = {{0}, {0}, {13, 11, 306, 111}, 160, 61, 9};
    g_graphics_context.wGfxBlitDstPage = PAGE_C;
    bak_scene_begin(&c); /* no view */
    bak_scene_sky_band(11, 20, 0xd7);
    UNIT_CHECK(g_bakVramTagNow == 0);
    bak_scene_drawn();
    g_graphics_context.wGfxBlitDstPage = PAGE_B;
    bool reused = false;
    for (int i = 0; i < 600; i++) {
        bak_scene_begin(&c);
        bak_scene_view();
        bak_scene_sky_band(11, 20, 0xd7);
        reused |= g_bakVramTagNow == tag || g_bakVramTagNow == 0;
        bak_scene_drawn();
    }
    UNIT_CHECK(!reused);
    UNIT_CHECK(scene_latest(PAGE_A) == f && f->tag == tag);
    stop();
}

/* Combat's effects: a line on screen in its colour, and the last billboard's image drawn again
 * elsewhere at its anchor (a shadow), both draws of the view; without a billboard in the frame a
 * sprite is none. Each item keeps the clip rectangle it was drawn within, a face its own. */
void unit_scene_lines_and_sprites_drawn_again(void) {
    static unsigned char packed[] = {0x82, 5, 0};
    ImageRecord sprite = {packed, sizeof packed, 0, 0x80, 2, 1};
    static const int16_t anchor[3] = {10, 300, -20};
    static const int16_t xs[4] = {140, 180, 180, 140}, ys[4] = {41, 41, 81, 81};
    start(PAGE_A);
    bak_scene_view();
    g_graphics_context.bClip_enabled = 0;
    bak_scene_sprite(&sprite, 148, 29, 23, 26, 0, CURSOR_REMAP_TAB_OFF + 0x100);
    UNIT_CHECK(g_bakVramTagNow == 0);
    g_graphics_context.bClip_enabled = 1;
    g_graphics_context.clip = (ClipRect){13, 306, 11, 70};
    bak_scene_billboard(&sprite, anchor, 3, 177, 27, 150, 30, 20, 24, 2, 0);
    bak_scene_drawn();
    bak_scene_sprite(&sprite, 148, 29, 23, 26, 2, CURSOR_REMAP_TAB_OFF + 0x100);
    uint8_t tag = g_bakVramTagNow;
    UNIT_CHECK(tag != 0);
    bak_scene_drawn();
    g_graphics_context.bClip_enabled = 0;
    bak_scene_line(20, 30, 60, 35, 0xaf);
    UNIT_CHECK(g_bakVramTagNow == tag);
    bak_scene_drawn();
    BakSceneFace a = face(s_square, 0x80, 0xf, 0);
    bak_scene_face(&a);
    bak_scene_polygon(4, xs, ys, 0, 1, 7, 8);
    bak_scene_drawn();
    scene_finish();

    const SceneFrame *f = scene_latest(PAGE_A);
    UNIT_CHECK(f != NULL && f->itemCount == 4 && f->vertexCount == 5);
    if (f == NULL || f->itemCount != 4)
        return;
    const SceneItem *it = f->items;
    UNIT_CHECK(it[0].clip[0] == 13 && it[0].clip[1] == 11 && it[0].clip[3] == 70);
    UNIT_CHECK(it[1].type == SCENE_BILLBOARD && it[1].fate == SCENE_DRAWN);
    UNIT_CHECK(it[1].firstVertex == it[0].firstVertex && it[1].texture == it[0].texture);
    UNIT_CHECK(it[1].remap == 11 && it[1].flip == 2);
    UNIT_CHECK(it[1].rect[0] == 148 && it[1].rect[1] == 29 && it[1].rect[2] == 23);
    UNIT_CHECK(it[1].rect[3] == 26 && it[1].clip[2] == 306);
    UNIT_CHECK(it[2].type == SCENE_LINE && it[2].fate == SCENE_DRAWN && it[2].fill == 0xaf);
    UNIT_CHECK(it[2].rect[0] == 20 && it[2].rect[1] == 30 && it[2].rect[2] == 60);
    UNIT_CHECK(it[2].rect[3] == 35);
    UNIT_CHECK(it[2].clip[0] == INT16_MIN && it[2].clip[3] == INT16_MAX);
    UNIT_CHECK(it[3].type == SCENE_FACE && it[3].clip[0] == 13 && it[3].clip[3] == 111);

    bak_scene_sprite(&sprite, 148, 29, 23, 26, 0, 0); /* a new frame: no billboard in it */
    UNIT_CHECK(g_bakVramTagNow == 0);
    stop();
}

/* A view and what it draws over, as its page is drawn into. */
static const SceneFrame *view(uint16_t page, const int16_t clip[4], bool background) {
    BakSceneCamera c = {{0}, {0}, {clip[0], clip[1], clip[2], clip[3]}, 160, 61, 9};
    static const int16_t anchor[3] = {10, 300, -20};
    static unsigned char packed[] = {0x82, 5, 0};
    static ImageRecord sprite = {packed, sizeof packed, 0, 0x80, 2, 1};
    g_graphics_context.wGfxBlitDstPage = page;
    bak_scene_begin(&c);
    bak_scene_view();
    if (background)
        bak_scene_sky_rect(clip[0], clip[1], clip[2] - clip[0] + 1, clip[3] - clip[1] + 1, 0, 0);
    else
        bak_scene_billboard(&sprite, anchor, 3, 177, 27, 150, 30, 20, 24, 0, 0);
    bak_scene_drawn();
    scene_finish();
    return scene_latest(page);
}

/* A picture shows the newest view its tags name, and under one that covers none of its view
 * itself (combat's actors) the one it was drawn over (the background copied under them), if it
 * has the same view; never what lies under a view with a background of its own. */
void unit_scene_shows_the_frames_under_a_view(void) {
    static const int16_t clip[4] = {13, 11, 306, 111}, inset[4] = {134, 16, 300, 104};
    start(PAGE_C);
    const SceneFrame *back = view(PAGE_C, clip, true);
    const SceneFrame *actors = view(PAGE_A, clip, false);
    UNIT_CHECK(back != NULL && actors != NULL);
    if (back == NULL || actors == NULL)
        return;
    unsigned counts[256] = {0};
    const SceneFrame *shown[SCENE_SHOWN_MAX + 1];
    UNIT_CHECK(scene_frames_shown(counts, shown) == 0 && shown[0] == NULL);
    counts[back->tag] = 20000;
    counts[actors->tag] = 900;
    UNIT_CHECK(scene_frames_shown(counts, shown) == 2);
    UNIT_CHECK(shown[0] == back && shown[1] == actors && shown[2] == NULL);
    counts[back->tag] = 0;
    UNIT_CHECK(scene_frames_shown(counts, shown) == 1 && shown[0] == actors);
    counts[back->tag] = 20000;
    counts[actors->tag] = 0;
    UNIT_CHECK(scene_frames_shown(counts, shown) == 1 && shown[0] == back);

    /* a newer view with a background of its own: nothing under it */
    const SceneFrame *world = view(PAGE_B, clip, true);
    counts[world->tag] = 50;
    UNIT_CHECK(scene_frames_shown(counts, shown) == 1 && shown[0] == world);
    /* over another view's place: nothing under it either */
    counts[world->tag] = 0;
    const SceneFrame *marks = view(PAGE_A, inset, false);
    counts[marks->tag] = 50;
    UNIT_CHECK(scene_frames_shown(counts, shown) == 1 && shown[0] == marks);
    stop();
}
