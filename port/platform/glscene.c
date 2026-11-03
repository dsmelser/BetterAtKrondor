/*
 * The 3-D view on the GPU (M7's step 3; glscene.h). A frame of the software pass (scene.h) is
 * drawn again in its own order: the sky's and the ground's bands in screen space, then the
 * items. Faces, billboards and circles go through the game's own projection (its focal length
 * and the view's centre, the view rectangle scaled to the output) and a depth buffer: the
 * objects the software draws first by their priority (the ground's shapes) neither test nor
 * write depth, as everything drawn after them lies over them; the others do, and each face of
 * an object is pulled a step of depth nearer than the one drawn before it, so that the decals
 * the software paints over a face stay on top. Texture coordinates are interpolated with
 * perspective. A pixel's palette index comes from its face's colour or its image (through its
 * remap table, 0 transparent but on the sky); its colour is the DAC's for that index. Images are
 * filtered after that (video.textures): the four texels about a pixel are each looked up, then
 * blended, the blend narrowed to a window pixel where a texel covers more (sharp: crisp texels
 * with smooth edges), or not (smooth), and where a pixel spans several texels (a far wall) such
 * blends across it are averaged; or the nearest texel alone is taken. A transparent texel takes
 * no part, so that a sprite's edge stays where its texels end. The
 * ground's strips are drawn in colour from the strip texture laid on the ground in the world, so
 * that its grain moves with the world as the party walks and turns, over the shading the
 * software reads down the screen (ground() in the shader); as indices, for the check below, the
 * texture is read as vgablit.c's spans read it at the pixel's place in the 1x view (but for a
 * strip whose rows repeat, which vgablit.c starts at each polygon's first row: here they start
 * at the strip's own, so that its faces meet without a seam where their edges, drawn finer than
 * 1x, cut through its texels). Lines (combat's effects) are bands one 1x pixel wide
 * on screen, painted over what comes before them without depth. Whatever the software cut to a
 * clip rectangle narrower than the view (a sprite sinking into the ground) is cut to it here. A
 * dithered rectangle (the maps' backgrounds) is the even blend of its two colours, which is what
 * its checkerboard of 1x pixels stands for; drawn as indices it is the checkerboard. What the
 * frame does not draw stays transparent, so that a frame drawn over another (GLSCENE_OVER:
 * combat's actors over its background) leaves the one under it showing.
 *
 * With BAK_SCENE_CHECK=gpu, every finished view is drawn at 1x as palette indices, with the
 * frames its page shows under it (scene_frames_shown), and compared with the page the software
 * drew it into, over the pixels those frames own; the share of equal pixels goes to stderr at
 * exit. BAK_SCENE_DUMP=<folder> writes the pictures of the first 16 views and of the 16 least
 * alike of the others there at exit, in the palette of that moment: the software's, the GPU's
 * at 1x, and the GPU's at 4x.
 */
#include <float.h>
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

enum {
    MODE_COLOUR,
    MODE_IMAGE,
    MODE_OPAQUE,
    MODE_STRIP,
    MODE_RAMP,
    MODE_DISC,
    MODE_DITHER
};
enum { DEPTH_NONE, DEPTH_WRITE, DEPTH_TEST };
enum {
    UNIT_IMAGE,
    UNIT_REMAP,
    UNIT_GROUND,
    UNIT_PALETTE,
    UNIT_DETAIL,
    UNIT_MEANS
};

#define GROUND_W 320
#define GROUND_H 205    /* the scratch page's 65536 pixels, the last row padded */
#define GROUND_TEXEL 8  /* world units a texel of the strip texture covers as the ground's grain */
#define GROUND_PATCH 64 /* and as its patches */
#define GROUND_BIAS 128 /* the ground's detail is signed: kept halved, plus this */
#define RAMP_ROW 15     /* the remap texture's row after the 15 tables: the sky's shade ramp */

