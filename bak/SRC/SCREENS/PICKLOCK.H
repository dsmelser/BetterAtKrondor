#ifndef BAK_PICKLOCK_H
#define BAK_PICKLOCK_H

#include "structs.h"
#include "SRC/GAME/ACTOR/ACTOR.H"

extern unsigned char g_bInventoryShopMode;
extern unsigned char g_abInvQuizAnswerTable[13];
extern unsigned char g_bPicklockSuccess;
extern short g_nPicklockMode;
extern Actor far *g_pPicklockWorkingInv;
extern Actor far *g_pPicklockOwnerActor;
extern short g_nPicklockScore;
extern short g_nPicklockDiffTier;

extern s16 picklock_screen_run(s16 score, s16 mode, Actor far *record);
extern void picklock_lock_slide_animation(s16 dir);
extern s16 picklock_screen_handle_drop(s16 item_kind, s16 slot);
extern void picklock_inv_info_query_disp(void);

#endif
