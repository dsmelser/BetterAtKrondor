#ifndef BAK_MDACON_H
#define BAK_MDACON_H

#include "structs.h"

extern u16 g_bMdaEnabled;

extern void mdacon_clear_screen(void);
extern void mdacon_write_string(s16 col, s16 row, char *str);
extern void mdacon_printf(s16 col, s16 row, char *fmt, ...);
extern void mdacon_draw_window_frame(s16 x, s16 y, s16 w, s16 h, s16 fill_interior);

#endif
