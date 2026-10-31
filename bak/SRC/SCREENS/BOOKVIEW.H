#ifndef BAK_BOOKVIEW_H
#define BAK_BOOKVIEW_H

#include "structs.h"

extern u16 g_wTextWrapXAccum;
extern u16 g_wTextWrapLinesDrawn;
extern u16 g_wTextWrapLinesRemaining;
extern BookViewerState *g_pBookViewer;

extern void bookview_init(void);
extern void bookview_shutdown(void);
extern s16 bookview_show(char *filename, s16 page_number);
extern BookPage far *bookview_find_page_by_number(PageDirectory far *page_directory,
                                                  s16 page_number);

#endif
