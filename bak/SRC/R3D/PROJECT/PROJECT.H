#ifndef BAK_PROJECT_H
#define BAK_PROJECT_H

#include "structs.h"

extern void project_perspective_xy(s16 x, s16 depth, s16 y, s16 *out_vec3);
extern void project_world_to_screen(s16 x, s16 y, s16 z, s16 *out_screen_xy, ViewContext *widget);
extern void project_tile_to_screen(s16 tile_x, s16 tile_y, s16 *out_screen);
extern void project_cursor_to_world_tile(s32 *out_world_xyz);

#endif
