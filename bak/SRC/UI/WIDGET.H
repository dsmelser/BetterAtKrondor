#ifndef BAK_WIDGET_H
#define BAK_WIDGET_H

#include "structs.h"

extern ImageRecord **g_pButtonSpriteUp;
extern ImageRecord **g_pBtnSpriteDownSheet;
extern s16 g_nScrollTrackOffset;
extern unsigned char g_saved_clip_enabled;
extern u16 g_saved_clip_ymax;
extern u16 g_saved_clip_xmin;
extern u16 g_saved_clip_xmax;
extern u16 g_saved_clip_ymin;

extern void widget_load_button_sprite_pair(char *filename);
extern void widget_free_button_sprite_pair(void);
extern void widget_blit_button_sprite(u16 sprite_idx, s16 x, s16 y, s16 palette, s16 center,
                                      s16 rect_w, s16 rect_h);
extern s16 widget_hit_test(MenuEntry *widget, s16 x, s16 y, s16 *out_region);
extern void widget_dispatch_by_type(MenuEntry *widget, s16 x, s16 y, s16 hover_flag);
extern void widget_toggle_state(MenuEntry *widget);
extern void widget_list_scroll(MenuEntry *widget, s16 dir);
extern void widget_scrollbar_update(MenuEntry *widget);
extern void widget_menu_draw(MenuEntry *widget, s16 x, s16 y, s16 hover_flag);
extern void widget_button_render_full(MenuEntry *button, s16 x, s16 y, s16 pressed, s16 sprite_set);
extern void widget_draw_text_button(MenuEntry *widget, s16 x, s16 y, s16 pressed);
extern void widget_draw_iconbox(MenuEntry *widget, s16 x, s16 y, s16 pressed, s16 sprite_flags);
extern void widget_draw_scrollbar(MenuEntry *widget, s16 x, s16 y, s16 pressed_part);
extern void widget_render_button(MenuEntry *widget, s16 x_off, s16 y_off, s16 hover_flag);
extern void widget_push_clip_rect(s16 xmin, s16 ymin, s16 xmax, s16 ymax);
extern void widget_pop_clip_rect(void);

#endif
