#ifndef BAK_UIWIDGET_H
#define BAK_UIWIDGET_H

#include "structs.h"

extern unsigned short g_nSkyHorizonRowCached;
extern unsigned short g_bTexturedPolyEnabled;
extern short g_nGlyphColorShift;
extern ImageRecord **g_tile_sprite;
extern ImageRecord **g_pCompassSpriteSheet;

extern void uiwidget_draw_text_shadowed_dflt(char *text, s16 shadow_color, s16 color, s16 x, s16 y);
extern void uiwidget_draw_text_shadowed(char *text, s16 shadow_color, s16 main_color, s16 x, s16 y);
extern s16 uiwidget_midpoint_int(s16 a, s16 b);
extern void uiwidget_panel_draw_3edge_bevel(s16 x, s16 y, s16 width, s16 height, s16 fill_color,
                                            s16 color1, s16 color2, s16 color3, s16 color4);
extern void uiwidget_panel_draw_inset(s16 x, s16 y, s16 width, s16 height, s16 color);
extern void uiwidget_tile_sprite_load(void);
extern void uiwidget_tile_sprite_free(void);
extern void uiwidget_panel_inset_inverted(s16 x, s16 y, s16 width, s16 height, s16 color_offset);
extern void uiwidget_compass_load(void);
extern void uiwidget_compass_free(void);
extern void uiwidget_compass_draw(void);
extern void uiwidget_draw_beveled_button(s16 x, s16 y, s16 w, s16 h);

#endif
