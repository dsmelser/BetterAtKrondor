/*
 * The 3-D view on the GPU (M7's step 3; glscene.h). A frame of the software pass (scene.h) is
 * drawn again in its own order: the sky's and the ground's bands in screen space, then the
 * items. Faces, billboards and circles go through the game's own projection (its focal length
 * and the view's centre, the view rectangle scaled to the output) and a depth buffer: the
 * objects the software draws first by their priority (the ground's shapes) neither test nor
 * write depth, as everything drawn after them lies over them; the others do, and each face of
 * an object is pulled a step of depth nearer than the one drawn before it, so that the decals
 * the software paints over a face stay on top. Texture coordinates are interpolated with
 * perspective. A pixel's palette index comes from its face's colour, its image (through its
 * remap table, 0 transparent but on the sky), or the ground's strip texture, read as vgablit.c's
 * spans read it at the pixel's place in the 1x view; its colour is the DAC's for that index.
 *
 * With BAK_SCENE_CHECK=gpu, every finished frame is drawn at 1x as palette indices and compared
 * with the page the software drew it into, over the view; the share of equal pixels goes to
 * stderr at exit. BAK_SCENE_DUMP=<folder> writes the first frames' pictures there: the
 * software's view, the GPU's at 1x, and the GPU's at 4x.
 */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "bak_video.h"
#include "gl.h"
#include "glscene.h"
#include "platform.h"
#include "vgaregs.h"

enum { MODE_COLOUR, MODE_IMAGE, MODE_OPAQUE, MODE_STRIP, MODE_RAMP, MODE_DISC };
enum { DEPTH_NONE, DEPTH_WRITE, DEPTH_TEST };
enum { UNIT_IMAGE, UNIT_REMAP, UNIT_GROUND, UNIT_PALETTE };

#define GROUND_W 320
#define GROUND_H 205 /* the scratch page's 65536 pixels, the last row padded */
#define RAMP_ROW 15  /* the remap texture's row after the 15 tables: the sky's shade ramp */

static const char s_vertexShader[] =
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "uniform vec4 u_proj;  /* clip x = x * proj.x + depth * proj.y, clip y = z * proj.z + depth * "
    "proj.w */\n"
    "uniform vec2 u_depth; /* clip z = depth * u_depth.x + u_depth.y */\n"
    "uniform vec4 u_view;  /* the 1x view: left, top, width, height */\n"
    "uniform int u_screen; /* a_pos is a place in the 1x picture */\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    if (u_screen != 0)\n"
    "        gl_Position = vec4(2.0 * (a_pos.x - u_view.x) / u_view.z - 1.0,\n"
    "                           1.0 - 2.0 * (a_pos.y - u_view.y) / u_view.w, 0.0, 1.0);\n"
    "    else\n"
    "        gl_Position = vec4(a_pos.x * u_proj.x + a_pos.y * u_proj.y,\n"
    "                           a_pos.z * u_proj.z + a_pos.y * u_proj.w,\n"
    "                           a_pos.y * u_depth.x + u_depth.y, a_pos.y);\n"
    "}\n";

