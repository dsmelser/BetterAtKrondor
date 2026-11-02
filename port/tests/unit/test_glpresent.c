/* The window's picture through OpenGL (glpresent.c): where it goes in the window, and what
 * each scaling draws, read back from an off-screen target. The drawing needs an OpenGL 3.3
 * context; without one (CI's machines) that test says so and passes. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/platform.h"

static bool rect_is(const SDL_FRect *r, float x, float y, float w, float h) {
    return r->x == x && r->y == y && r->w == w && r->h == h;
}

/* The picture fills the output as far as its shape allows, centred; whole multiples only when
 * asked, and never below 1 by that rule. */
void unit_glpresent_letterboxes_the_picture(void) {
    SDL_FRect r;
    glpresent_layout(960, 720, 320, 240, false, &r);
    UNIT_CHECK(rect_is(&r, 0, 0, 960, 720));
    glpresent_layout(1000, 720, 320, 240, false, &r);
    UNIT_CHECK(rect_is(&r, 20, 0, 960, 720));
    glpresent_layout(1000, 800, 320, 240, false, &r);
    UNIT_CHECK(rect_is(&r, 0, 25, 1000, 750));
    glpresent_layout(1000, 800, 320, 240, true, &r);
    UNIT_CHECK(rect_is(&r, 20, 40, 960, 720));
    glpresent_layout(160, 120, 320, 240, true, &r);
    UNIT_CHECK(rect_is(&r, 0, 0, 160, 120));
    glpresent_layout(640, 400, 320, 200, false, &r); /* square pixels */
    UNIT_CHECK(rect_is(&r, 0, 0, 640, 400));
}

/* A 4x2 picture of distinct colours, as XRGB words. */
static const uint32_t s_picture[8] = {0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF,
                                      0x00102030, 0x00405060, 0x00708090, 0x00000000};

/* Whether an RGBA read-back of outW x outH holds the picture scaled by a whole k at (x0, y0),
 * black around it. */
static bool scaled_by(const uint8_t *rgba, int outW, int outH, int k, int x0, int y0) {
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            const uint8_t *p = rgba + ((size_t)y * (size_t)outW + (size_t)x) * 4;
            uint32_t want = 0;
            int px = (x - x0) / k, py = (y - y0) / k;
            if (x >= x0 && y >= y0 && px < 4 && py < 2)
                want = s_picture[py * 4 + px];
            uint32_t got = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
            if (got != want || p[3] != 0xFF) {
                fprintf(stderr, "glpresent: (%d,%d) is %06X, not %06X\n", x, y, (unsigned)got,
                        (unsigned)want);
                return false;
            }
        }
    }
    return true;
}

void unit_glpresent_draws_the_picture(void) {
#ifdef BAK_ASAN
    /* SDL's video on Windows names the input devices on a thread of its own, shifting a string
     * within its buffer (SDL_CreateDeviceName): ASan takes the C runtime's memmove for memcpy
     * there and stops, as it does in the gamepad mappings (platform.c). */
    printf("glpresent: ASan: not drawn\n");
    return;
#endif
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        printf("glpresent: no video (%s): not drawn\n", SDL_GetError());
        return;
    }
    const char *driver = SDL_GetCurrentVideoDriver();
    SDL_Window *window = NULL;
    bool open = false;
    if (driver != NULL && strcmp(driver, "dummy") != 0) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        window = SDL_CreateWindow("glpresent", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        open = window != NULL && glpresent_open(window);
    }
    if (!open) {
        printf("glpresent: no OpenGL 3.3 context: not drawn\n");
    } else {
        static uint8_t rgba[16 * 16 * 4];
        /* Nearest at 1x and 2x, and pixel art at whole scales, give the picture's pixels. */
        UNIT_CHECK(
            glpresent_render_offscreen(s_picture, 4, 2, 2, 4, 2, SCALING_NEAREST, false, rgba));
        UNIT_CHECK(scaled_by(rgba, 4, 2, 1, 0, 0));
        UNIT_CHECK(
            glpresent_render_offscreen(s_picture, 4, 2, 2, 8, 4, SCALING_NEAREST, false, rgba));
        UNIT_CHECK(scaled_by(rgba, 8, 4, 2, 0, 0));
        UNIT_CHECK(
            glpresent_render_offscreen(s_picture, 4, 2, 2, 8, 4, SCALING_PIXELART, false, rgba));
        UNIT_CHECK(scaled_by(rgba, 8, 4, 2, 0, 0));
        UNIT_CHECK(
            glpresent_render_offscreen(s_picture, 4, 2, 2, 12, 6, SCALING_PIXELART, false, rgba));
        UNIT_CHECK(scaled_by(rgba, 12, 6, 3, 0, 0));
        /* Letterboxed: black above and below. */
        UNIT_CHECK(
            glpresent_render_offscreen(s_picture, 4, 2, 2, 8, 8, SCALING_NEAREST, false, rgba));
        UNIT_CHECK(scaled_by(rgba, 8, 8, 2, 0, 2));
        /* Pixel art at 2.5x: a picture pixel's middle keeps its colour, its edges blend. */
        UNIT_CHECK(
            glpresent_render_offscreen(s_picture, 4, 2, 2, 10, 5, SCALING_PIXELART, false, rgba));
        UNIT_CHECK(rgba[0] == 0xFF && rgba[1] == 0 && rgba[2] == 0);
        glpresent_close();
    }
    if (window != NULL)
        SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
