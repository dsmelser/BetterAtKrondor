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

#include "platform.h"

/* The GL functions used, loaded through SDL (GL 1.1's too: SDL finds them in opengl32). On
 * 32-bit Windows they are __stdcall (GLAPIENTRY). */
#define GL_FUNCTIONS(X)                                                                            \
    X(void, Clear, (GLbitfield))                                                                   \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                      \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))                                            \
    X(const GLubyte *, GetString, (GLenum))                                                        \
    X(void, GenTextures, (GLsizei, GLuint *))                                                      \
    X(void, DeleteTextures, (GLsizei, const GLuint *))                                             \
    X(void, BindTexture, (GLenum, GLuint))                                                         \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                                \
    X(void, TexImage2D,                                                                            \
      (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))               \
    X(void, TexSubImage2D,                                                                         \
      (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *))               \
    X(void, PixelStorei, (GLenum, GLint))                                                          \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))                  \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                                  \
    X(void, ActiveTexture, (GLenum))                                                               \
    X(GLuint, CreateShader, (GLenum))                                                              \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))                 \
    X(void, CompileShader, (GLuint))                                                               \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *))                                                \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                              \
    X(void, DeleteShader, (GLuint))                                                                \
    X(GLuint, CreateProgram, (void))                                                               \
    X(void, AttachShader, (GLuint, GLuint))                                                        \
    X(void, LinkProgram, (GLuint))                                                                 \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *))                                               \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                             \
    X(void, UseProgram, (GLuint))                                                                  \
    X(void, DeleteProgram, (GLuint))                                                               \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *))                                         \
    X(void, Uniform1i, (GLint, GLint))                                                             \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                \
    X(void, GenVertexArrays, (GLsizei, GLuint *))                                                  \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint *))                                         \
    X(void, BindVertexArray, (GLuint))                                                             \
    X(void, GenFramebuffers, (GLsizei, GLuint *))                                                  \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *))                                         \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                     \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                         \
    X(GLenum, CheckFramebufferStatus, (GLenum))

#define GLP_DECLARE(ret, name, args) ret(GLAPIENTRY *name) args;
static struct {
    GL_FUNCTIONS(GLP_DECLARE)
} gl;
#undef GLP_DECLARE

static SDL_Window *s_window;
static SDL_GLContext s_context;
static GLuint s_program, s_texture, s_vao;
static GLint s_uRect, s_uTexel, s_uPixelArt;
static int s_texW, s_texH; /* the texture's size, 0 before the first picture */

/* The version line goes first; the rest is the subset GLSL 3.30 and GLSL ES 3.00 share. */
static const char s_version[] = "#version 330 core\n";

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

static bool load_functions(void) {
#define GLP_LOAD(ret, name, args)                                                                  \
    {                                                                                              \
        SDL_FunctionPointer f = SDL_GL_GetProcAddress("gl" #name);                                 \
        if (f == NULL) {                                                                           \
            fprintf(stderr, "OpenGL: no gl" #name "\n");                                           \
            return false;                                                                          \
        }                                                                                          \
        memcpy(&gl.name, &f, sizeof f);                                                            \
    }
    GL_FUNCTIONS(GLP_LOAD)
#undef GLP_LOAD
    return true;
}

static GLuint compile(GLenum kind, const char *source) {
    GLuint shader = gl.CreateShader(kind);
    const GLchar *parts[] = {s_version, source};
    gl.ShaderSource(shader, 2, parts, NULL);
    gl.CompileShader(shader);
    GLint ok = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        gl.GetShaderInfoLog(shader, sizeof log, NULL, log);
        fprintf(stderr, "OpenGL: a %s shader does not compile: %s\n",
                kind == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        gl.DeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool build_program(void) {
    GLuint vs = compile(GL_VERTEX_SHADER, s_vertexShader);
    GLuint fs = compile(GL_FRAGMENT_SHADER, s_fragmentShader);
    if (vs == 0 || fs == 0) {
        gl.DeleteShader(vs);
        gl.DeleteShader(fs);
        return false;
    }
    s_program = gl.CreateProgram();
    gl.AttachShader(s_program, vs);
    gl.AttachShader(s_program, fs);
    gl.LinkProgram(s_program);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    GLint ok = 0;
    gl.GetProgramiv(s_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        gl.GetProgramInfoLog(s_program, sizeof log, NULL, log);
        fprintf(stderr, "OpenGL: the shaders do not link: %s\n", log);
        return false;
    }
    s_uRect = gl.GetUniformLocation(s_program, "u_rect");
    s_uTexel = gl.GetUniformLocation(s_program, "u_texel");
    s_uPixelArt = gl.GetUniformLocation(s_program, "u_pixelArt");
    gl.UseProgram(s_program);
    gl.Uniform1i(gl.GetUniformLocation(s_program, "u_picture"), 0);
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
    if (!load_functions() || !build_program()) {
        glpresent_close();
        return false;
    }
    SDL_GL_SetSwapInterval(0); /* the game paces its frames itself */
    gl.GenTextures(1, &s_texture);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, s_texture);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.GenVertexArrays(1, &s_vao);
    gl.BindVertexArray(s_vao);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    s_texW = s_texH = 0;
    return true;
}

void glpresent_close(void) {
    if (s_context == NULL)
        return;
    if (gl.DeleteProgram != NULL) {
        gl.DeleteProgram(s_program);
        gl.DeleteTextures(1, &s_texture);
        gl.DeleteVertexArrays(1, &s_vao);
    }
    SDL_GL_DestroyContext(s_context);
    memset(&gl, 0, sizeof gl);
    s_context = NULL;
    s_window = NULL;
    s_program = s_texture = s_vao = 0;
}

const char *glpresent_name(void) {
    if (s_context == NULL)
        return NULL;
    static char name[256];
    snprintf(name, sizeof name, "OpenGL %s (%s)", (const char *)gl.GetString(GL_VERSION),
             (const char *)gl.GetString(GL_RENDERER));
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
    gl.BindTexture(GL_TEXTURE_2D, s_texture);
    if (w != s_texW || h != s_texH) {
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, xrgb);
        s_texW = w;
        s_texH = h;
    } else {
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, xrgb);
    }
    GLint filter = scaling == SCALING_NEAREST ? GL_NEAREST : GL_LINEAR;
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

    SDL_FRect r;
    glpresent_layout(outW, outH, w, shownH, integer, &r);
    gl.Viewport(0, 0, outW, outH);
    gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    gl.UseProgram(s_program);
    gl.Uniform4f(s_uRect, r.x / (float)outW * 2.0f - 1.0f, 1.0f - r.y / (float)outH * 2.0f,
                 (r.x + r.w) / (float)outW * 2.0f - 1.0f, 1.0f - (r.y + r.h) / (float)outH * 2.0f);
    gl.Uniform4f(s_uTexel, 1.0f / (float)w, 1.0f / (float)h, (float)w, (float)h);
    gl.Uniform1i(s_uPixelArt, scaling == SCALING_PIXELART);
    gl.BindVertexArray(s_vao);
    gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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
    gl.GenTextures(1, &target);
    gl.BindTexture(GL_TEXTURE_2D, target);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, outW, outH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl.GenFramebuffers(1, &fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    bool ok = gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glpresent_render(xrgb, w, h, shownH, outW, outH, scaling, integer);
        gl.ReadPixels(0, 0, outW, outH, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
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
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.DeleteFramebuffers(1, &fbo);
    gl.DeleteTextures(1, &target);
    return ok;
}