static const char s_fragmentShader[] =
    "precision highp float;\n"
    "precision highp int;\n"
    "uniform highp usampler2D u_image;\n"
    "uniform highp usampler2D u_remap;\n"
    "uniform highp usampler2D u_ground;\n"
    "uniform highp sampler2D u_palette;\n"
    "uniform int u_mode;\n"
    "uniform int u_colour;\n"
    "uniform int u_remapRow;\n"
    "uniform ivec2 u_imageSize;\n"
    "uniform ivec4 u_strip; /* base, stride, row subtracted, rows before the base again */\n"
    "uniform int u_stripFirst;\n"
    "uniform vec4 u_view; /* the 1x view: left, top, width, height */\n"
    "uniform vec4 u_frag; /* the view's left and top in window pixels, 1x pixels per window "
    "pixel */\n"
    "uniform vec3 u_disc; /* a disc's centre and radius in the 1x picture */\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "vec2 place() {\n"
    "    return vec2(u_view.x + (gl_FragCoord.x - u_frag.x) * u_frag.z,\n"
    "                u_view.y + (u_frag.y - gl_FragCoord.y) * u_frag.w);\n"
    "}\n"
    "int wrap(int a, int n) { /* a mod n for n > 0: GLSL leaves % of a negative undefined */\n"
    "    return a >= 0 ? a % n : n - 1 - (-a - 1) % n;\n"
    "}\n"
    "void main() {\n"
    "    int index = u_colour;\n"
    "    if (u_mode == 1 || u_mode == 2) {\n"
    "        ivec2 t = clamp(ivec2(floor(v_uv)), ivec2(0), u_imageSize - 1);\n"
    "        index = int(texelFetch(u_image, t, 0).r);\n"
    "        if (u_mode == 1 && index == 0)\n"
    "            discard;\n"
    "        if (u_remapRow >= 0)\n"
    "            index = int(texelFetch(u_remap, ivec2(index, u_remapRow), 0).r);\n"
    "    } else if (u_mode == 3) {\n"
    "        ivec2 p = ivec2(floor(place()));\n"
    "        int si = u_strip.w == 0 ? u_strip.x + (p.y - u_strip.z) * u_strip.y\n"
    "                                : u_strip.x + wrap(p.y - u_stripFirst, u_strip.w) * u_strip.y;\n"
    "        int at = 4 * ((si + (p.x >> 2)) & 0xffff) + (p.x & 3);\n"
    "        index = at < 65536 ? int(texelFetch(u_ground, ivec2(at % 320, at / 320), 0).r) : 0;\n"
    "    } else if (u_mode == 4) {\n"
    "        int k = u_colour - int(floor(place().y)); /* rows up from the band's last */\n"
    "        index = int(texelFetch(u_remap, ivec2(clamp(k, 0, 255), 15), 0).r);\n"
    "    } else if (u_mode == 5) {\n"
    "        vec2 d = place() - (u_disc.xy + 0.5);\n"
    "        if (dot(d, d) > (u_disc.z + 0.5) * (u_disc.z + 0.5))\n"
    "            discard;\n"
    "    }\n"
    "#ifdef INDICES\n"
    "    o_color = vec4(float(index) / 255.0, 0.0, 0.0, 1.0);\n"
    "#else\n"
    "    o_color = texelFetch(u_palette, ivec2(index, 0), 0);\n"
    "#endif\n"
    "}\n";

typedef struct Vertex {
    float x, y, z, u, v;
} Vertex;

typedef struct Draw {
    GLenum primitive;
    uint32_t first, count;
    uint8_t mode, screen, depth;
    int8_t remap;
    int colour;
    uint32_t texture;
    float offset; /* polygon offset units */
    SceneStrip strip;
    int stripFirst;
    float disc[3];
} Draw;

typedef struct Uniforms {
    GLint proj, depth, view, screen, mode, colour, remapRow, imageSize, strip, stripFirst, frag,
        disc;
} Uniforms;

static struct {
    bool open;
    GLuint program[2]; /* colours, indices */
    Uniforms u[2];
    GLuint vao, vbo;
    GLuint remap, ground, palette;
    uint32_t groundHash;
    bool groundKnown;
    GLuint *textures; /* by texture id */
    uint32_t textureCap;
    Vertex *vertices;
    uint32_t vertexCount, vertexCap;
    Draw *draws;
    uint32_t drawCount, drawCap;
} s;

static bool grow(void **array, uint32_t *cap, uint32_t need, size_t size) {
    if (need <= *cap)
        return true;
    uint32_t n = *cap ? *cap : 256;
    while (n < need)
        n *= 2;
    void *p = realloc(*array, (size_t)n * size);
    if (p == NULL)
        return false;
    *array = p;
    *cap = n;
    return true;
}

/* ---- the draws ---------------------------------------------------------------------- */

static Draw *new_draw(GLenum primitive, uint8_t mode) {
    if (!grow((void **)&s.draws, &s.drawCap, s.drawCount + 1, sizeof *s.draws))
        return NULL;
    Draw *d = &s.draws[s.drawCount++];
    memset(d, 0, sizeof *d);
    d->primitive = primitive;
    d->mode = mode;
    d->remap = -1;
    d->first = s.vertexCount;
    return d;
}

static void vertex(Draw *d, float x, float y, float z, float u, float v) {
    if (d == NULL ||
        !grow((void **)&s.vertices, &s.vertexCap, s.vertexCount + 1, sizeof *s.vertices))
        return;
    s.vertices[s.vertexCount++] = (Vertex){x, y, z, u, v};
    d->count++;
}

/* A rectangle of the 1x picture, the image's texels u0..u1, v0..v1 across it. */
static Draw *screen_rect(uint8_t mode, float x, float y, float w, float h, float u0, float v0,
                         float u1, float v1) {
    Draw *d = new_draw(GL_TRIANGLES, mode);
    if (d == NULL)
        return NULL;
    d->screen = 1;
    vertex(d, x, y, 0, u0, v0);
    vertex(d, x + w, y, 0, u1, v0);
    vertex(d, x + w, y + h, 0, u1, v1);
    vertex(d, x, y, 0, u0, v0);
    vertex(d, x + w, y + h, 0, u1, v1);
    vertex(d, x, y + h, 0, u0, v1);
    return d;
}

static void sky(const SceneSkyOp *op) {
    Draw *d = NULL;
    switch (op->kind) {
    case SCENE_SKY_BAND:
    case SCENE_SKY_RECT:
        d = screen_rect(MODE_COLOUR, op->x, op->y, op->w, op->h, 0, 0, 0, 0);
        if (d != NULL)
            d->colour = op->colour;
        break;
    case SCENE_SKY_SHADED:
        d = screen_rect(MODE_RAMP, op->x, op->y, op->w, op->h, 0, 0, 0, 0);
        if (d != NULL)
            d->colour = op->y + op->h - 1;
        break;
    case SCENE_SKY_SPRITE:
        d = screen_rect(MODE_OPAQUE, op->x, op->y, op->w, op->h, 0, 0, op->w, op->h);
        if (d != NULL)
            d->texture = op->texture;
        break;
    case SCENE_SKY_GROUND:
        d = screen_rect(MODE_STRIP, op->x, op->y, op->w, op->h, 0, 0, 0, 0);
        if (d != NULL) {
            d->strip = op->strip;
            d->stripFirst = op->y;
        }
        break;
    }
}

/* Image corner k's texels: top left, top right, bottom right, bottom left. */
static void image_corner(uint8_t k, const SceneTexture *t, float *u, float *v) {
    float w = t != NULL ? t->width : 1, h = t != NULL ? t->height : 1;
    *u = k == 1 || k == 2 ? w : 0;
    *v = k >= 2 ? h : 0;
}

static void face(const SceneFrame *f, const SceneItem *it, uint8_t depth, float offset) {
    const SceneVertex *v = &f->vertices[it->firstVertex];
    uint8_t mode = it->textured ? MODE_IMAGE : it->striped ? MODE_STRIP : MODE_COLOUR;
    const SceneTexture *t = it->textured ? scene_texture(it->texture) : NULL;
    if (it->textured || it->striped || it->filled) {
        Draw *d = new_draw(GL_TRIANGLES, mode);
        if (d == NULL)
            return;
        d->depth = depth;
        d->offset = offset;
        d->colour = it->fill;
        d->texture = it->texture;
        d->remap = it->remap;
        d->strip = it->strip;
        d->stripFirst = it->rect[1];
        for (unsigned i = 1; i + 1 < it->corners; i++) {
            unsigned c[3] = {0, i, i + 1};
            for (unsigned k = 0; k < 3; k++) {
                float u = 0, w = 0;
                if (it->textured && c[k] < 4)
                    image_corner(it->imageCorner[c[k]], t, &u, &w);
                vertex(d, v[c[k]].x, v[c[k]].y, v[c[k]].z, u, w);
            }
        }
    }
    /* draw_polygon: an outline in its own colour after the fill, or alone without one */
    if (!it->textured && !it->striped && (!it->filled || it->outline != it->fill)) {
        Draw *d = new_draw(GL_LINE_LOOP, MODE_COLOUR);
        if (d == NULL)
            return;
        d->depth = depth == DEPTH_NONE ? DEPTH_NONE : DEPTH_TEST;
        d->colour = it->outline;
        for (unsigned i = 0; i < it->corners; i++)
            vertex(d, v[i].x, v[i].y, v[i].z, 0, 0);
    }
}

/* A point of the 1x picture at a depth, back in camera space. */
static void unproject(const SceneFrame *f, float sx, float sy, float depth, float *x, float *z) {
    float focal = ldexpf(1.0f, f->screenShift);
    *x = (sx - (float)f->centerX) * depth / focal;
    *z = ((float)f->centerY - sy) * depth / focal;
}

/* A billboard: its rectangle on screen, at its anchor's depth. */
static void billboard(const SceneFrame *f, const SceneItem *it) {
    const SceneTexture *t = scene_texture(it->texture);
    float depth = f->vertices[it->firstVertex].y;
    if (t == NULL || depth <= 0)
        return;
    Draw *d = new_draw(GL_TRIANGLES, MODE_IMAGE);
    if (d == NULL)
        return;
    d->depth = DEPTH_WRITE;
    d->texture = it->texture;
    d->remap = it->remap;
    float x0, z0, x1, z1;
    unproject(f, it->rect[0], it->rect[1], depth, &x0, &z0);
    unproject(f, (float)(it->rect[0] + it->rect[2]), (float)(it->rect[1] + it->rect[3]), depth, &x1,
              &z1);
    float u0 = 0, u1 = t->width, v0 = 0, v1 = t->height;
    if (it->flip & 2) {
        u0 = u1;
        u1 = 0;
    }
    if (it->flip & 1) {
        v0 = v1;
        v1 = 0;
    }
    vertex(d, x0, depth, z0, u0, v0);
    vertex(d, x1, depth, z0, u1, v0);
    vertex(d, x1, depth, z1, u1, v1);
    vertex(d, x0, depth, z0, u0, v0);
    vertex(d, x1, depth, z1, u1, v1);
    vertex(d, x0, depth, z1, u0, v1);
}

static void circle(const SceneFrame *f, const SceneItem *it) {
    float depth = f->vertices[it->firstVertex].y, r = it->rect[2] + 1.0f;
    if (depth <= 0)
        return;
    Draw *d = new_draw(GL_TRIANGLES, MODE_DISC);
    if (d == NULL)
        return;
    d->depth = DEPTH_WRITE;
    d->colour = it->fill;
    d->disc[0] = it->rect[0];
    d->disc[1] = it->rect[1];
    d->disc[2] = it->rect[2];
    float x0, z0, x1, z1;
    unproject(f, it->rect[0] - r, it->rect[1] - r, depth, &x0, &z0);
    unproject(f, it->rect[0] + r + 1, it->rect[1] + r + 1, depth, &x1, &z1);
    vertex(d, x0, depth, z0, 0, 0);
    vertex(d, x1, depth, z0, 0, 0);
    vertex(d, x1, depth, z1, 0, 0);
    vertex(d, x0, depth, z0, 0, 0);
    vertex(d, x1, depth, z1, 0, 0);
    vertex(d, x0, depth, z1, 0, 0);
}

static void build(const SceneFrame *f) {
    s.vertexCount = s.drawCount = 0;
    for (uint32_t i = 0; i < f->skyCount; i++)
        sky(&f->sky[i]);
    uint32_t object = UINT32_MAX;
    float step = 0;
    for (uint32_t i = 0; i < f->itemCount; i++) {
        const SceneItem *it = &f->items[i];
        if (it->fate != SCENE_DRAWN)
            continue;
        bool ground = it->object != SCENE_NO_OBJECT && it->object < f->objectCount &&
                      f->objects[it->object].priority != 0;
        if (it->object != object) {
            object = it->object;
            step = 0;
        }
        switch (it->type) {
        case SCENE_FACE:
            face(f, it, ground ? DEPTH_NONE : DEPTH_WRITE, step);
            step -= 2.0f;
            break;
        case SCENE_BILLBOARD:
            billboard(f, it);
            break;
        case SCENE_CIRCLE:
            circle(f, it);
            break;
        }
    }
}

/* ---- OpenGL ------------------------------------------------------------------------- */

static GLuint new_texture(GLenum unit) {
    GLuint t = 0;
    g_gl.GenTextures(1, &t);
    g_gl.ActiveTexture(unit);
    g_gl.BindTexture(GL_TEXTURE_2D, t);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static bool program(int which, const char *fragment) {
    GLuint p = gl_program(s_vertexShader, fragment);
    if (p == 0)
        return false;
    s.program[which] = p;
    Uniforms *u = &s.u[which];
    u->proj = g_gl.GetUniformLocation(p, "u_proj");
    u->depth = g_gl.GetUniformLocation(p, "u_depth");
    u->view = g_gl.GetUniformLocation(p, "u_view");
    u->screen = g_gl.GetUniformLocation(p, "u_screen");
    u->mode = g_gl.GetUniformLocation(p, "u_mode");
    u->colour = g_gl.GetUniformLocation(p, "u_colour");
    u->remapRow = g_gl.GetUniformLocation(p, "u_remapRow");
    u->imageSize = g_gl.GetUniformLocation(p, "u_imageSize");
    u->strip = g_gl.GetUniformLocation(p, "u_strip");
    u->stripFirst = g_gl.GetUniformLocation(p, "u_stripFirst");
    u->frag = g_gl.GetUniformLocation(p, "u_frag");
    u->disc = g_gl.GetUniformLocation(p, "u_disc");
    g_gl.UseProgram(p);
    g_gl.Uniform1i(g_gl.GetUniformLocation(p, "u_image"), UNIT_IMAGE);
    g_gl.Uniform1i(g_gl.GetUniformLocation(p, "u_remap"), UNIT_REMAP);
    g_gl.Uniform1i(g_gl.GetUniformLocation(p, "u_ground"), UNIT_GROUND);
    g_gl.Uniform1i(g_gl.GetUniformLocation(p, "u_palette"), UNIT_PALETTE);
    return true;
}

static void check_init(void);

bool glscene_open(void) {
    if (s.open)
        return true;
    static char indices[sizeof s_fragmentShader + 32];
    snprintf(indices, sizeof indices, "#define INDICES 1\n%s", s_fragmentShader);
    if (!program(0, s_fragmentShader) || !program(1, indices)) {
        glscene_close();
        return false;
    }
    g_gl.GenVertexArrays(1, &s.vao);
    g_gl.BindVertexArray(s.vao);
    g_gl.GenBuffers(1, &s.vbo);
    g_gl.BindBuffer(GL_ARRAY_BUFFER, s.vbo);
    g_gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void *)0);
    g_gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                             (const void *)offsetof(Vertex, u));
    g_gl.EnableVertexAttribArray(0);
    g_gl.EnableVertexAttribArray(1);
    g_gl.BindVertexArray(0);
    s.remap = new_texture(GL_TEXTURE0 + UNIT_REMAP);
    s.palette = new_texture(GL_TEXTURE0 + UNIT_PALETTE);
    s.ground = new_texture(GL_TEXTURE0 + UNIT_GROUND);
    static const uint8_t none[GROUND_W * GROUND_H]; /* until a strip texture comes */
    g_gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, GROUND_W, GROUND_H, 0, GL_RED_INTEGER,
                    GL_UNSIGNED_BYTE, none);
    g_gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    g_gl.ActiveTexture(GL_TEXTURE0);
    s.open = true;
    s.groundKnown = false;
    check_init();
    return true;
}