static const char s_vertexShader[] =
    "layout(location = 0) in vec3 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "uniform vec4 u_proj;  /* clip x = x * proj.x + depth * proj.y, clip y = z * proj.z + depth * "
    "proj.w */\n"
    "uniform vec2 u_depth; /* clip z = depth * u_depth.x + u_depth.y */\n"
    "uniform vec4 u_view;  /* the 1x view: left, top, width, height */\n"
    "uniform int u_screen; /* a_pos is a place in the 1x picture */\n"
    "uniform mat3 u_toWorld; /* camera space to the world's axes */\n"
    "uniform vec3 u_eye;     /* the camera in the world, less whole periods of the ground */\n"
    "out vec2 v_uv;\n"
    "out vec3 v_world; /* a face's point in the world, as u_eye places the camera */\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_world = u_eye + u_toWorld * a_pos;\n"
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
    "uniform vec4 u_view; /* the 1x view: left, top, width, height */\n"
    "uniform vec4 u_frag; /* the view's left and top in window pixels, 1x pixels per window "
    "pixel */\n"
    "uniform vec3 u_disc; /* a disc's centre and radius in the 1x picture */\n"
    "uniform ivec2 u_dither; /* a dithered rectangle's second colour and its left edge */\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "vec2 place() {\n"
    "    return vec2(u_view.x + (gl_FragCoord.x - u_frag.x) * u_frag.z,\n"
    "                u_view.y + (u_frag.y - gl_FragCoord.y) * u_frag.w);\n"
    "}\n"
    "int wrap(int a, int n) { /* a mod n for n > 0: GLSL leaves % of a negative undefined */\n"
    "    return a >= 0 ? a % n : n - 1 - (-a - 1) % n;\n"
    "}\n"
    "#ifndef INDICES\n"
    "uniform highp sampler2D u_detail; /* the strip texture's colours less their rows' means */\n"
    "uniform highp sampler2D u_means;  /* its rows' mean colours, a column of 256 */\n"
    "uniform mat3 u_toWorld;\n"
    "uniform vec3 u_eye;\n"
    "uniform vec4 u_cam;   /* the view's centre in the 1x picture, the focal length, the eye's "
    "height */\n"
    "uniform ivec2 u_band; /* the strip's band of the texture: its first row, its rows */\n"
    "uniform int u_screen;\n"
    "in vec3 v_world;\n"
    "/* The detail at a place in the strip texture (in texels), its derivatives given. */\n"
    "vec3 detail(vec2 at, vec2 dx, vec2 dy) {\n"
    "    vec2 size = GROUND_SIZE;\n"
    "    vec3 kept = textureGrad(u_detail, at / size, dx / size, dy / size).rgb;\n"
    "    return 2.0 * kept - GROUND_BIAS / 127.5;\n"
    "}\n"
    "/* The ground where a strip lies: the strip's band of the texture laid on the ground in the\n"
    " * world twice, as a grain of texels GROUND_TEXEL units square and as patches of texels\n"
    " * GROUND_PATCH units square, over each row's mean colour replaced by the one the software's\n"
    " * walk down the screen reads there (the band's shading); the detail fades into the mean as\n"
    " * its texels shrink. Each repetition of the band down the world starts its columns 117\n"
    " * further across, so that no column's average runs on as a line toward the horizon (the\n"
    " * ground is seen nearly edge-on), and the patches start a third of the way across, not to\n"
    " * line up with the grain. The ground's band (u_screen) lies where the pixel's ray meets\n"
    " * the world's level 0. */\n"
    "vec3 ground() {\n"
    "    vec2 at = place();\n"
    "    vec3 w = v_world;\n"
    "    bool beyond = false;\n"
    "    if (u_screen != 0) {\n"
    "        vec3 ray = vec3((at.x - u_cam.x) / u_cam.z, 1.0, (u_cam.y - at.y) / u_cam.z);\n"
    "        ray = u_toWorld * ray;\n"
    "        beyond = ray.z > -1e-6 || u_cam.w <= 0.0;\n"
    "        w = u_eye + ray * (u_cam.w / max(-ray.z, 1e-6));\n"
    "    }\n"
    "    vec2 t = w.xy / GROUND_TEXEL, tx = dFdx(t), ty = dFdy(t);\n"
    "    vec2 p = w.xy / GROUND_PATCH, px = dFdx(p), py = dFdy(p);\n"
    "    float first = float(u_band.x), rows = float(max(u_band.y, 1));\n"
    "    float tile = floor(t.y / rows), patchTile = floor(p.y / rows);\n"
    "    float row = first + t.y - tile * rows;\n"
    "    float g = first + (at.y - float(u_strip.z)) * float(u_strip.y) / 80.0;\n"
    "    g = u_strip.w != 0 ? row : clamp(g, first + 0.5, first + rows - 0.5);\n"
    "    vec3 colour = texture(u_means, vec2(0.5, g / 256.0)).rgb;\n"
    "    vec2 grainAt = vec2(t.x + mod(tile, 64.0) * 117.0, row);\n"
    "    vec2 patchAt = vec2(p.x + (mod(patchTile, 8.0) + 1.0) * 117.0, p.y - patchTile * rows);\n"
    "    if (!beyond)\n"
    "        colour += detail(grainAt, tx, ty) + 0.8 * detail(patchAt + vec2(0, first), px, py);\n"
    "    return colour;\n"
    "}\n"
    "uniform int u_filter; /* the images: TEXTURES_* (0 sharp, 1 smooth, 2 nearest) */\n"
    "/* An image's texel through its remap row and the palette, premultiplied by its coverage:\n"
    " * none where an image with transparent texels holds 0. */\n"
    "vec4 texel(ivec2 t) {\n"
    "    int index = int(texelFetch(u_image, clamp(t, ivec2(0), u_imageSize - 1), 0).r);\n"
    "    float a = u_mode == 1 && index == 0 ? 0.0 : 1.0;\n"
    "    if (u_remapRow >= 0)\n"
    "        index = int(texelFetch(u_remap, ivec2(index, u_remapRow), 0).r);\n"
    "    return vec4(texelFetch(u_palette, ivec2(index, 0), 0).rgb * a, a);\n"
    "}\n"
    "/* The four texels about a place in the image blended, the blend narrowed to a window pixel\n"
    " * where a texel covers more (sharp; width: texels a window pixel spans). */\n"
    "vec4 blend(vec2 at, vec2 width) {\n"
    "    vec2 p = at - 0.5, f = fract(p);\n"
    "    ivec2 k = ivec2(floor(p));\n"
    "    if (u_filter == 0)\n"
    "        f = clamp((f - 0.5) / clamp(width, 1e-4, 1.0) + 0.5, 0.0, 1.0);\n"
    "    return mix(mix(texel(k), texel(k + ivec2(1, 0)), f.x),\n"
    "               mix(texel(k + ivec2(0, 1)), texel(k + ivec2(1, 1)), f.x), f.y);\n"
    "}\n"
    "/* The image filtered after the palette lookup: a blend at the place; where the pixel spans\n"
    " * more than a texel, blends about a texel apart across the pixel's footprint, at most 4 by\n"
    " * 4, averaged, so that a far wall's texels do not shimmer. */\n"
    "vec4 image() {\n"
    "    vec2 dx = dFdx(v_uv), dy = dFdy(v_uv), width = fwidth(v_uv);\n"
    "    ivec2 n = ivec2(clamp(ceil(vec2(length(dx), length(dy))), 1.0, 4.0));\n"
    "    vec4 sum = vec4(0.0);\n"
    "    for (int j = 0; j < n.y; j++)\n"
    "        for (int i = 0; i < n.x; i++) {\n"
    "            vec2 at = (vec2(i, j) + 0.5) / vec2(n) - 0.5;\n"
    "            sum += blend(v_uv + dx * at.x + dy * at.y, width);\n"
    "        }\n"
    "    return sum / float(n.x * n.y);\n"
    "}\n"
    "#endif\n"
    "void main() {\n"
    "    int index = u_colour;\n"
    "    if (u_mode == 1 || u_mode == 2) {\n"
    "#ifndef INDICES\n"
    "        if (u_filter != 2) {\n"
    "            vec4 s = image();\n"
    "            if (s.a < 0.5) /* a sprite's edge where its texels end */\n"
    "                discard;\n"
    "            o_color = vec4(s.rgb / s.a, 1.0);\n"
    "            return;\n"
    "        }\n"
    "#endif\n"
    "        ivec2 t = clamp(ivec2(floor(v_uv)), ivec2(0), u_imageSize - 1);\n"
    "        index = int(texelFetch(u_image, t, 0).r);\n"
    "        if (u_mode == 1 && index == 0)\n"
    "            discard;\n"
    "        if (u_remapRow >= 0)\n"
    "            index = int(texelFetch(u_remap, ivec2(index, u_remapRow), 0).r);\n"
    "    } else if (u_mode == 3) {\n"
    "#ifdef INDICES\n"
    "        ivec2 p = ivec2(floor(place()));\n"
    "        int si = u_strip.w == 0 ? u_strip.x + (p.y - u_strip.z) * u_strip.y\n"
    "                                : u_strip.x + wrap(p.y - u_strip.z, u_strip.w) * u_strip.y;\n"
    "        int at = 4 * ((si + (p.x >> 2)) & 0xffff) + (p.x & 3);\n"
    "        index = at < 65536 ? int(texelFetch(u_ground, ivec2(at % 320, at / 320), 0).r) : 0;\n"
    "#else\n"
    "        o_color = vec4(ground(), 1.0);\n"
    "        return;\n"
    "#endif\n"
    "    } else if (u_mode == 4) {\n"
    "        int k = u_colour - int(floor(place().y)); /* rows up from the band's last */\n"
    "        index = int(texelFetch(u_remap, ivec2(clamp(k, 0, 255), 15), 0).r);\n"
    "    } else if (u_mode == 5) {\n"
    "        vec2 d = place() - (u_disc.xy + 0.5);\n"
    "        if (dot(d, d) > (u_disc.z + 0.5) * (u_disc.z + 0.5))\n"
    "            discard;\n"
    "    } else if (u_mode == 6) {\n"
    "#ifdef INDICES\n"
    "        ivec2 p = ivec2(floor(place()));\n"
    "        if (((p.x + p.y + u_dither.y) & 1) != 0)\n"
    "            index = u_dither.x;\n"
    "#else\n"
    "        o_color = mix(texelFetch(u_palette, ivec2(u_colour, 0), 0),\n"
    "                      texelFetch(u_palette, ivec2(u_dither.x, 0), 0), 0.5);\n"
    "        return;\n"
    "#endif\n"
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
    float disc[3];
    int dither, ditherX;
    bool clipped;    /* cut to clip, which is narrower than the view */
    int16_t clip[4]; /* left, top, right, bottom in the 1x picture */
} Draw;

