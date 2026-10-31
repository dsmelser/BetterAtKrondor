#ifndef BAK_TIMERPL_H
#define BAK_TIMERPL_H

#include "structs.h"

extern TimerEventEntry *timerpool_upsert(u16 kind, u16 sub_id, u16 mode, s32 value);
extern void timerpool_tick(s16 delta_ticks);
extern s16 timerpool_contains(u16 kind, u16 sub_id);

#endif
