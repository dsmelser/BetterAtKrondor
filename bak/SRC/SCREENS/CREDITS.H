#ifndef BAK_CREDITS_H
#define BAK_CREDITS_H

#include "structs.h"

extern u16 g_nCreditsPassCount;
extern unsigned char *g_pCreditsBlob;
extern char **g_credits_strings;
extern s16 g_credits_pair_count;
extern s16 g_credits_scroll_y;

extern void credits_load(void);
extern void credits_free(void);
extern s16 credits_draw_scroll_pass(s16 x_left, s16 width, s16 reset);
extern s16 credits_run(void);

#endif
