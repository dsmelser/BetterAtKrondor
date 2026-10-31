/*
 * MOUSE.ASM's data in C, with its initial values: the graphics-controller mode bytes
 * the callback primes for drawing (EGA write modes, switched to their VGA forms by
 * mouse_install on a VGA) and the latched mouse state. The callback's private stack is
 * kept for its symbol; natively the callback runs on the game's stack.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/INPUT/MOUSE.H"
#include "bak_game_end.h"

unsigned short g_mouse_x_mickeys;
unsigned short g_mouse_y_mickeys;
void far *g_pMouseHandlerCb;
unsigned char g_abMouseCbStack[400];
unsigned short g_wSavedVgaGcReg0AndIdx;
unsigned short g_wSavedVgaGcReg1And4;
unsigned short g_wSavedVgaGcReg5And8;
unsigned short g_wSavedVgaScReg2AndIdx;
unsigned char g_bSavedVgaGcReg3;
unsigned char g_bVgaGcReg5ModeA = 0x02;
unsigned char g_bVgaGcReg5Mode8AltA = 0x40;
unsigned char g_bVgaGcReg5ModeB = 0x01;
unsigned char g_bVgaGcReg5Mode8AltB = 0x41;
unsigned char g_mouse_installed;
unsigned char g_mouse_buttons;
