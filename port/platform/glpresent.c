/*
 * The window's picture through OpenGL: a 3.3 core context, whose shaders are written in the
 * subset GLES 3 (WebGL 2) shares, so that only their version line changes for the web. The 1x
 * picture the CRTC scans out (video.c) is a texture, drawn into the window's letterbox as the
 * settings ask: pixel art (each window pixel blends the picture's pixels its footprint covers,
 * so edges stay sharp without the uneven rows nearest gives at a scale that is not whole),
 * nearest or linear, optionally by whole multiples only. This is the context the 3-D view will
 * be drawn in at the display's resolution. Where no 3.3 context can be made (an old driver,
 * SDL's dummy video driver) video.c keeps SDL's renderer, as it does for video.renderer=sdl.
 */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "gl.h"
#include "platform.h"

static SDL_Window *s_window;
static SDL_GLContext s_context;
static GLuint s_program, s_texture, s_vao;
static GLint s_uRect, s_uTexel, s_uPixelArt;
static int s_texW, s_texH; /* the texture's size, 0 before the first picture */

/* A quad over the picture's rectangle, from the vertex's index alone: (0,0) (1,0) (0,1) (1,1),
 * the texture's first row at the top. */
static const char s_vertexShader[] =
    "uniform vec4 u_rect; /* the picture's left, top, right and bottom in clip space */\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    v_uv = corner;\n"
    "    gl_Position = vec4(mix(u_rect.xy, u_rect.zw, corner), 0.0, 1.0);\n"
    "}\n";

/* The picture's pixels are XRGB words, uploaded as RGBA bytes: blue, green, red, nothing. */
static const char s_fragmentShader[] =
    "uniform sampler2D u_picture;\n"
    "uniform vec4 u_texel; /* 1/width, 1/height, width, height */\n"
    "uniform int u_pixelArt;\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    vec2 uv = v_uv;\n"
    "    if (u_pixelArt != 0) {\n"
    "        /* The window pixel's footprint in picture pixels, at most one: inside a picture\n"
    "           pixel the sample stays at its middle, and across an edge it slides to the next\n"
    "           as far as the footprint is over it, the linear filter blending the two. */\n"
    "        vec2 box = clamp(fwidth(v_uv) * u_texel.zw, 1e-5, 1.0);\n"
    "        vec2 tx = v_uv * u_texel.zw - 0.5 * box;\n"
    "        vec2 slide = smoothstep(vec2(1.0) - box, vec2(1.0), fract(tx));\n"
    "        uv = (floor(tx) + 0.5 + slide) * u_texel.xy;\n"
    "    }\n"
    "    o_color = vec4(texture(u_picture, uv).bgr, 1.0);\n"
    "}\n";

static bool build_program(void) {
    s_program = gl_program(s_vertexShader, s_fragmentShader);
    if (s_program == 0)
        return false;
    s_uRect = g_gl.GetUniformLocation(s_program, "u_rect");
    s_uTexel = g_gl.GetUniformLocation(s_program, "u_texel");
    s_uPixelArt = g_gl.GetUniformLocation(s_program, "u_pixelArt");
    g_gl.UseProgram(s_program);
    g_gl.Uniform1i(g_gl.GetUniformLocation(s_program, "u_picture"), 0);
    return true;
}

bool glpresent_open(SDL_Window *window) {
    s_window = window;
    s_context = SDL_GL_CreateContext(window);
    if (s_context == NULL) {
        fprintf(stderr, "OpenGL 3.3: %s\n", SDL_GetError());
        s_window = NULL;
        return false;
    }
    if (!gl_load() || !build_program()) {
        glpresent_close();
        return false;
    }
    SDL_GL_SetSwapInterval(0); /* the game paces its frames itself */
    g_gl.GenTextures(1, &s_texture);
    g_gl.ActiveTexture(GL_TEXTURE0);
    g_gl.BindTexture(GL_TEXTURE_2D, s_texture);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_gl.GenVertexArrays(1, &s_vao);
    g_gl.BindVertexArray(s_vao);
    g_gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    g_gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    s_texW = s_texH = 0;
    return true;
}