void glscene_close(void) {
    if (g_gl.DeleteProgram != NULL) {
        g_gl.DeleteProgram(s.program[0]);
        g_gl.DeleteProgram(s.program[1]);
        g_gl.DeleteVertexArrays(1, &s.vao);
        g_gl.DeleteBuffers(1, &s.vbo);
        GLuint t[3] = {s.remap, s.ground, s.palette};
        g_gl.DeleteTextures(3, t);
        for (uint32_t i = 0; i < s.textureCap; i++)
            if (s.textures[i] != 0)
                g_gl.DeleteTextures(1, &s.textures[i]);
    }
    free(s.textures);
    free(s.vertices);
    free(s.draws);
    memset(&s, 0, sizeof s);
}

/* The GL texture of a scene texture, made as it is first drawn. */
static GLuint texture_for(uint32_t id) {
    const SceneTexture *t = scene_texture(id);
    if (t == NULL)
        return 0;
    if (id >= s.textureCap) {
        uint32_t old = s.textureCap;
        if (!grow((void **)&s.textures, &s.textureCap, id + 1, sizeof *s.textures))
            return 0;
        memset(s.textures + old, 0, (s.textureCap - old) * sizeof *s.textures);
    }
    if (s.textures[id] == 0) {
        s.textures[id] = new_texture(GL_TEXTURE0 + UNIT_IMAGE);
        g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, t->width, t->height, 0, GL_RED_INTEGER,
                        GL_UNSIGNED_BYTE, t->indices);
    }
    return s.textures[id];
}