typedef struct Uniforms {
    GLint proj, depth, view, screen, mode, colour, remapRow, imageSize, strip, frag, disc, dither;
    GLint toWorld, eye, cam, band, filter;
} Uniforms;

static struct {
    bool open;
    GLuint program[2]; /* colours, indices */
    Uniforms u[2];
    GLuint vao, vbo;
    GLuint remap, ground, palette;
    GLuint detail, means; /* the strip texture in colour (ground_colours) */
    uint32_t groundHash;
    bool groundKnown;
    uint32_t coloursPalette, coloursGround; /* what the colours were made from */
    bool coloursKnown;
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
        d = screen_rect(op->dither != 0 ? MODE_DITHER : MODE_COLOUR, op->x, op->y, op->w, op->h, 0,
                        0, 0, 0);
        if (d != NULL) {
            d->colour = op->colour;
            d->dither = op->dither;
            d->ditherX = op->x;
        }
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
        if (d != NULL)
            d->strip = op->strip;
        break;
    }
}

/* Image corner k's texels: top left, top right, bottom right, bottom left. */
static void image_corner(uint8_t k, const SceneTexture *t, float *u, float *v) {
    float w = t != NULL ? t->width : 1, h = t != NULL ? t->height : 1;
    *u = k == 1 || k == 2 ? w : 0;
    *v = k >= 2 ? h : 0;
}

