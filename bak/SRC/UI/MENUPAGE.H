#ifndef BAK_MENUPAGE_H
#define BAK_MENUPAGE_H

#include "structs.h"

extern short g_nJoyXVel;
extern short g_nJoyYVel;
extern MenuEntry *g_pMenuHoverEntry;
extern MenuEntry *g_pMenuPressAnchor;
extern MenuEntry *g_pMenuClickedEntry;
extern unsigned short g_wMenuDragSubMode;
extern unsigned short g_wMenuDragState;
extern s16 g_menupageCtrlArrowsToPage;

extern MenuPage *menupage_load(char *filename);
extern void menupage_free(MenuPage *page);
extern void menupage_begin(MenuPage *page);
extern void menupage_end(MenuPage *page);
extern void menupage_draw(MenuPage *page);
extern void menupage_draw_entries(MenuEntry *first, s16 count, s16 x, s16 y);
extern void menupage_cur_set_pos_clamped(s16 x, s16 y);
extern void menupage_cursor_joystick_step(void);
extern void menupage_navigate(MenuPage *page, s16 dx_sign, s16 dy_sign, s16 tab_mode);
extern s16 menupage_run(MenuPage *page, unsigned short *out_consumed);
extern s16 menupage_input_poll(MenuPage *page, unsigned short *out_consumed);
extern MenuEntry *menupage_state_0e84(void);
extern MenuEntry *menupage_focused_entry(void);
extern MenuEntry *menupage_state_0e80(void);
extern u16 menupage_highlight_color(void);
extern u16 menupage_state_0e7c(void);
extern u16 menupage_input_poll_key(void);

#endif