static void upload(const GlSceneInputs *in) {
    static uint8_t rgba[256 * 4];
    static uint8_t remap[256 * 16];
    for (int i = 0; i < 256; i++) {
        const uint8_t *c = in->dac != NULL ? in->dac[i & in->pelMask] : (const uint8_t *)"\0\0";
        for (int k = 0; k < 3; k++)
            rgba[i * 4 + k] = (uint8_t)(c[k] << 2 | c[k] >> 4);
        rgba[i * 4 + 3] = 0xff;
    }
    g_gl.ActiveTexture(GL_TEXTURE0 + UNIT_PALETTE);
    g_gl.BindTexture(GL_TEXTURE_2D, s.palette);
    g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    memset(remap, 0, sizeof remap);
    if (in->remapTables != NULL)
        memcpy(remap, in->remapTables, 15 * 256);
    memcpy(remap + RAMP_ROW * 256, g_skyShadeRamp, sizeof g_skyShadeRamp);
    g_gl.ActiveTexture(GL_TEXTURE0 + UNIT_REMAP);
    g_gl.BindTexture(GL_TEXTURE_2D, s.remap);
    g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 256, 16, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, remap);

    g_gl.ActiveTexture(GL_TEXTURE0 + UNIT_GROUND);
    g_gl.BindTexture(GL_TEXTURE_2D, s.ground);
    if (in->ground != NULL) {
        uint32_t hash = platform_hash(0, in->ground, 65536);
        if (!s.groundKnown || hash != s.groundHash) {
            static uint8_t ground[GROUND_W * GROUND_H];
            memcpy(ground, in->ground, 65536);
            g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, GROUND_W, GROUND_H, 0, GL_RED_INTEGER,
                            GL_UNSIGNED_BYTE, ground);
            s.groundHash = hash;
            s.groundKnown = true;
        }
    }
}

