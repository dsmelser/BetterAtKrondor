#ifndef BAK_TIMER_H
#define BAK_TIMER_H

#include "structs.h"

extern char g_timer_installed;
/* The timer interrupt counts this down while the game waits on it; natively each
 * access first runs the interrupts that are due (BAK_POLLED). */
extern s16 g_nFrameTickCountdownValue;
#define g_nFrameTickCountdown BAK_POLLED(g_nFrameTickCountdownValue)
extern unsigned short g_nTimerPitDivisor;
extern unsigned short g_nTimerBiosChainReload;
extern unsigned short g_nTimerBiosChainCountdown;
extern unsigned short g_registry_slot_mask;
extern TimerCallback g_aTimerCallback[16];
extern TimerSched g_aTimerSched[16];

extern s16 registry_slot_alloc(FARFN callback, s16 interval);
extern s16 registry_slot_free(s16 slot);
extern void timer_install(u16 frequency);
extern s16 timer_restore_default(void);
extern void timer_irq0_dispatch(void);

#endif
