#ifndef BAK_TOWNSCN_H
#define BAK_TOWNSCN_H

#include "structs.h"

extern TownScene *g_pCurrentTownScene;
extern MenuPage *g_pTownMenuPage;
extern s16 g_nTownSceneAnimHandle;
extern u16 g_dialog_in_scene;
extern u16 g_dialog_anim_channel;

extern void townscene_main_loop(u16 scene_kind, s16 scene_index);
extern void townscene_anim_channel_play_sync(s16 channel_id);
extern void townscene_cheat_menu_screen(void);
extern void townscene_chest_open_with_cipher(void);

#endif
