#ifndef BAK_MAP_H
#define BAK_MAP_H

#include "structs.h"

extern u16 g_full_redraw_needed;
extern s16 g_nMapReloadPending;
extern unsigned char *g_pColorRemap;
extern u16 g_wExploreCameraYawSaved;
extern s32 g_lWorldZMin;
extern s32 g_lWorldZMax;
extern s32 g_lWorldZStep;
extern s32 g_lExploreCameraZSaved;
extern MenuPage *g_pReqMapPage;

extern void map_req_map_screen_load(void);
extern void map_req_map_screen_free(void);
extern void map_color_remap_load(char *filename);
extern void map_color_remap_free(void);
extern void map_main_loop(void);
extern void map_camera_snap_face_south(s32 *pSavedZ, s16 *pSavedYaw);
extern void map_camera_restore_z_yaw(s32 new_pos_z, s16 new_yaw);
extern void map_animate_camera_to_tile(TileMoveRecord *ptr);

#endif
