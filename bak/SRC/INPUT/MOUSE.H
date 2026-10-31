#ifndef BAK_MOUSE_H
#define BAK_MOUSE_H

#include "structs.h"

extern unsigned short g_mouse_x_mickeys;
extern unsigned short g_mouse_y_mickeys;
extern void far *g_pMouseHandlerCb;
extern unsigned char g_abMouseCbStack[400];
extern unsigned short g_wSavedVgaGcReg0AndIdx;
extern unsigned short g_wSavedVgaGcReg1And4;
extern unsigned short g_wSavedVgaGcReg5And8;
extern unsigned short g_wSavedVgaScReg2AndIdx;
extern unsigned char g_bSavedVgaGcReg3;
extern unsigned char g_bVgaGcReg5ModeA;
extern unsigned char g_bVgaGcReg5Mode8AltA;
extern unsigned char g_bVgaGcReg5ModeB;
extern unsigned char g_bVgaGcReg5Mode8AltB;
extern unsigned char g_mouse_installed;
extern unsigned char g_mouse_buttons;

extern s16 mouse_install(void);
extern void mouse_set_cursor_clip_rect(s16 x, s16 y, s16 w, s16 h);
extern void mouse_set_handler_cb(void far *cb);
extern void mouse_event_callback(void);
extern void vga_save_and_setup_gc_sc(void);
extern void vga_restore_registers(void);
extern s16 mouse_restore_handler(void);
extern u16 mouse_get_position(u16 *out_x, u16 *out_y);
extern s16 mouse_set_position(s16 x, s16 y);
extern s16 mouse_button_pressed(s16 button);

#endif
