#ifndef BAK_WORLDLP_H
#define BAK_WORLDLP_H

#include "structs.h"

extern MenuPage *g_pReqMainPage;
extern s16 g_nSceneReloadPending;
extern s16 g_nHotspotActivateRequest;

extern void worldloop_req_main_screen_load(void);
extern void worldloop_req_main_screen_unload(void);
extern s16 world3d_main_loop(void);
extern void worldloop_camera_snap_reset(s32 *pSavedZ, s16 *pSavedYaw);
extern void worldloop_camera_set_pos_z_yaw(s32 pos_z, s16 yaw);
extern void worldloop_party_move_done_clr(void);
extern void worldloop_step_or_final_movement(void);
extern void worldloop_set_flag_8b_preds(void);
extern void worldloop_pty_stat7_flag_cd(void);

#endif
