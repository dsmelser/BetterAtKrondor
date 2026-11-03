/*
 * glscene.h - the 3-D view on the GPU (M7): a frame the software pass drew (scene.h) drawn
 * again through OpenGL at the output's resolution, with a depth buffer and perspective-correct
 * textures, its colours from the live palette.
 */
#ifndef BAK_GLSCENE_H
#define BAK_GLSCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "scene.h"

/* What a frame is drawn with besides itself: the DAC (6-bit components) and its pixel mask,
 * the remap tables the blits use (POLYRAST's, 15 of 256), and the ground's strip texture (the
 * scratch page at AC00h, 65536 chunky pixels). */
typedef struct GlSceneInputs {
    const uint8_t (*dac)[3];
    uint8_t pelMask;
    const uint8_t *remapTables;
    const uint8_t *ground;
} GlSceneInputs;

/* In the current OpenGL 3.3 context (glpresent_open's); false, having said why, if it cannot. */
bool glscene_open(void);
void glscene_close(void);
/* The frame's view (its clip rectangle) into the framebuffer bound, at x, y, w, h of its pixels
 * (GL's: y from the bottom). With indices the palette index of each pixel comes out as red /
 * 255 instead of its colour. */
void glscene_draw(const SceneFrame *frame, const GlSceneInputs *in, int x, int y, int w, int h,
                  bool indices);
/* The same off-screen at w x h, read back as RGBA, the top row first. */
bool glscene_render_offscreen(const SceneFrame *frame, const GlSceneInputs *in, int w, int h,
                              bool indices, uint8_t *rgba);

#endif
