/*
 * The 3-D view in the window (M7's step 4). The picture the CRTC scans out keeps its 1x pixels,
 * but where they are a 3-D frame's (their tags: bak_video.h), the window shows the GPU's drawing
 * of that frame instead (glscene.c), at the window's resolution. Whatever the game draws over
 * the view clears the tags under it, so dialogs, portraits, the cursor and the map's icons stay
 * on top as 1x pixels; a dialog's background pasted back gives the view its tags again.
 *
 * The frame is the one whose tag most of the picture's tagged pixels carry. Its drawing is kept
 * until the frame, the size it is drawn at or the palette changes.
 */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bak_video.h"
#include "gl.h"
#include "glscene.h"
#include "glview.h"
#include "platform.h"
#include "scene.h"

static const char s_vertexShader[] =
    "uniform vec4 u_rect; /* the view's left, top, right and bottom in clip space */\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    v_uv = corner;\n"
    "    gl_Position = vec4(mix(u_rect.xy, u_rect.zw, corner), 0.0, 1.0);\n"
    "}\n";

/* The GPU's drawing has its first row at the bottom, as GL draws; the mask its first at the
 * top, as the picture's. */
static const char s_fragmentShader[] =
    "precision highp float;\n"
    "uniform sampler2D u_view;\n"
    "uniform sampler2D u_mask;\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "void main() {\n"
    "    if (texture(u_mask, v_uv).r < 0.5)\n"
    "        discard;\n"
    "    o_color = vec4(texture(u_view, vec2(v_uv.x, 1.0 - v_uv.y)).rgb, 1.0);\n"
    "}\n";

static struct {
    bool open;
    GLuint program, vao, mask, color, depth, fbo;
    GLint uRect;
    int fboW, fboH;
    uint32_t serial; /* the frame drawn, 0 for none */
    uint32_t paletteHash;
    unsigned shown, drawn; /* for BAK_TRACE */
} s;

bool glview_open(void) {
    if (s.open)
        return true;
    s.program = gl_program(s_vertexShader, s_fragmentShader);
    if (s.program == 0)
        return false;
    s.uRect = g_gl.GetUniformLocation(s.program, "u_rect");
    g_gl.UseProgram(s.program);
    g_gl.Uniform1i(g_gl.GetUniformLocation(s.program, "u_view"), 0);
    g_gl.Uniform1i(g_gl.GetUniformLocation(s.program, "u_mask"), 1);
    g_gl.GenVertexArrays(1, &s.vao);
    g_gl.GenTextures(1, &s.mask);
    g_gl.ActiveTexture(GL_TEXTURE1);
    g_gl.BindTexture(GL_TEXTURE_2D, s.mask);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    g_gl.ActiveTexture(GL_TEXTURE0);
    s.open = true;
    return true;
}

static void drop_target(void) {
    if (s.fbo != 0) {
        g_gl.DeleteFramebuffers(1, &s.fbo);
        GLuint t[2] = {s.color, s.depth};
        g_gl.DeleteTextures(2, t);
    }
    s.fbo = s.color = s.depth = 0;
    s.fboW = s.fboH = 0;
    s.serial = 0;
}

void glview_close(void) {
    if (!s.open)
        return;
    if (SDL_getenv("BAK_TRACE") != NULL)
        fprintf(stderr, "trace: the 3-D view on the GPU: %u pictures shown with it, %u drawings\n",
                s.shown, s.drawn);
    if (g_gl.DeleteProgram != NULL) {
        drop_target();
        g_gl.DeleteProgram(s.program);
        g_gl.DeleteVertexArrays(1, &s.vao);
        g_gl.DeleteTextures(1, &s.mask);
    }
    memset(&s, 0, sizeof s);
}

/* The GPU's drawing of the frame at w x h, into the target kept for it. */
static bool draw_frame(const SceneFrame *f, int w, int h, const uint8_t (*dac)[3],
                       uint8_t pelMask) {
    uint32_t palette = platform_hash(pelMask, dac, 256 * 3);
    if (s.fbo != 0 && s.fboW == w && s.fboH == h && s.serial == f->serial &&
        s.paletteHash == palette)
        return true;
    if (s.fbo == 0 || s.fboW != w || s.fboH != h) {
        drop_target();
        g_gl.ActiveTexture(GL_TEXTURE0);
        g_gl.GenTextures(1, &s.color);
        g_gl.BindTexture(GL_TEXTURE_2D, s.color);
        g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_gl.GenTextures(1, &s.depth);
        g_gl.BindTexture(GL_TEXTURE_2D, s.depth);
        g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT,
                        GL_UNSIGNED_INT, NULL);
        g_gl.GenFramebuffers(1, &s.fbo);
        g_gl.BindFramebuffer(GL_FRAMEBUFFER, s.fbo);
        g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s.color, 0);
        g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s.depth, 0);
        bool complete = g_gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!complete) {
            drop_target();
            return false;
        }
        s.fboW = w;
        s.fboH = h;
    }
    GlSceneInputs in = {dac, pelMask, scene_remap_tables(), bak_video_page(0xAC00)};
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, s.fbo);
    glscene_draw(f, &in, 0, 0, w, h, false);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    s.serial = f->serial;
    s.paletteHash = palette;
    s.drawn++;
    return true;
}

