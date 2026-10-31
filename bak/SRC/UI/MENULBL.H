#ifndef BAK_MENULBL_H
#define BAK_MENULBL_H

#include "structs.h"

extern s16 g_nCreditsScrollYOffset;
extern s16 g_nCreditsMaxLabelHeight;

extern void menulbl_calc_max_label_width(void);
extern void menulbl_scroll_step_and_draw(s16 a, s16 b, s16 c);

#endif