void glscene_draw(const SceneFrame *f, const GlSceneInputs *in, int x, int y, int w, int h,
                  bool indices) {
    if (!s.open || w <= 0 || h <= 0)
        return;
    build(f);
    g_gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    upload(in);

    float viewW = (float)(f->clip[2] - f->clip[0] + 1), viewH = (float)(f->clip[3] - f->clip[1] + 1);
    if (viewW <= 0 || viewH <= 0)
        return;
    float focal = ldexpf(1.0f, f->screenShift), nearZ = focal, farZ = 65536.0f;
    for (uint32_t i = 0; i < f->vertexCount; i++)
        farZ = SDL_max(farZ, 2.0f * f->vertices[i].y);
    const Uniforms *u = &s.u[indices];
    g_gl.UseProgram(s.program[indices]);
    g_gl.Uniform4f(u->proj, 2.0f * focal / viewW, 2.0f * ((float)f->centerX - f->clip[0]) / viewW - 1.0f,
                   2.0f * focal / viewH, 1.0f - 2.0f * ((float)f->centerY - f->clip[1]) / viewH);
    g_gl.Uniform2f(u->depth, (farZ + nearZ) / (farZ - nearZ), -2.0f * farZ * nearZ / (farZ - nearZ));
    g_gl.Uniform4f(u->view, f->clip[0], f->clip[1], viewW, viewH);
    g_gl.Uniform4f(u->frag, (float)x, (float)(y + h), viewW / (float)w, viewH / (float)h);

    g_gl.BindVertexArray(s.vao);
    g_gl.BindBuffer(GL_ARRAY_BUFFER, s.vbo);
    g_gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(s.vertexCount * sizeof(Vertex)), s.vertices,
                    GL_STREAM_DRAW);
    g_gl.Viewport(x, y, w, h);
    g_gl.Scissor(x, y, w, h);
    g_gl.Enable(GL_SCISSOR_TEST);
    g_gl.ClearColor(0, 0, 0, 1);
    g_gl.DepthMask(GL_TRUE);
    g_gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    g_gl.DepthFunc(GL_LEQUAL);
    g_gl.ActiveTexture(GL_TEXTURE0 + UNIT_IMAGE);

    for (uint32_t i = 0; i < s.drawCount; i++) {
        const Draw *d = &s.draws[i];
        if (d->count == 0)
            continue;
        g_gl.Uniform1i(u->screen, d->screen);
        g_gl.Uniform1i(u->mode, d->mode);
        g_gl.Uniform1i(u->colour, d->colour);
        g_gl.Uniform1i(u->remapRow, d->remap);
        if (d->mode == MODE_IMAGE || d->mode == MODE_OPAQUE) {
            const SceneTexture *t = scene_texture(d->texture);
            GLuint tex = texture_for(d->texture);
            if (t == NULL || tex == 0)
                continue;
            g_gl.BindTexture(GL_TEXTURE_2D, tex);
            g_gl.Uniform2i(u->imageSize, t->width, t->height);
        } else if (d->mode == MODE_STRIP) {
            g_gl.Uniform4i(u->strip, d->strip.siBase, d->strip.stride, d->strip.rowSub,
                           d->strip.rowCount);
            g_gl.Uniform1i(u->stripFirst, d->stripFirst);
        } else if (d->mode == MODE_DISC) {
            g_gl.Uniform3f(u->disc, d->disc[0], d->disc[1], d->disc[2]);
        }
        if (d->depth == DEPTH_NONE) {
            g_gl.Disable(GL_DEPTH_TEST);
        } else {
            g_gl.Enable(GL_DEPTH_TEST);
            g_gl.DepthMask(d->depth == DEPTH_WRITE ? GL_TRUE : GL_FALSE);
        }
        if (d->offset != 0) {
            g_gl.Enable(GL_POLYGON_OFFSET_FILL);
            g_gl.PolygonOffset(0, d->offset);
        } else {
            g_gl.Disable(GL_POLYGON_OFFSET_FILL);
        }
        g_gl.DrawArrays(d->primitive, (GLint)d->first, (GLsizei)d->count);
    }

    /* as glpresent.c expects the context */
    g_gl.Disable(GL_DEPTH_TEST);
    g_gl.Disable(GL_SCISSOR_TEST);
    g_gl.Disable(GL_POLYGON_OFFSET_FILL);
    g_gl.DepthMask(GL_TRUE);
    g_gl.BindVertexArray(0);
    g_gl.ActiveTexture(GL_TEXTURE0);
    g_gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