/* A point of the 1x picture at a depth, back in camera space. */
static void unproject(const SceneFrame *f, float sx, float sy, float depth, float *x, float *z) {
    float focal = ldexpf(1.0f, f->screenShift);
    *x = (sx - (float)f->centerX) * depth / focal;
    *z = ((float)f->centerY - sy) * depth / focal;
}

/* A point of camera space on the 1x picture. */
static void project(const SceneFrame *f, SceneVertex p, float *sx, float *sy) {
    float focal = ldexpf(1.0f, f->screenShift);
    *sx = (float)f->centerX + p.x * focal / p.y;
    *sy = (float)f->centerY - p.z * focal / p.y;
}

static SceneVertex between(SceneVertex a, SceneVertex b, float t) {
    return (SceneVertex){a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

/* An item drawn within a clip rectangle narrower than the view is cut to it. */
static void clip_to_item(Draw *d, const SceneFrame *f, const SceneItem *it) {
    if (d == NULL || (it->clip[0] <= f->clip[0] && it->clip[1] <= f->clip[1] &&
                      it->clip[2] >= f->clip[2] && it->clip[3] >= f->clip[3]))
        return;
    d->clipped = true;
    memcpy(d->clip, it->clip, sizeof d->clip);
}

/* A band one 1x pixel wide from the middle of pixel (ax, ay) to that of (bx, by) and half a
 * pixel past each, in the 1x picture: the pixels draw_line sets, at any size. */
static void band(Draw *d, float ax, float ay, float bx, float by) {
    ax += 0.5f;
    ay += 0.5f;
    bx += 0.5f;
    by += 0.5f;
    float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) {
        dx = 1;
        dy = 0;
        len = 1;
    }
    dx *= 0.5f / len; /* half a pixel along the line; (-dy, dx) across it */
    dy *= 0.5f / len;
    float p[4][2] = {{ax - dx - dy, ay - dy + dx},
                     {ax - dx + dy, ay - dy - dx},
                     {bx + dx + dy, by + dy - dx},
                     {bx + dx - dy, by + dy + dx}};
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int k = 0; k < 6; k++)
        vertex(d, p[order[k]][0], p[order[k]][1], 0, 0, 0);
}

/* A line on screen, painted over what is drawn before it and under what is drawn after. */
static void line(const SceneFrame *f, const SceneItem *it) {
    Draw *d = new_draw(GL_TRIANGLES, MODE_COLOUR);
    if (d == NULL)
        return;
    d->screen = 1;
    d->colour = it->fill;
    clip_to_item(d, f, it);
    band(d, it->rect[0], it->rect[1], it->rect[2], it->rect[3]);
}

/*
 * draw_polygon's outline: each edge a band one 1x pixel wide across it on screen and half a
 * pixel past its ends (so the corners meet), each end at its own depth, cut at the view plane:
 * the weight of the original's lines at any size, where GL's are one window pixel.
 */
