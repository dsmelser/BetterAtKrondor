#ifndef BAK_TIMERPL_H
#define BAK_TIMERPL_H

#include "structs.h"

extern TimerEventEntry *timerpool_upsert(unsigned short kind, unsigned short sub_id, unsigned short mode, s32 value);
extern void timerpool_tick(s16 delta_ticks);
extern s16 timerpool_contains(unsigned short kind, unsigned short sub_id);

#endif
