#ifndef BAK_FONT_H
#define BAK_FONT_H

#include "structs.h"

extern unsigned char g_abFontColorRemap[5];
extern unsigned char g_saved_text_fg_color;
extern unsigned char g_font_underline_offset[20];
extern s16 far *g_font_glyph_offset_table[20];
extern unsigned char far *g_font_width_table[20];
extern unsigned char far *g_font_bitmap_data[20];
extern unsigned char g_font_format[20];

extern s16 font_activate(s16 slot);
extern s16 font_get_line_height(s16 slot);
extern s16 font_height_get(s16 slot);
extern s16 font_slot_in_use(s16 slot);
extern s16 font_text_width_ds(char *text);
extern s16 font_text_pixel_width(char far *str);
extern u16 font_draw_char(unsigned char ch, s16 x, s16 y);
extern s16 font_render_glyph_or_ctrl(char ch, s16 x, s16 y);
extern void font_draw_text_ds(char *text, s16 x, s16 y);
extern void font_draw_text_far(char far *text, s16 x, s16 y);
extern s16 font_glyph_metrics(s16 ch, u16 *out_width, u16 *out_height);

#endif
