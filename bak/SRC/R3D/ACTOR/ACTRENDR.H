#ifndef BAK_ACTRENDR_H
#define BAK_ACTRENDR_H

#include "structs.h"

extern char actorrender_frustum_cull(void);
extern s16 actorrender_mesh_parts_cull_sort(s16 part_off, s16 part_count, s16 *parts);
extern s16 actorrender_mesh_part_ptr_arr(s16 extra, s16 count, s16 base, s16 *output);
extern void actorrender_mesh_parts(s16 part_off, s16 part_count, s16 *part_ptr_array);
extern void actorrender_entity(WorldObject far *entry);

#endif
