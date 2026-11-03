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

/* A flat square of the given colour: x0..x1 across, z0..z1 up, at a depth. */
static void square(int32_t x0, int32_t x1, int32_t z0, int32_t z1, int32_t depth, uint8_t colour) {
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
    bak_scene_polygon(4, xs, ys, colour == 0, 1, colour, colour);
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
    bak_scene_sky_rect(0, 0, VIEW_W, 24, 1);
    bak_scene_sky_rect(0, 24, VIEW_W, 24, 2);
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

void unit_glscene_draws_a_frame(void) {
#ifdef BAK_ASAN
    printf("glscene: ASan: not drawn (see glpresent)\n");
    return;
#endif
    const SceneFrame *frame = make_frame();
    UNIT_CHECK(frame != NULL && frame->itemCount == 6);
    if (frame == NULL)
        return;
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        printf("glscene: no video (%s): not drawn\n", SDL_GetError());
        scene_reset();
        scene_enable(false);
        return;
    }
    const char *driver = SDL_GetCurrentVideoDriver();
    SDL_Window *window = NULL;
    bool open = false;
    if (driver != NULL && strcmp(driver, "dummy") != 0) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        window = SDL_CreateWindow("glscene", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        open = window != NULL && glpresent_open(window) && glscene_open();
    }
    if (!open) {
        printf("glscene: no OpenGL 3.3 context: not drawn\n");
    } else {
        static uint8_t dac[256][3], remap[15 * 256];
        static uint8_t rgba[VIEW_W * 3 * VIEW_H * 3 * 4];
        for (int i = 0; i < 256; i++) {
            dac[i][0] = (uint8_t)(i & 63);
            dac[i][1] = (uint8_t)((i * 2) & 63);
            dac[i][2] = (uint8_t)((i * 3) & 63);
        }
        GlSceneInputs in = {(const uint8_t(*)[3])dac, 0xff, remap, NULL};
        UNIT_CHECK(glscene_render_offscreen(frame, &in, VIEW_W, VIEW_H, true, rgba));
        UNIT_CHECK(index_at(rgba, VIEW_W, 60, 5) == 1);  /* the sky */
        UNIT_CHECK(index_at(rgba, VIEW_W, 5, 28) == 2);  /* the sky's lower part */
        UNIT_CHECK(index_at(rgba, VIEW_W, 5, 40) == 3);  /* the ground's shape */
        UNIT_CHECK(index_at(rgba, VIEW_W, 32, 40) == 4); /* a later face over it */
        UNIT_CHECK(index_at(rgba, VIEW_W, 20, 10) == 5); /* the nearer face */
        UNIT_CHECK(index_at(rgba, VIEW_W, 5, 2) == 6);   /* the farther one beside it */
        UNIT_CHECK(index_at(rgba, VIEW_W, 44, 12) == 7); /* the image's texels */
        UNIT_CHECK(index_at(rgba, VIEW_W, 52, 12) == 8);
        UNIT_CHECK(index_at(rgba, VIEW_W, 44, 20) == 9);
        UNIT_CHECK(index_at(rgba, VIEW_W, 52, 20) == 1); /* its transparent one */
        UNIT_CHECK(index_at(rgba, VIEW_W, 58, 32) == 10); /* the billboard */
        /* At 3x in colour: the DAC's 6-bit colours made 8-bit. */
        UNIT_CHECK(glscene_render_offscreen(frame, &in, VIEW_W * 3, VIEW_H * 3, false, rgba));
        const uint8_t *p = rgba + ((size_t)(12 * 3 + 1) * VIEW_W * 3 + (size_t)(44 * 3 + 1)) * 4;
        UNIT_CHECK(p[0] == 28 && p[1] == 56 && p[2] == 85);
        glscene_close();
        glpresent_close();
    }
    if (window != NULL)
        SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    scene_reset();
    scene_enable(false);
}
