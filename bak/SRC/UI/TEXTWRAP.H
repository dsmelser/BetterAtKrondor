#ifndef BAK_TEXTWRAP_H
#define BAK_TEXTWRAP_H

#include "structs.h"

extern s16 textwrap_compute_lines(s32 text, s16 max_width, s16 *out_lines, s16 max_lines);
extern void textwrap_draw_aligned(s32 text, s16 x, s16 y, s16 max_width, s16 max_height,
                                  s16 line_spacing, u16 flags, s16 first_line);

#endif
