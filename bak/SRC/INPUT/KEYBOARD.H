#ifndef BAK_KEYBOARD_H
#define BAK_KEYBOARD_H

#include "structs.h"

extern unsigned char g_abAdapterRowStrideShift[16];
extern unsigned char g_keyboard_installed;
extern unsigned char g_bCapsLockEnabled;
extern unsigned short g_wIsrLastKeyEventDead;
extern unsigned char g_input_mode_lut[2];
extern unsigned char g_bUnused2cfa[250];
extern unsigned char g_key_state[121];
extern unsigned char g_ext_scancode_from[11];
extern unsigned char g_ext_scancode_to[11];
extern unsigned char g_bEnhancedKeyboard;

extern s16 keyboard_install(s16 install_timer_int);
extern unsigned char keyboard_restore_handler(void);
extern void isr_keyboard(void);
extern void near input_axis_accumulate_isr(void);
extern u16 kbhit_read(void);
extern s16 key_state_get_via_2cf8_table(s16 mode);
extern s16 key_is_down(s16 scancode);

#endif
