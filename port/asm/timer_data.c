/* TIMER.ASM's data in C, with its initial values. */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/INPUT/TIMER.H"
#include "bak_game_end.h"

char g_timer_installed;
s16 g_nFrameTickCountdownValue;
unsigned short g_nTimerPitDivisor = 0xffff;
unsigned short g_nTimerBiosChainReload;
unsigned short g_nTimerBiosChainCountdown;
unsigned short g_registry_slot_mask;
TimerCallback g_aTimerCallback[16];
TimerSched g_aTimerSched[16];
