/* The 3-D view on the GPU (glscene.c): a frame made through the capture's hooks, drawn off-screen
 * and read back. It needs an OpenGL 3.3 context; without one (CI's machines) the test says so
 * and passes. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/glscene.h"
#include "../../platform/platform.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "bak_game_end.h"

#define VIEW_W 64
#define VIEW_H 48

/* A flat square of the given colour, or its outline alone (draw_polygon with fill off): x0..x1
 * across, z0..z1 up, at a depth. */
static void flat(int32_t x0, int32_t x1, int32_t z0, int32_t z1, int32_t depth, uint8_t colour,
                 bool filled) {
    BakSceneFace f;
    memset(&f, 0, sizeof f);
    int32_t corners[4][3] = {{x0, depth, z1}, {x1, depth, z1}, {x1, depth, z0}, {x0, depth, z0}};
    memcpy(f.cam, corners, sizeof corners);
    f.corners = 4;
    f.frontMask = 0xf;
    f.clip[2] = VIEW_W - 1;
    f.clip[3] = VIEW_H - 1;
    f.centerX = VIEW_W / 2;
    f.centerY = VIEW_H / 2;
    f.screenShift = 6;
    f.flags = colour == 0 ? 0x10 : 0x80;
    f.filled = 1;
    f.fill = f.outline = colour;
    bak_scene_face(&f);
    int16_t xs[4], ys[4];
    for (int i = 0; i < 4; i++) {
        xs[i] = (int16_t)(VIEW_W / 2 + corners[i][0] * 64 / depth);
        ys[i] = (int16_t)(VIEW_H / 2 - corners[i][2] * 64 / depth);
    }
    bak_scene_polygon(4, xs, ys, colour == 0, filled, filled ? colour : 0, colour);
}

static void square(int32_t x0, int32_t x1, int32_t z0, int32_t z1, int32_t depth, uint8_t colour) {
    flat(x0, x1, z0, z1, depth, colour, true);
}

/*
 * The frame: the sky's colours 1 above row 24 and 2 below; the ground's shape (priority) in 3
 * near the bottom, drawn first without depth, so that a far face of 4 drawn after it covers it;
 * a near face of 5, then a farther one of 6 behind it, which the depth test keeps behind; a
 * textured face of 2x2 texels, 7 8 / 9 and a transparent one; a billboard of 10.
 */
static const SceneFrame *make_frame(void) {
    static unsigned char texels[4] = {7, 9, 8, 0}; /* columns */
    static unsigned char sprite[1] = {10};
    static ImageRecord image = {texels, sizeof texels, 0, 0, 2, 2};
    static ImageRecord bill = {sprite, sizeof sprite, 0, 0, 1, 1};
    static const char shapes[3] = {0};
    static const int16_t anchor[3] = {28, 300, -8};
    BakSceneCamera c = {{0}, {0}, {0, 0, VIEW_W - 1, VIEW_H - 1}, VIEW_W / 2, VIEW_H / 2, 6};
    scene_reset();
    scene_enable(true);
    g_graphics_context.wGfxBlitDstPage = 0xA800;
    bak_scene_begin(&c);
    bak_scene_sky_rect(0, 0, VIEW_W, 24, 1, 0);
    bak_scene_sky_rect(0, 24, VIEW_W, 24, 2, 0);
    bak_scene_object(0, 200, -60, 300, 0, 7, &shapes[0]);
    square(-200, 200, -100, -30, 200, 3);
    bak_scene_object(0, 1000, -250, 600, 0, 0, &shapes[1]);
    square(-200, 200, -500, 0, 1000, 4);
    bak_scene_object(0, 320, 50, 100, 0, 0, &shapes[2]);
    square(-100, -20, 0, 100, 320, 5);
    bak_scene_object(0, 640, 160, 400, 0, 0, &shapes[1]);
    square(-320, 0, 0, 320, 640, 6);
    bak_scene_object(80, 320, 40, 100, 0, 0, &shapes[2]);
    square(40, 120, 0, 80, 320, 0);
    static const int16_t xs[4] = {40, 56, 56, 40}, ys[4] = {8, 8, 24, 24};
    bak_scene_texture(&image, 0, xs, ys);
    bak_scene_billboard(&bill, anchor, 0, 38, 25, 56, 30, 6, 6, 0, 0);
    scene_finish();
    return scene_latest(0xA800);
}