bool glscene_render_offscreen(const SceneFrame *f, const GlSceneInputs *in, int w, int h,
                              bool indices, uint8_t *rgba) {
    if (!s.open)
        return false;
    GLuint fbo = 0, target = 0, depth = 0;
    g_gl.ActiveTexture(GL_TEXTURE0);
    g_gl.GenTextures(1, &target);
    g_gl.BindTexture(GL_TEXTURE_2D, target);
    g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    g_gl.GenTextures(1, &depth);
    g_gl.BindTexture(GL_TEXTURE_2D, depth);
    g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT,
                    GL_UNSIGNED_INT, NULL);
    g_gl.GenFramebuffers(1, &fbo);
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, fbo);
    g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    g_gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
    bool ok = g_gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glscene_draw(f, in, 0, 0, w, h, indices);
        g_gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
        g_gl.ReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        g_gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
        /* GL's rows count from the bottom: the picture's first row last. */
        size_t pitch = (size_t)w * 4;
        uint8_t *row = malloc(pitch);
        for (int r = 0; row != NULL && r < h / 2; r++) {
            uint8_t *a = rgba + (size_t)r * pitch, *b = rgba + (size_t)(h - 1 - r) * pitch;
            memcpy(row, a, pitch);
            memcpy(a, b, pitch);
            memcpy(b, row, pitch);
        }
        ok = row != NULL;
        free(row);
    }
    g_gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    g_gl.DeleteFramebuffers(1, &fbo);
    g_gl.DeleteTextures(1, &target);
    g_gl.DeleteTextures(1, &depth);
    return ok;
}