static void outline(const SceneFrame *f, const SceneItem *it, Draw *d) {
    const SceneVertex *v = &f->vertices[it->firstVertex];
    float focal = ldexpf(1.0f, f->screenShift);
    for (unsigned i = 0; i < it->corners; i++) {
        SceneVertex a = v[i], b = v[(i + 1) % it->corners];
        if (a.y < focal && b.y < focal)
            continue;
        if (a.y < focal)
            a = between(a, b, (focal - a.y) / (b.y - a.y));
        else if (b.y < focal)
            b = between(b, a, (focal - b.y) / (a.y - b.y));
        float ax, ay, bx, by;
        project(f, a, &ax, &ay);
        project(f, b, &bx, &by);
        float dx = bx - ax, dy = by - ay, len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-4f) {
            dx = 1;
            dy = 0;
            len = 1;
        }
        dx *= 0.5f / len; /* half a pixel along the edge; (-dy, dx) across it */
        dy *= 0.5f / len;
        float p[4][3] = {{ax - dx - dy, ay - dy + dx, a.y},
                         {ax - dx + dy, ay - dy - dx, a.y},
                         {bx + dx + dy, by + dy - dx, b.y},
                         {bx + dx - dy, by + dy + dx, b.y}};
        SceneVertex q[4];
        for (int k = 0; k < 4; k++) {
            q[k].y = p[k][2];
            unproject(f, p[k][0], p[k][1], p[k][2], &q[k].x, &q[k].z);
        }
        static const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int k = 0; k < 6; k++)
            vertex(d, q[order[k]].x, q[order[k]].y, q[order[k]].z, 0, 0);
    }
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
        clip_to_item(d, f, it);
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
    /* draw_polygon: an outline in its own colour after the fill, or alone without one; over
     * its fill, which is pulled as near */
    if (!it->textured && !it->striped && (!it->filled || it->outline != it->fill)) {
        Draw *d = new_draw(GL_TRIANGLES, MODE_COLOUR);
        if (d == NULL)
            return;
        d->depth = depth == DEPTH_NONE ? DEPTH_NONE : DEPTH_TEST;
        d->offset = offset - 1.0f;
        d->colour = it->outline;
        clip_to_item(d, f, it);
        outline(f, it, d);
    }
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
    clip_to_item(d, f, it);
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
    clip_to_item(d, f, it);
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
        case SCENE_LINE:
            line(f, it);
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
    u->frag = g_gl.GetUniformLocation(p, "u_frag");
    u->disc = g_gl.GetUniformLocation(p, "u_disc");
    u->dither = g_gl.GetUniformLocation(p, "u_dither");
    u->toWorld = g_gl.GetUniformLocation(p, "u_toWorld");
    u->eye = g_gl.GetUniformLocation(p, "u_eye");
    u->cam = g_gl.GetUniformLocation(p, "u_cam");
    u->band = g_gl.GetUniformLocation(p, "u_band");
    u->filter = g_gl.GetUniformLocation(p, "u_filter");
    g_gl.UseProgram(p);
    g_gl.Uniform1i(g_gl.GetUniformLocation(p, "u_image"), UNIT_IMAGE);
    g_gl.Uniform1i(g_gl.GetUniformLocation(p, "u_remap"), UNIT_REMAP);
    g_gl.Uniform1i(g_gl.GetUniformLocation(p, "u_ground"), UNIT_GROUND);
    g_gl.Uniform1i(g_gl.GetUniformLocation(p, "u_palette"), UNIT_PALETTE);
    g_gl.Uniform1i(g_gl.GetUniformLocation(p, "u_detail"), UNIT_DETAIL);
    g_gl.Uniform1i(g_gl.GetUniformLocation(p, "u_means"), UNIT_MEANS);
    return true;
}

static void check_init(void);

/* The fragment shader with the ground's sizes defined, and INDICES if asked. */
static void fragment_source(char *to, size_t size, bool indices) {
    snprintf(to, size,
             "%s#define GROUND_SIZE vec2(%d.0, %d.0)\n#define GROUND_TEXEL %d.0\n"
             "#define GROUND_PATCH %d.0\n#define GROUND_BIAS %d.0\n%s",
             indices ? "#define INDICES 1\n" : "", GROUND_W, GROUND_H, GROUND_TEXEL, GROUND_PATCH,
             GROUND_BIAS, s_fragmentShader);
}

bool glscene_open(void) {
    if (s.open)
        return true;
    static char colours[sizeof s_fragmentShader + 256], indices[sizeof s_fragmentShader + 256];
    fragment_source(colours, sizeof colours, false);
    fragment_source(indices, sizeof indices, true);
    if (!program(0, colours) || !program(1, indices)) {
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
    s.means = new_texture(GL_TEXTURE0 + UNIT_MEANS);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    s.detail = new_texture(GL_TEXTURE0 + UNIT_DETAIL);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    /* the ground is seen nearly edge-on: its texels shrink far more down the screen than across */
    if (SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic") ||
        SDL_GL_ExtensionSupported("GL_ARB_texture_filter_anisotropic")) {
        GLfloat most = 1.0f;
        g_gl.GetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &most);
        g_gl.TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, SDL_min(most, 16.0f));
    }
    g_gl.ActiveTexture(GL_TEXTURE0);
    s.open = true;
    s.groundKnown = false;
    s.coloursKnown = false;
    check_init();
    return true;
}

