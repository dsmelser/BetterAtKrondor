#ifndef BAK_CHARSCRN_H
#define BAK_CHARSCRN_H

#include "structs.h"

extern char g_dialog_in_char_screen;

extern void charscreen_info_loop(Combatant *actor);
extern void charscreen_recalc_condition_tick(void);
extern void charscreen_temple_heal_menu(s16 actor_filter, s16 mode);

#endif