/* ---- the check against the software's picture ------------------------------------------ */

static struct {
    bool on;
    const char *dump;
    unsigned long long frames, pixels, equal;
    double worst;
    uint32_t worstSerial;
    unsigned dumped;
} s_check;

static void check_report(void) {
    if (s_check.frames == 0) {
        fprintf(stderr, "scene: no frame drawn on the GPU\n");
        return;
    }
    fprintf(stderr,
            "scene: %llu frames drawn on the GPU at 1x: %.3f%% of %llu pixels the software's; "
            "the least alike, frame %u, %.3f%%\n",
            s_check.frames, 100.0 * (double)s_check.equal / (double)s_check.pixels,
            s_check.pixels, (unsigned)s_check.worstSerial, 100.0 * s_check.worst);
}

static void save(const char *what, uint32_t serial, int w, int h, const uint8_t *indices,
                 size_t step) {
    SDL_Surface *surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888);
    if (surface == NULL)
        return;
    for (int y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels + (size_t)y * surface->pitch);
        for (int x = 0; x < w; x++) {
            const uint8_t *c = g_vga.dac[indices[((size_t)y * (size_t)w + (size_t)x) * step] &
                                          g_vga.pelMask];
            row[x] = (uint32_t)(c[0] << 2 | c[0] >> 4) << 16 |
                     (uint32_t)(c[1] << 2 | c[1] >> 4) << 8 | (uint32_t)(c[2] << 2 | c[2] >> 4);
        }
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/%06u-%s.bmp", s_check.dump, (unsigned)serial, what);
    SDL_SaveBMP(surface, path);
    SDL_DestroySurface(surface);
}