bool glview_draw(const uint8_t *tags, int w, int h, const SDL_FRect *r, int outW, int outH,
                 const uint8_t (*dac)[3], uint8_t pelMask) {
    if (!s.open || r->w <= 0 || r->h <= 0)
        return false;
    /* the frame most of the tagged pixels are */
    static unsigned counts[256];
    memset(counts, 0, sizeof counts);
    for (int i = 0; i < w * h; i++)
        counts[tags[i]]++;
    unsigned tag = 0, most = 0;
    for (unsigned t = 1; t < 256; t++) {
        if (counts[t] > most) {
            most = counts[t];
            tag = t;
        }
    }
    if (most < 64)
        return false;
    const SceneFrame *f = scene_frame_tagged((uint8_t)tag);
    if (f == NULL || f->background)
        return false;
    int x0 = SDL_max(f->clip[0], 0), y0 = SDL_max(f->clip[1], 0);
    int x1 = SDL_min(f->clip[2], w - 1), y1 = SDL_min(f->clip[3], h - 1);
    int vw = x1 - x0 + 1, vh = y1 - y0 + 1;
    if (vw <= 0 || vh <= 0)
        return false;

    /* the view's rectangle in the output, and the frame drawn at its size there */
    float sx = r->w / (float)w, sy = r->h / (float)h;
    float left = r->x + (float)x0 * sx, top = r->y + (float)y0 * sy;
    float width = (float)vw * sx, height = (float)vh * sy;
    int dw = (int)SDL_roundf(width), dh = (int)SDL_roundf(height);
    if (dw <= 0 || dh <= 0 || !draw_frame(f, dw, dh, dac, pelMask))
        return false;

    static uint8_t mask[BAK_VGA_WIDTH * 480];
    if ((size_t)vw * (size_t)vh > sizeof mask)
        return false;
    for (int y = 0; y < vh; y++)
        for (int x = 0; x < vw; x++)
            mask[y * vw + x] = tags[(y0 + y) * w + x0 + x] == tag ? 0xff : 0;
    g_gl.ActiveTexture(GL_TEXTURE1);
    g_gl.BindTexture(GL_TEXTURE_2D, s.mask);
    g_gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, vw, vh, 0, GL_RED, GL_UNSIGNED_BYTE, mask);
    g_gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    g_gl.ActiveTexture(GL_TEXTURE0);
    g_gl.BindTexture(GL_TEXTURE_2D, s.color);

    g_gl.Viewport(0, 0, outW, outH);
    g_gl.UseProgram(s.program);
    g_gl.Uniform4f(s.uRect, left / (float)outW * 2.0f - 1.0f, 1.0f - top / (float)outH * 2.0f,
                   (left + width) / (float)outW * 2.0f - 1.0f,
                   1.0f - (top + height) / (float)outH * 2.0f);
    g_gl.BindVertexArray(s.vao);
    g_gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    g_gl.BindVertexArray(0);
    s.shown++;
    return true;
}
