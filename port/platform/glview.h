/*
 * glview.h - the 3-D view in the window (M7's steps 4 and 5): the GPU's drawing of the frames a
 * picture shows (glscene.c) over the 1x picture, where its pixels carry their tags (bak_video.h).
 */
#ifndef BAK_GLVIEW_H
#define BAK_GLVIEW_H

#include <stdbool.h>
#include <stdint.h>

struct SDL_FRect;

/* In the current OpenGL context, after glscene_open; false, having said why, if it cannot. */
bool glview_open(void);
void glview_close(void);
/* Over the w x h picture just drawn at r of an outW x outH output: the frames its pixels' tags
 * (w x h, as the picture) show (scene_frames_shown), drawn where they name them. False when
 * nothing was drawn. */
bool glview_draw(const uint8_t *tags, int w, int h, const struct SDL_FRect *r, int outW, int outH,
                 const uint8_t (*dac)[3], uint8_t pelMask);

#endif