static void check_frame(const SceneFrame *f) {
    int x0 = f->clip[0], y0 = f->clip[1], w = f->clip[2] - x0 + 1, h = f->clip[3] - y0 + 1;
    if (f->page == 0 || f->background || w <= 0 || h <= 0 || x0 < 0 || y0 < 0 || x0 + w > BAK_VGA_WIDTH ||
        y0 + h > BAK_VGA_HEIGHT)
        return;
    GlSceneInputs in = {g_vga.dac, g_vga.pelMask, scene_remap_tables(), bak_video_page(0xAC00)};
    uint8_t *rgba = malloc((size_t)w * (size_t)h * 4);
    if (rgba == NULL || !glscene_render_offscreen(f, &in, w, h, true, rgba)) {
        free(rgba);
        return;
    }
    const uint8_t *page = bak_video_page(f->page);
    unsigned long long equal = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            equal += page[(size_t)(y0 + y) * BAK_VGA_STRIDE + (size_t)(x0 + x)] ==
                     rgba[((size_t)y * (size_t)w + (size_t)x) * 4];
    double share = (double)equal / ((double)w * (double)h);
    s_check.frames++;
    s_check.pixels += (unsigned long long)w * (unsigned long long)h;
    s_check.equal += equal;
    if (s_check.frames == 1 || share < s_check.worst) {
        s_check.worst = share;
        s_check.worstSerial = f->serial;
    }
    if (s_check.dump != NULL && s_check.dumped < 64) {
        s_check.dumped++;
        uint8_t *soft = malloc((size_t)w * (size_t)h);
        if (soft != NULL) {
            for (int y = 0; y < h; y++)
                memcpy(soft + (size_t)y * (size_t)w,
                       page + (size_t)(y0 + y) * BAK_VGA_STRIDE + (size_t)x0, (size_t)w);
            save("software", f->serial, w, h, soft, 1);
            free(soft);
        }
        save("gpu", f->serial, w, h, rgba, 4);
        int k = 4;
        uint8_t *big = malloc((size_t)w * (size_t)h * 16 * 4);
        if (big != NULL && glscene_render_offscreen(f, &in, w * k, h * k, true, big))
            save("gpu4x", f->serial, w * k, h * k, big, 4);
        free(big);
    }
    free(rgba);
}

static void check_init(void) {
    const char *env = SDL_getenv("BAK_SCENE_CHECK");
    if (s_check.on || env == NULL || SDL_strcmp(env, "gpu") != 0)
        return;
    s_check.on = true;
    s_check.dump = SDL_getenv("BAK_SCENE_DUMP");
    scene_set_finished_hook(check_frame);
    atexit(check_report);
}
