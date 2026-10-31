#ifndef BAK_JOYSTICK_H
#define BAK_JOYSTICK_H

#include "structs.h"

extern unsigned char g_bJoystick0Installed;
extern unsigned char g_bJoystick1Installed;
extern unsigned char g_bJoyAxisTimeoutMask;
extern u16 g_nJoy0CenterX;
extern u16 g_nJoy0CenterY;
extern u16 g_nJoy1CenterX;
extern u16 g_nJoy1CenterY;
extern u16 g_nJoy0SensX;
extern u16 g_nJoy0SensY;
extern u16 g_nJoy1SensX;
extern u16 g_nJoy1SensY;
extern u16 g_nJoyReadScratchX;
extern u16 g_nJoyLoopDelay;
extern s16 g_nJoyDirScratchX;
extern s16 g_nJoyDirScratchY;

extern void near gameport_read_axis();
extern s16 near joystick_normalize_axis(void);
extern s16 joystick_init(void);
extern void joystick_read_position(s16 which, s16 *out_x, s16 *out_y);
extern unsigned char joystick_read_direction(s16 which);
extern s16 joystick_button_pressed(s16 button);
extern void joystick_poll_axis(s16 axis);

#endif
