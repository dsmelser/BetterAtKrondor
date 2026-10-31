#ifndef BAK_DISTDIR_H
#define BAK_DISTDIR_H

#include "structs.h"

extern u32 distdir_vec3_distance_dir_approx(s32 *a, u32 b, unsigned short dir_in);
extern s32 distdir_octagonal_distance(s32 *p1, s32 *p2);
extern s32 distdir_octagonal_distance_dxdy(s32 dx, s32 dy);

#endif