void glscene_close(void) {
    if (g_gl.DeleteProgram != NULL) {
        g_gl.DeleteProgram(s.program[0]);
        g_gl.DeleteProgram(s.program[1]);
        g_gl.DeleteVertexArrays(1, &s.vao);
        g_gl.DeleteBuffers(1, &s.vbo);
        GLuint t[5] = {s.remap, s.ground, s.palette, s.detail, s.means};
        g_gl.DeleteTextures(5, t);
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

/* The scissor box of a draw drawn into x, y, w, h (the frame's view there): the view, or the
 * draw's clip rectangle within it. */
static void scissor(const Draw *d, const SceneFrame *f, int x, int y, int w, int h) {
    if (!d->clipped) {
        g_gl.Scissor(x, y, w, h);
        return;
    }
    float sx = (float)w / (float)(f->clip[2] - f->clip[0] + 1);
    float sy = (float)h / (float)(f->clip[3] - f->clip[1] + 1);
    int left = x + (int)SDL_roundf((float)(d->clip[0] - f->clip[0]) * sx);
    int right = x + (int)SDL_roundf((float)(d->clip[2] + 1 - f->clip[0]) * sx);
    int top = y + h - (int)SDL_roundf((float)(d->clip[1] - f->clip[1]) * sy); /* GL's y is up */
    int bottom = y + h - (int)SDL_roundf((float)(d->clip[3] + 1 - f->clip[1]) * sy);
    left = SDL_max(left, x);
    right = SDL_min(right, x + w);
    bottom = SDL_max(bottom, y);
    top = SDL_min(top, y + h);
    g_gl.Scissor(left, bottom, SDL_max(right - left, 0), SDL_max(top - bottom, 0));
}

static uint8_t s_strips[GROUND_W * GROUND_H]; /* the strip texture last uploaded, padded */

/* The strip texture in the palette's colours, for the ground laid in the world (ground() in the
 * shader): each row's mean colour (a column of 256), and each texel less its row's mean, halved
 * (a bright speck on a dark row keeps its contrast) and plus GROUND_BIAS, mipmapped: the grain
 * averages away into the means as it recedes. */
static void ground_colours(const uint8_t *rgba) {
    static uint8_t means[256 * 4];
    static uint8_t detail[GROUND_W * GROUND_H * 4];
    memset(means, 0, sizeof means);
    for (int r = 0; r < GROUND_H; r++) {
        const uint8_t *row = s_strips + r * GROUND_W;
        unsigned sum[3] = {0, 0, 0};
        for (int x = 0; x < GROUND_W; x++)
            for (int k = 0; k < 3; k++)
                sum[k] += rgba[row[x] * 4 + k];
        uint8_t *m = &means[r * 4];
        for (int k = 0; k < 3; k++)
            m[k] = (uint8_t)((sum[k] + GROUND_W / 2) / GROUND_W);
        m[3] = 0xff;
        for (int x = 0; x < GROUND_W; x++) {
            uint8_t *d = &detail[((size_t)r * GROUND_W + (size_t)x) * 4];
            for (int k = 0; k < 3; k++)
                d[k] = (uint8_t)((rgba[row[x] * 4 + k] - m[k] + 2 * GROUND_BIAS) >> 1);
            d[3] = 0xff;
        }
    }
    g_gl.ActiveTexture(GL_TEXTURE0 + UNIT_MEANS);
    g_gl.BindTexture(GL_TEXTURE_2D, s.means);
    g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, means);
    g_gl.ActiveTexture(GL_TEXTURE0 + UNIT_DETAIL);
    g_gl.BindTexture(GL_TEXTURE_2D, s.detail);
    g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GROUND_W, GROUND_H, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    detail);
    g_gl.GenerateMipmap(GL_TEXTURE_2D);
}

/* The palette, the remap tables and the ground's strip texture (the frame's own copy, or the
 * page's), and the strip texture in colour if the frame draws strips in colour. */
static void upload(const GlSceneInputs *in, const uint8_t *strips, bool colours) {
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
    if (strips != NULL) {
        uint32_t hash = platform_hash(0, strips, 65536);
        if (!s.groundKnown || hash != s.groundHash) {
            memcpy(s_strips, strips, 65536);
            g_gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, GROUND_W, GROUND_H, 0, GL_RED_INTEGER,
                            GL_UNSIGNED_BYTE, s_strips);
            s.groundHash = hash;
            s.groundKnown = true;
        }
    }
    if (colours && s.groundKnown) {
        uint32_t palette = platform_hash(0, rgba, sizeof rgba);
        if (!s.coloursKnown || palette != s.coloursPalette || s.groundHash != s.coloursGround) {
            ground_colours(rgba);
            s.coloursPalette = palette;
            s.coloursGround = s.groundHash;
            s.coloursKnown = true;
        }
    }
}

