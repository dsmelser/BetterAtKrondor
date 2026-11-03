/* The 3-D view in the window (glview.c): a frame drawn at 4x over a picture whose pixels all name
 * it, into a target of the test's, with and without antialiasing. It needs an OpenGL 3.3 context;
 * without one (CI's machines, ASan) the test says so and passes. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/gl.h"
#include "../../platform/glscene.h"
#include "../../platform/glview.h"
#include "../../platform/platform.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "bak_game_end.h"

#define VIEW_W 64
#define VIEW_H 48
#define SCALE 4
#define OUT_W (VIEW_W * SCALE)
#define OUT_H (VIEW_H * SCALE)

static uint8_t s_dac[256][3];
static uint8_t s_tags[VIEW_W * VIEW_H];
static uint8_t s_rgba[OUT_W * OUT_H * 4];

/*
 * A view: with a sky, colour 1 over all of it, or none; then a face of colour 2 with a slanted
 * right edge, from (12.8, 4.8) across to (38.4, 4.8) and down to (51.2, 43.2), 1000 units ahead.
 * Every pixel of the picture names it.
 */
static const SceneFrame *make_view(bool sky) {
    static const char shape = 0;
    BakSceneCamera c = {{0}, {0}, {0, 0, VIEW_W - 1, VIEW_H - 1}, VIEW_W / 2, VIEW_H / 2, 6};
    static const int32_t corners[4][3] = {
        {-300, 1000, 300}, {100, 1000, 300}, {300, 1000, -300}, {-300, 1000, -300}};
    g_graphics_context.wGfxBlitDstPage = 0xA800;
    g_graphics_context.bClip_enabled = 0;
    bak_scene_begin(&c);
    bak_scene_view();
    if (sky)
        bak_scene_sky_rect(0, 0, VIEW_W, VIEW_H, 1, 0);
    bak_scene_object(0, 1000, 0, 600, 0, 0, &shape);
    BakSceneFace f;
    memset(&f, 0, sizeof f);
    memcpy(f.cam, corners, sizeof corners);
    f.corners = 4;
    f.frontMask = 0xf;
    f.clip[2] = VIEW_W - 1;
    f.clip[3] = VIEW_H - 1;
    f.centerX = VIEW_W / 2;
    f.centerY = VIEW_H / 2;
    f.screenShift = 6;
    f.flags = 0x80;
    f.filled = 1;
    f.fill = f.outline = 2;
    bak_scene_face(&f);
    int16_t xs[4], ys[4];
    for (int i = 0; i < 4; i++) {
        xs[i] = (int16_t)(VIEW_W / 2 + corners[i][0] * 64 / corners[i][1]);
        ys[i] = (int16_t)(VIEW_H / 2 - corners[i][2] * 64 / corners[i][1]);
    }
    bak_scene_polygon(4, xs, ys, 0, 1, 2, 2);
    bak_scene_drawn();
    scene_finish();
    const SceneFrame *frame = scene_latest(0xA800);
    memset(s_tags, frame != NULL ? frame->tag : 0, sizeof s_tags);
    return frame;
}

/* The view drawn by glview over a target cleared to green, read back (GL's rows, bottom first). */
static bool draw(void) {
    GLuint fbo = 0, target = 0;
    g_gl.GenTextures(1, &target);
    g_gl.BindTexture(GL_TEXTURE_2D, target);
    g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, OUT_W, OUT_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    g_gl.GenFramebuffers(1, &fbo);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    g_gl.ClearColor(0, 1, 0, 1);
    g_gl.Clear(GL_COLOR_BUFFER_BIT);
    SDL_FRect r = {0, 0, OUT_W, OUT_H};
    bool drawn =
        glview_draw(s_tags, VIEW_W, VIEW_H, &r, OUT_W, OUT_H, (const uint8_t (*)[3])s_dac, 0xff);
    g_gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    g_gl.ReadPixels(0, 0, OUT_W, OUT_H, GL_RGBA, GL_UNSIGNED_BYTE, s_rgba);
    g_gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    g_gl.DeleteFramebuffers(1, &fbo);
    g_gl.DeleteTextures(1, &target);
    return drawn;
}

static const uint8_t *at(int x, int y) { /* the picture's x, y at 4x */
    return s_rgba + ((size_t)(OUT_H - 1 - y) * OUT_W + (size_t)x) * 4;
}

/* Pixels that are some of the face's red and some of the sky's blue. */
static int blends(void) {
    int n = 0;
    for (int i = 0; i < OUT_W * OUT_H; i++)
        n += s_rgba[i * 4] != 0 && s_rgba[i * 4 + 2] != 0;
    return n;
}

static bool open_view(const char *antialias) {
    char why[128];
    glview_close();
    return settings_override(antialias, why, sizeof why) && glview_open();
}

static void checks(void) {
    /* without antialiasing an edge pixel is the face's or the sky's */
    UNIT_CHECK(open_view("video.antialias=0"));
    make_view(true);
    UNIT_CHECK(draw());
    const uint8_t *face = at(25 * SCALE, 24 * SCALE), *sky = at(60 * SCALE, 5 * SCALE);
    UNIT_CHECK(face[0] == 255 && face[1] == 0 && face[2] == 0);
    UNIT_CHECK(sky[0] == 0 && sky[1] == 0 && sky[2] == 255);
    UNIT_CHECK(blends() == 0);
    /* with it, the slanted edge's pixels blend the two; inside, each is its own */
    UNIT_CHECK(open_view("video.antialias=4"));
    make_view(true);
    UNIT_CHECK(draw());
    int n = blends();
    UNIT_CHECK(n > 40 && n < 600);
    UNIT_CHECK(face[0] == 255 && face[1] == 0 && face[2] == 0);
    UNIT_CHECK(sky[0] == 0 && sky[1] == 0 && sky[2] == 255);
    /* over nothing, an edge pixel's drawn samples keep their colour: red, or the target's
       green where fewer than half were drawn */
    make_view(false);
    UNIT_CHECK(draw());
    int other = 0, red = 0;
    for (int i = 0; i < OUT_W * OUT_H; i++) {
        const uint8_t *p = s_rgba + (size_t)i * 4;
        red += p[0] == 255 && p[1] == 0 && p[2] == 0;
        other += !(p[0] == 255 && p[1] == 0 && p[2] == 0) && !(p[0] == 0 && p[1] == 255);
    }
    UNIT_CHECK(red > 1000 && other == 0);
}

void unit_glview_antialiases_the_view(void) {
#ifdef BAK_ASAN
    printf("glview: ASan: not drawn (see glpresent)\n");
    return;
#endif
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        printf("glview: no video (%s): not drawn\n", SDL_GetError());
        return;
    }
    const char *driver = SDL_GetCurrentVideoDriver();
    SDL_Window *window = NULL;
    bool open = false;
    if (driver != NULL && strcmp(driver, "dummy") != 0) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        window = SDL_CreateWindow("glview", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        open = window != NULL && glpresent_open(window) && glscene_open();
    }
    if (!open) {
        printf("glview: no OpenGL 3.3 context: not drawn\n");
    } else {
        s_dac[1][2] = 63; /* the sky blue, the face red */
        s_dac[2][0] = 63;
        scene_reset();
        scene_enable(true);
        checks();
        glview_close();
        scene_reset();
        scene_enable(false);
        char why[128];
        settings_override("video.antialias=4", why, sizeof why);
        glscene_close();
        glpresent_close();
    }
    if (window != NULL)
        SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