void glpresent_close(void) {
    if (s_context == NULL)
        return;
    if (g_gl.DeleteProgram != NULL) {
        g_gl.DeleteProgram(s_program);
        g_gl.DeleteTextures(1, &s_texture);
        g_gl.DeleteVertexArrays(1, &s_vao);
    }
    SDL_GL_DestroyContext(s_context);
    gl_unload();
    s_context = NULL;
    s_window = NULL;
    s_program = s_texture = s_vao = 0;
}

const char *glpresent_name(void) {
    if (s_context == NULL)
        return NULL;
    static char name[256];
    snprintf(name, sizeof name, "OpenGL %s (%s)", (const char *)g_gl.GetString(GL_VERSION),
             (const char *)g_gl.GetString(GL_RENDERER));
    return name;
}

void glpresent_layout(int outW, int outH, int w, int shownH, bool integer, SDL_FRect *r) {
    float scale = SDL_min((float)outW / (float)w, (float)outH / (float)shownH);
    if (integer && scale >= 1.0f)
        scale = SDL_floorf(scale);
    r->w = SDL_roundf((float)w * scale);
    r->h = SDL_roundf((float)shownH * scale);
    r->x = SDL_floorf(((float)outW - r->w) / 2.0f);
    r->y = SDL_floorf(((float)outH - r->h) / 2.0f);
}

void glpresent_render(const uint32_t *xrgb, int w, int h, int shownH, int outW, int outH,
                      int scaling, bool integer) {
    g_gl.BindTexture(GL_TEXTURE_2D, s_texture);
    if (w != s_texW || h != s_texH) {
        g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, xrgb);
        s_texW = w;
        s_texH = h;
    } else {
        g_gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, xrgb);
    }
    GLint filter = scaling == SCALING_NEAREST ? GL_NEAREST : GL_LINEAR;
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

    SDL_FRect r;
    glpresent_layout(outW, outH, w, shownH, integer, &r);
    g_gl.Viewport(0, 0, outW, outH);
    g_gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    g_gl.Clear(GL_COLOR_BUFFER_BIT);
    g_gl.UseProgram(s_program);
    g_gl.Uniform4f(s_uRect, r.x / (float)outW * 2.0f - 1.0f, 1.0f - r.y / (float)outH * 2.0f,
                   (r.x + r.w) / (float)outW * 2.0f - 1.0f,
                   1.0f - (r.y + r.h) / (float)outH * 2.0f);
    g_gl.Uniform4f(s_uTexel, 1.0f / (float)w, 1.0f / (float)h, (float)w, (float)h);
    g_gl.Uniform1i(s_uPixelArt, scaling == SCALING_PIXELART);
    g_gl.BindVertexArray(s_vao);
    g_gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void glpresent_draw(const uint32_t *xrgb, int w, int h, int shownH, int scaling, bool integer) {
    int outW = 0, outH = 0;
    SDL_GetWindowSizeInPixels(s_window, &outW, &outH);
    if (outW <= 0 || outH <= 0)
        return; /* minimized */
    glpresent_render(xrgb, w, h, shownH, outW, outH, scaling, integer);
    SDL_GL_SwapWindow(s_window);
}

bool glpresent_render_offscreen(const uint32_t *xrgb, int w, int h, int shownH, int outW, int outH,
                                int scaling, bool integer, uint8_t *rgba) {
    GLuint fbo = 0, target = 0;
    g_gl.GenTextures(1, &target);
    g_gl.BindTexture(GL_TEXTURE_2D, target);
    g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, outW, outH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    g_gl.GenFramebuffers(1, &fbo);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    bool ok = g_gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glpresent_render(xrgb, w, h, shownH, outW, outH, scaling, integer);
        g_gl.ReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        /* GL's rows count from the bottom: the picture's first row last. */
        size_t pitch = (size_t)outW * 4;
        uint8_t row[4 * 1024];
        for (int y = 0; y < outH / 2 && pitch <= sizeof row; y++) {
            uint8_t *a = rgba + (size_t)y * pitch, *b = rgba + (size_t)(outH - 1 - y) * pitch;
            memcpy(row, a, pitch);
            memcpy(a, b, pitch);
            memcpy(b, row, pitch);
        }
        ok = pitch <= sizeof row;
    }
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    g_gl.DeleteFramebuffers(1, &fbo);
    g_gl.DeleteTextures(1, &target);
    return ok;
}