static int index_at(const uint8_t *rgba, int w, int x, int y) {
    return rgba[((size_t)y * (size_t)w + (size_t)x) * 4];
}

static uint8_t s_dac[256][3], s_remap[15 * 256];
static uint8_t s_rgba[VIEW_W * 3 * VIEW_H * 3 * 4];

/* The checks run in an OpenGL 3.3 context, with a DAC whose colour i is (i, 2i, 3i) & 63; where
 * there is none (or under ASan), the test says so and passes. */
static void in_a_context(const char *what, void (*checks)(const GlSceneInputs *in)) {
#ifdef BAK_ASAN
    printf("%s: ASan: not drawn (see glpresent)\n", what);
    return;
#endif
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        printf("%s: no video (%s): not drawn\n", what, SDL_GetError());
        return;
    }
    const char *driver = SDL_GetCurrentVideoDriver();
    SDL_Window *window = NULL;
    bool open = false;
    if (driver != NULL && strcmp(driver, "dummy") != 0) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        window = SDL_CreateWindow(what, 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        open = window != NULL && glpresent_open(window) && glscene_open();
    }
    if (!open) {
        printf("%s: no OpenGL 3.3 context: not drawn\n", what);
    } else {
        for (int i = 0; i < 256; i++) {
            s_dac[i][0] = (uint8_t)(i & 63);
            s_dac[i][1] = (uint8_t)((i * 2) & 63);
            s_dac[i][2] = (uint8_t)((i * 3) & 63);
        }
        GlSceneInputs in = {(const uint8_t (*)[3])s_dac, 0xff, s_remap, NULL};
        checks(&in);
        glscene_close();
        glpresent_close();
    }
    if (window != NULL)
        SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static const SceneFrame *s_frame;

static void draws_a_frame(const GlSceneInputs *in) {
    uint8_t *rgba = s_rgba;
    UNIT_CHECK(glscene_render_offscreen(s_frame, in, VIEW_W, VIEW_H, true, rgba));
    UNIT_CHECK(index_at(rgba, VIEW_W, 60, 5) == 1);  /* the sky */
    UNIT_CHECK(index_at(rgba, VIEW_W, 5, 28) == 2);  /* the sky's lower part */
    UNIT_CHECK(index_at(rgba, VIEW_W, 5, 40) == 3);  /* the ground's shape */
    UNIT_CHECK(index_at(rgba, VIEW_W, 32, 40) == 4); /* a later face over it */
    UNIT_CHECK(index_at(rgba, VIEW_W, 20, 10) == 5); /* the nearer face */
    UNIT_CHECK(index_at(rgba, VIEW_W, 5, 2) == 6);   /* the farther one beside it */
    UNIT_CHECK(index_at(rgba, VIEW_W, 44, 12) == 7); /* the image's texels */
    UNIT_CHECK(index_at(rgba, VIEW_W, 52, 12) == 8);
    UNIT_CHECK(index_at(rgba, VIEW_W, 44, 20) == 9);
    UNIT_CHECK(index_at(rgba, VIEW_W, 52, 20) == 1);  /* its transparent one */
    UNIT_CHECK(index_at(rgba, VIEW_W, 58, 32) == 10); /* the billboard */
    /* At 3x in colour: the DAC's 6-bit colours made 8-bit. */
    UNIT_CHECK(glscene_render_offscreen(s_frame, in, VIEW_W * 3, VIEW_H * 3, false, rgba));
    const uint8_t *p = rgba + ((size_t)(12 * 3 + 1) * VIEW_W * 3 + (size_t)(44 * 3 + 1)) * 4;
    UNIT_CHECK(p[0] == 28 && p[1] == 56 && p[2] == 85);
}

void unit_glscene_draws_a_frame(void) {
    s_frame = make_frame();
    UNIT_CHECK(s_frame != NULL && s_frame->itemCount == 6);
    if (s_frame != NULL)
        in_a_context("glscene", draws_a_frame);
    scene_reset();
    scene_enable(false);
}

/*
 * Combat's way: a background (a map's dithered rectangle of colours 20 and 21, a face of 3 1000
 * units ahead) and a frame drawn over it (a face of 5 behind that one, 2000 units ahead, and an
 * outline of 9). The frame over it is painted over it whatever their depths, and where it draws
 * nothing the background shows; alone, it leaves those pixels transparent. The dither is the
 * checkerboard as indices, from the rectangle's left edge, and the blend of its colours in
 * colour; an outline keeps a 1x pixel's weight.
 */
