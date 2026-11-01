#ifndef BAK_POLYFILL_H
#define BAK_POLYFILL_H

#include "structs.h"

extern const unsigned char *g_pTexturedSpanLut;

extern void draw_polygon_textured(s16 vertex_count, s16 *verts_x, s16 *verts_y,
                                  const unsigned char *lut);

#endif