void glscene_draw(const SceneFrame *f, const GlSceneInputs *in, int x, int y, int w, int h,
                  unsigned flags) {
    bool indices = (flags & GLSCENE_INDICES) != 0;
    if (!s.open || w <= 0 || h <= 0)
        return;
    build(f);
    bool strips = false;
    for (uint32_t i = 0; i < s.drawCount && !strips; i++)
        strips = s.draws[i].mode == MODE_STRIP;
    g_gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    upload(in, f->hasGround ? f->ground : in->ground, strips && !indices);

    float viewW = (float)(f->clip[2] - f->clip[0] + 1), viewH = (float)(f->clip[3] - f->clip[1] + 1);
    if (viewW <= 0 || viewH <= 0)
        return;
    /* The depth range: the near plane the software's (the focal length), or as far as half the
     * nearest depth drawn, so that a view from far away (the maps, 100,000 units up) keeps its
     * depth precision; the far one past everything. */
    float focal = ldexpf(1.0f, f->screenShift), nearest = FLT_MAX, farZ = 65536.0f;
    for (uint32_t i = 0; i < f->vertexCount; i++)
        farZ = SDL_max(farZ, 2.0f * f->vertices[i].y);
    for (uint32_t i = 0; i < s.drawCount; i++)
        for (uint32_t k = 0; !s.draws[i].screen && k < s.draws[i].count; k++)
            nearest = SDL_min(nearest, s.vertices[s.draws[i].first + k].y);
    float nearZ = nearest < FLT_MAX ? SDL_max(focal, 0.5f * nearest) : focal;
    const Uniforms *u = &s.u[indices];
    g_gl.UseProgram(s.program[indices]);
    g_gl.Uniform4f(u->proj, 2.0f * focal / viewW, 2.0f * ((float)f->centerX - f->clip[0]) / viewW - 1.0f,
                   2.0f * focal / viewH, 1.0f - 2.0f * ((float)f->centerY - f->clip[1]) / viewH);
    g_gl.Uniform2f(u->depth, (farZ + nearZ) / (farZ - nearZ), -2.0f * farZ * nearZ / (farZ - nearZ));
    g_gl.Uniform4f(u->view, f->clip[0], f->clip[1], viewW, viewH);
    g_gl.Uniform4f(u->frag, (float)x, (float)(y + h), viewW / (float)w, viewH / (float)h);
    float toWorld[9]; /* column by column: the view matrix turned back */
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            toWorld[3 * i + j] = (float)f->view[3 * j + i] / 16384.0f;
    g_gl.UniformMatrix3fv(u->toWorld, 1, GL_FALSE, toWorld);
    g_gl.Uniform4f(u->cam, f->centerX, f->centerY, focal, (float)f->camPos[2]);
    g_gl.Uniform1i(u->filter, settings_get_choice(SETTING_VIDEO_TEXTURES));

    g_gl.BindVertexArray(s.vao);
    g_gl.BindBuffer(GL_ARRAY_BUFFER, s.vbo);
    g_gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(s.vertexCount * sizeof(Vertex)), s.vertices,
                    GL_STREAM_DRAW);
    g_gl.Viewport(x, y, w, h);
    g_gl.Scissor(x, y, w, h);
    g_gl.Enable(GL_SCISSOR_TEST);
    g_gl.ClearColor(0, 0, 0, 0);
    g_gl.DepthMask(GL_TRUE);
    g_gl.Clear((flags & GLSCENE_OVER) ? GL_DEPTH_BUFFER_BIT
                                      : GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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
            int rows = SDL_max(d->strip.rowCount != 0 ? d->strip.rowCount : d->strip.bandRows, 1);
            g_gl.Uniform2i(u->band, d->strip.bandRow, rows);
            /* the camera less whole periods of the ground's texture (its patches' across, eight
             * repetitions of them down, which its shifts repeat with): small, and the same to it */
            g_gl.Uniform3f(u->eye, (float)(f->camPos[0] % (GROUND_W * GROUND_PATCH)),
                           (float)(f->camPos[1] % (rows * GROUND_PATCH * 8)), (float)f->camPos[2]);
        } else if (d->mode == MODE_DISC) {
            g_gl.Uniform3f(u->disc, d->disc[0], d->disc[1], d->disc[2]);
        } else if (d->mode == MODE_DITHER) {
            g_gl.Uniform2i(u->dither, d->dither, d->ditherX);
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
        scissor(d, f, x, y, w, h);
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

bool glscene_render_frames_offscreen(const SceneFrame *const *frames, const GlSceneInputs *in,
                                     int w, int h, bool indices, uint8_t *rgba) {
    if (!s.open || frames[0] == NULL)
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
        for (const SceneFrame *const *at = frames; *at != NULL; at++)
            glscene_draw(*at, in, 0, 0, w, h,
                         (indices ? GLSCENE_INDICES : 0u) | (at != frames ? GLSCENE_OVER : 0u));
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

bool glscene_render_offscreen(const SceneFrame *f, const GlSceneInputs *in, int w, int h,
                              bool indices, uint8_t *rgba) {
    const SceneFrame *frames[2] = {f, NULL};
    return glscene_render_frames_offscreen(frames, in, w, h, indices, rgba);
}

/* ---- the check against the software's picture ------------------------------------------ */

/* A view's pictures as palette indices: the software's and the GPU's at 1x, the GPU's at 4x. */
typedef struct Dump {
    uint32_t serial;
    double share;
    int w, h;
    uint8_t *software, *gpu, *gpu4x;
} Dump;

#define DUMP_FIRST 16 /* the first views dumped */
#define DUMP_WORST 16 /* and the least alike of the others */

static struct {
    bool on;
    char *dump; /* the folder (a copy: SDL frees its environment before the end) */
    unsigned long long frames, pixels, equal;
    double worst;
    uint32_t worstSerial;
    Dump dumps[DUMP_FIRST + DUMP_WORST]; /* written at the end, in the palette of that moment:
                                            a view is often drawn while the palette is faded
                                            out (a map's first, combat's background) */
} s_check;

static void dump_free(Dump *d) {
    free(d->software);
    free(d->gpu);
    free(d->gpu4x);
    memset(d, 0, sizeof *d);
}

static void write_bmp(const Dump *d, const char *what, int w, int h, const uint8_t *indices) {
    SDL_Surface *surface =
        indices != NULL ? SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888) : NULL;
    for (int y = 0; surface != NULL && y < h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)surface->pixels + (size_t)y * surface->pitch);
        for (int x = 0; x < w; x++) {
            const uint8_t *c =
                g_vga.dac[indices[(size_t)y * (size_t)w + (size_t)x] & g_vga.pelMask];
            row[x] = (uint32_t)(c[0] << 2 | c[0] >> 4) << 16 |
                     (uint32_t)(c[1] << 2 | c[1] >> 4) << 8 | (uint32_t)(c[2] << 2 | c[2] >> 4);
        }
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/%06u-%s.bmp", s_check.dump, (unsigned)d->serial, what);
    if (surface == NULL || !SDL_SaveBMP(surface, path))
        fprintf(stderr, "scene: %s not written: %s\n", path, SDL_GetError());
    SDL_DestroySurface(surface);
}

