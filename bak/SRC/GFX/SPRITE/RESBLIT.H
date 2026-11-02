#ifndef BAK_RESBLIT_H
#define BAK_RESBLIT_H

#include "structs.h"

extern s16 g_nDialogScxEmsHandle;
extern s16 g_nInventorScxEmsHandle;
extern s16 g_nFrameScxEmsHandle;
extern ImageRecord **g_pHeadsBmxAssetTable;

extern void resblit_alloc_ems_64k(char *path, s16 *out_handle);
extern void resblit_load_resource_set_1ec(s16 flags);
extern ImageRecord **resblit_load_asset_table(char *path, s16 storage_mode);
extern void resblit_list_remap_palette(ImageRecord **resource_list, unsigned char *palette_lut);
extern void resblit_sprite(ImageRecord *sprite, s16 dst_x, s16 dst_y);
extern void resblit_sprite_frame(ImageRecord *sprite, s16 dst_x, s16 dst_y, s16 frame);
extern void resblit_hud_draw_party_icons_row(char *filename);
extern void resblit_load_pal_or_stream(char *filename);

#endif