static const SceneFrame *s_back, *s_over;

static void make_frames(void) {
    static const char shapes[2] = {0};
    BakSceneCamera c = {{0}, {0}, {0, 0, VIEW_W - 1, VIEW_H - 1}, VIEW_W / 2, VIEW_H / 2, 6};
    scene_reset();
    scene_enable(true);
    g_graphics_context.wGfxBlitDstPage = 0xA800;
    bak_scene_begin(&c);
    bak_scene_sky_rect(1, 0, VIEW_W - 1, VIEW_H, 20, 21);
    bak_scene_object(0, 1000, 0, 400, 0, 0, &shapes[0]);
    square(-300, 0, -200, 200, 1000, 3); /* x 12.8-32, y 11.2-36.8 */
    scene_finish();
    s_back = scene_latest(0xA800);
    g_graphics_context.wGfxBlitDstPage = 0xA000;
    bak_scene_begin(&c);
    bak_scene_object(0, 2000, 0, 400, 0, 0, &shapes[1]);
    square(-200, 200, -100, 100, 2000, 5);    /* x 25.6-38.4, y 20.8-27.2 */
    flat(400, 600, 300, 500, 2000, 9, false); /* an outline: x 44.8-51.2, y 8-14.4 */
    scene_finish();
    s_over = scene_latest(0xA000);
}

static void draws_a_frame_over_another(const GlSceneInputs *in) {
    uint8_t *rgba = s_rgba;
    const SceneFrame *both[3] = {s_back, s_over, NULL}, *over[2] = {s_over, NULL};
    UNIT_CHECK(glscene_render_frames_offscreen(both, in, VIEW_W, VIEW_H, true, rgba));
    UNIT_CHECK(index_at(rgba, VIEW_W, 3, 2) == 20); /* 3 + 2 + 1 even */
    UNIT_CHECK(index_at(rgba, VIEW_W, 4, 2) == 21);
    UNIT_CHECK(index_at(rgba, VIEW_W, 16, 14) == 3); /* the background's face */
    UNIT_CHECK(index_at(rgba, VIEW_W, 30, 24) == 5); /* the farther face over it */
    UNIT_CHECK(index_at(rgba, VIEW_W, 36, 24) == 5);
    UNIT_CHECK(glscene_render_frames_offscreen(over, in, VIEW_W, VIEW_H, false, rgba));
    UNIT_CHECK(rgba[((size_t)2 * VIEW_W + 3) * 4 + 3] == 0);     /* nothing drawn */
    UNIT_CHECK(rgba[((size_t)24 * VIEW_W + 30) * 4 + 3] == 255); /* the face */
    /* the outline's left edge across row 11 at 3x: a band a 1x pixel wide, not a hairline */
    UNIT_CHECK(glscene_render_frames_offscreen(over, in, VIEW_W * 3, VIEW_H * 3, false, rgba));
    int band = 0;
    for (int x = 125; x < 143; x++)
        band += rgba[((size_t)34 * VIEW_W * 3 + (size_t)x) * 4 + 3] == 255;
    UNIT_CHECK(band == 3);
    UNIT_CHECK(rgba[((size_t)34 * VIEW_W * 3 + 134) * 4] == 36); /* colour 9's red */
    /* in colour, the dither's two colours (81, 162, 243) and (85, 170, 255) evenly */
    UNIT_CHECK(glscene_render_offscreen(s_back, in, VIEW_W, VIEW_H, false, rgba));
    const uint8_t *p = rgba + ((size_t)2 * VIEW_W + 3) * 4;
    UNIT_CHECK(SDL_abs(p[0] - 83) <= 1 && SDL_abs(p[1] - 166) <= 1 && SDL_abs(p[2] - 249) <= 1);
    UNIT_CHECK(p[3] == 255);
    UNIT_CHECK(memcmp(p, rgba + ((size_t)2 * VIEW_W + 4) * 4, 4) == 0);
}

void unit_glscene_draws_a_frame_over_another(void) {
    make_frames();
    UNIT_CHECK(s_back != NULL && s_over != NULL && s_back != s_over);
    if (s_back != NULL && s_over != NULL)
        in_a_context("glscene over", draws_a_frame_over_another);
    scene_reset();
    scene_enable(false);
}