static void check_report(void) {
    for (unsigned i = 0; i < SDL_arraysize(s_check.dumps); i++) {
        Dump *d = &s_check.dumps[i];
        if (d->serial == 0)
            continue;
        write_bmp(d, "software", d->w, d->h, d->software);
        write_bmp(d, "gpu", d->w, d->h, d->gpu);
        write_bmp(d, "gpu4x", d->w * 4, d->h * 4, d->gpu4x);
        dump_free(d);
    }
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

/* Where a view's pictures go, if they are kept: the first ones, then the least alike. */
static Dump *dump_slot(double share) {
    Dump *d = s_check.dumps, *most = NULL;
    for (unsigned i = 0; i < SDL_arraysize(s_check.dumps); i++) {
        if (d[i].serial == 0)
            return &d[i];
        if (i >= DUMP_FIRST && (most == NULL || d[i].share > most->share))
            most = &d[i];
    }
    if (most == NULL || most->share <= share)
        return NULL;
    dump_free(most);
    return most;
}

/* The pixels of a picture (every step-th byte), as a block of their own. */
static uint8_t *indices_of(const uint8_t *from, int w, int h, size_t step, size_t stride) {
    uint8_t *to = malloc((size_t)w * (size_t)h);
    for (int y = 0; to != NULL && y < h; y++)
        for (int x = 0; x < w; x++)
            to[(size_t)y * (size_t)w + (size_t)x] = from[(size_t)y * stride + (size_t)x * step];
    return to;
}

/* A view as its page shows it: the frames its pixels' tags name (combat's actors over the
 * background under them), compared over the pixels they own. */
static void check_frame(const SceneFrame *f) {
    int x0 = f->clip[0], y0 = f->clip[1], w = f->clip[2] - x0 + 1, h = f->clip[3] - y0 + 1;
    if (f->page == 0 || f->tag == 0 || w <= 0 || h <= 0 || x0 < 0 || y0 < 0 ||
        x0 + w > BAK_VGA_WIDTH || y0 + h > BAK_VGA_HEIGHT)
        return;
    uint8_t *page = bak_video_page(f->page);
    const uint8_t *tags = bak_vram_tags(page);
    static unsigned counts[256];
    memset(counts, 0, sizeof counts);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            counts[tags[(size_t)(y0 + y) * BAK_VGA_STRIDE + (size_t)(x0 + x)]]++;
    counts[0] = 0;
    const SceneFrame *frames[SCENE_SHOWN_MAX + 1];
    unsigned n = scene_frames_shown(counts, frames);
    if (n == 0 || frames[n - 1] != f)
        return;
    bool owned[256] = {false};
    for (unsigned i = 0; i < n; i++)
        owned[frames[i]->tag] = true;
    GlSceneInputs in = {g_vga.dac, g_vga.pelMask, scene_remap_tables(), bak_video_page(0xAC00)};
    uint8_t *rgba = malloc((size_t)w * (size_t)h * 4);
    if (rgba == NULL || !glscene_render_frames_offscreen(frames, &in, w, h, true, rgba)) {
        free(rgba);
        return;
    }
    unsigned long long equal = 0, pixels = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            size_t at = (size_t)(y0 + y) * BAK_VGA_STRIDE + (size_t)(x0 + x);
            if (!owned[tags[at]])
                continue;
            pixels++;
            equal += page[at] == rgba[((size_t)y * (size_t)w + (size_t)x) * 4];
        }
    }
    double share = pixels != 0 ? (double)equal / (double)pixels : 1.0;
    if (pixels != 0) {
        s_check.frames++;
        s_check.pixels += pixels;
        s_check.equal += equal;
        if (s_check.frames == 1 || share < s_check.worst) {
            s_check.worst = share;
            s_check.worstSerial = f->serial;
        }
    }
    Dump *d = pixels != 0 && s_check.dump != NULL ? dump_slot(share) : NULL;
    if (d != NULL) {
        uint8_t *big = malloc((size_t)w * (size_t)h * 16 * 4);
        *d = (Dump){
            f->serial,
            share,
            w,
            h,
            indices_of(page + (size_t)y0 * BAK_VGA_STRIDE + (size_t)x0, w, h, 1, BAK_VGA_STRIDE),
            indices_of(rgba, w, h, 4, (size_t)w * 4),
            NULL};
        if (big != NULL && glscene_render_frames_offscreen(frames, &in, w * 4, h * 4, true, big))
            d->gpu4x = indices_of(big, w * 4, h * 4, 4, (size_t)w * 16);
        free(big);
    }
    free(rgba);
}

static void check_init(void) {
    const char *env = SDL_getenv("BAK_SCENE_CHECK");
    if (s_check.on || env == NULL || SDL_strcmp(env, "gpu") != 0)
        return;
    s_check.on = true;
    const char *dump = SDL_getenv("BAK_SCENE_DUMP");
    s_check.dump = dump != NULL ? SDL_strdup(dump) : NULL;
    scene_set_finished_hook(check_frame);
    atexit(check_report);
}
