#ifndef BAK_SYSLOWIO_H
#define BAK_SYSLOWIO_H

#include "structs.h"

extern char g_cBusyCursorShown;
/* Advanced by a timer callback; natively each access first runs the interrupts that
 * are due (BAK_POLLED), as the game reads them to wait. */
extern u32 g_timer_ticks_value;
extern u32 g_dwSysTickCount_value;
#define g_timer_ticks BAK_POLLED(g_timer_ticks_value)
#define g_dwSysTickCount BAK_POLLED(g_dwSysTickCount_value)

extern void syslowio_timer_tick_isr(void);
extern void syslowio_mouse_handler_cb_noop(void);

#endif
