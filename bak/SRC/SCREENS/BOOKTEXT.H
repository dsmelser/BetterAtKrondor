#ifndef BAK_BOOKTEXT_H
#define BAK_BOOKTEXT_H

#include "structs.h"

extern char g_szRomanDigits[7];
extern short g_anRomanValues[7];
extern short g_anRomanSubPair[7];

extern s16 booktext_render_page(PageDirectory far *page_directory, BookPage far *page,
                                s16 redraw_bg);
extern s16 booktext_layout_next_run(BookPage far *page, s16 y0, s16 y1, s16 y_bot);
extern s16 booktext_is_break_char(char c);

#endif
