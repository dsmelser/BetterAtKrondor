#ifndef BAK_WORLDRND_H
#define BAK_WORLDRND_H

#include "structs.h"

extern u16 g_spriteBankCount;
extern PagedArrayEntry *g_spriteBanks;
extern ImageRecord *g_pBillboardSpriteImg;
extern s16 g_nBillboardScrX;
extern s16 g_nBillboardScrY;
extern s16 g_nBillboardW;
extern s16 g_nBillboardH;
extern u16 g_nFogRemapTableCount;
extern u16 g_wSpriteFogBucketWidth;
extern u32 g_lSpriteFogOnsetDist;
extern s32 g_lSpriteFogFarDist;
extern u16 g_nLodBucketWidth;
extern u32 g_dwLodNearThreshold;
extern u32 g_lFarCullDist;

extern ImageRecord **worldrender_table_swap(s16 slot, ImageRecord **new_table_ptr);
extern void worldrender_swap_record_table(s16 slot_a, s16 slot_b);
extern ImageRecord **worldrender_table_load(s16 slot, char *fname);
extern void worldrender_table_unload(s16 slot);
extern ImageRecord *worldrender_paged_array_get(s16 index);
extern s16 *worldrender_mesh_vertex_to_world(unsigned char idx, s16 *out_vec3);
extern void worldrender_sprite_billboard(unsigned char far *entry);
extern void worldrender_textured_quad_lod(s16 texture_index, s16 *xs, s16 *ys);
extern void worldrender_world_point_as_circ(CircObj far *obj);

#endif
