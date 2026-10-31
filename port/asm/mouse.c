/*
 * MOUSE.ASM in C: mouse install and teardown, the cursor range, the event callback and
 * the position and button queries.
 *
 * Each INT 33h call becomes the matching service of the emulated driver
 * (port/platform/mouse.c). The callback no longer switches to a private stack: the
 * driver calls it from the event pump, on the game's own stack. Around the game's
 * handler it saves and primes the graphics-controller and sequencer registers as the
 * original does, parking the foreground's VGA latches in display memory at A000:FFFF
 * and reloading them afterwards.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/INPUT/MOUSE.H"
#include "bak_game_end.h"

#include "bak_video.h"
#include "../drivers/vga.h"

#define LATCH_SEG 0xA000
#define LATCH_OFF 0xFFFF

/* The driver's x/y ranges: four units per pixel, last pixel inclusive. */
void mouse_set_cursor_clip_rect(s16 x, s16 y, s16 w, s16 h) {
    bak_int33_set_x_range((s16)(x << 2), (s16)((s16)(x + w - 1) << 2));
    bak_int33_set_y_range((s16)(y << 2), (s16)((s16)(y + h - 1) << 2));
}

s16 mouse_install(void) {
    if (g_mouse_installed != 0)
        return 0;
    /* fn 0 returns FFFFh with a driver present; the flag keeps the low byte of its negation. */
    u16 present = bak_int33_reset();
    g_mouse_installed = (unsigned char)(0 - present);
    if (present == 0)
        return 0;
    bak_int33_set_position(0x7fff, 0x7fff);
    bak_int33_show_cursor();
    bak_int33_hide_cursor();
    bak_int33_set_ratio(8, 8);
    bak_int33_set_position(0, 0);
    mouse_set_cursor_clip_rect(0, 0, (s16)g_wScreen_width, (s16)g_wScreen_height);
    bak_int33_set_handler(0x1f, mouse_event_callback);
    if (g_graphics_context.bVideoAdapter == 8) {
        g_bVgaGcReg5ModeA = g_bVgaGcReg5Mode8AltA;
        g_bVgaGcReg5ModeB = g_bVgaGcReg5Mode8AltB;
    }
    return 1;
}

void mouse_set_handler_cb(void far *cb) {
    g_pMouseHandlerCb = cb;
}

void mouse_event_callback(u16 x_mickeys, u16 y_mickeys, u16 buttons) {
    g_mouse_buttons = (unsigned char)buttons;
    g_mouse_x_mickeys = x_mickeys;
    g_mouse_y_mickeys = y_mickeys;
    if (g_pMouseHandlerCb == NULL)
        return;
    vga_save_and_setup_gc_sc();
    (*(void (*)(void))g_pMouseHandlerCb)();
    vga_restore_registers();
}

void vga_save_and_setup_gc_sc(void) {
    unsigned char index, value;

    index = bak_port_in8(0x3ce);
    bak_port_out8(0x3ce, 0);
    g_wSavedVgaGcReg0AndIdx = (u16)(index << 8 | bak_port_in8(0x3cf));

    bak_port_out8(0x3ce, 1);
    value = bak_port_in8(0x3cf);
    bak_port_out8(0x3cf, 0);
    bak_port_out8(0x3ce, 4);
    g_wSavedVgaGcReg1And4 = (u16)(value << 8 | bak_port_in8(0x3cf));

    bak_port_out8(0x3ce, 5);
    value = bak_port_in8(0x3cf);
    bak_port_out8(0x3cf, g_bVgaGcReg5ModeB);
    /* A byte stored in write mode 1: the latches go to the planes the map mask enables. */
    vga_write_latches(LATCH_SEG, LATCH_OFF, bak_vga_sequencer(2));
    bak_port_out8(0x3cf, g_bVgaGcReg5ModeA);
    bak_port_out8(0x3ce, 8);
    g_wSavedVgaGcReg5And8 = (u16)(value << 8 | bak_port_in8(0x3cf));
    bak_port_out8(0x3cf, 0xff);

    bak_port_out8(0x3ce, 3);
    g_bSavedVgaGcReg3 = bak_port_in8(0x3cf);
    bak_port_out8(0x3cf, 0);

    index = bak_port_in8(0x3c4);
    bak_port_out8(0x3c4, 2);
    g_wSavedVgaScReg2AndIdx = (u16)(index << 8 | bak_port_in8(0x3c5));
    bak_port_out8(0x3c5, 0x0f);
}

void vga_restore_registers(void) {
    bak_port_out8(0x3c4, 2);
    bak_port_out8(0x3c5, (unsigned char)g_wSavedVgaScReg2AndIdx);
    bak_port_out8(0x3c4, (unsigned char)(g_wSavedVgaScReg2AndIdx >> 8));

    bak_port_out8(0x3ce, 3);
    bak_port_out8(0x3cf, g_bSavedVgaGcReg3);
    bak_port_out8(0x3ce, 8);
    bak_port_out8(0x3cf, (unsigned char)g_wSavedVgaGcReg5And8);
    bak_port_out8(0x3ce, 5);
    bak_port_out8(0x3cf, g_bVgaGcReg5ModeB);
    /* A read reloads all four latches with what the save parked there. */
    vga_read_latches(LATCH_SEG, LATCH_OFF);
    bak_port_out8(0x3cf, (unsigned char)(g_wSavedVgaGcReg5And8 >> 8));

    bak_port_out8(0x3ce, 4);
    bak_port_out8(0x3cf, (unsigned char)g_wSavedVgaGcReg1And4);
    bak_port_out8(0x3ce, 1);
    bak_port_out8(0x3cf, (unsigned char)(g_wSavedVgaGcReg1And4 >> 8));
    bak_port_out8(0x3ce, 0);
    bak_port_out8(0x3cf, (unsigned char)g_wSavedVgaGcReg0AndIdx);
    bak_port_out8(0x3ce, (unsigned char)(g_wSavedVgaGcReg0AndIdx >> 8));
}

s16 mouse_restore_handler(void) {
    if (g_mouse_installed == 0)
        return 0;
    g_mouse_installed = 0;
    bak_int33_reset();
    bak_int33_set_handler(0, NULL);
    return 1;
}

/* Pixels from the latched position (four units per pixel), when a mouse is installed.
 * The original returns nothing in particular: AX is the last value it moved. The driver's
 * interrupt, which latches the position and the buttons, runs natively when the game polls,
 * so the queries poll first: the game waits on them in bare loops. */
u16 mouse_get_position(u16 *out_x, u16 *out_y) {
    if (g_mouse_installed == 0)
        return 0;
    bak_poll();
    *out_x = (u16)(g_mouse_x_mickeys >> 2);
    *out_y = (u16)(g_mouse_y_mickeys >> 2);
    return *out_y;
}

s16 mouse_set_position(s16 x, s16 y) {
    if (g_mouse_installed == 0)
        return 0;
    g_mouse_x_mickeys = (u16)(x << 2);
    g_mouse_y_mickeys = (u16)(y << 2);
    bak_int33_set_position((s16)g_mouse_x_mickeys, (s16)g_mouse_y_mickeys);
    return 1;
}

/* Button 0 tests bit 0 of the latched mask; any other button bit 1. */
s16 mouse_button_pressed(s16 button) {
    u16 mask;
    if (g_mouse_installed == 0)
        return 0;
    bak_poll();
    mask = g_mouse_buttons;
    if (button != 0)
        mask >>= 1;
    return (s16)(mask & 1);
}
