/*
 * MUSDISP.ASM in C: the game's commands to the digital driver at g_pMusicDriver, AX =
 * the command and SS:SI = the caller's stack arguments.
 *
 *   0 audio_driver_disp_seg1000_alt(a, b): start, a and b its arguments
 *   1 audio_music_tick: the timer's call (SNDINIT.C), every second tick
 *   2 audio_driver_disp_seg1000_far: stop
 *   6, 9, 0Ah, 0Bh, 0Ch: FUN_1000_13d7, 13e9, 13f0, 13f7, 1405, which the game never calls
 *
 * audio_music_tick first acknowledges the interrupt controller (EOI), as if it ended the
 * interrupt itself. The stubs without arguments point SI at whatever their caller had on
 * the stack; natively the block is absent (NULL).
 */
#include <stddef.h>

#include "../drivers/dude.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/AUDIO/DRIVER/MUSDISP.H"
#include "SRC/AUDIO/RES/AUDRESIN.H"
#include "bak_game_end.h"

static u16 dispatch(u16 cmd, void *args) {
    return dude_digital_call(g_pMusicDriver, cmd, args);
}

s16 audio_driver_disp_seg1000_alt(s16 a, s16 b) {
    s16 args[2] = {a, b};
    return (s16)dispatch(0, args);
}

void FUN_1000_13d7(void) {
    dispatch(6, NULL);
}

void audio_music_tick(void) {
    bak_port_out8(0x20, 0x20);
    dispatch(1, NULL);
}

void FUN_1000_13e9(void) {
    dispatch(9, NULL);
}

void FUN_1000_13f0(void) {
    dispatch(0x0a, NULL);
}

void FUN_1000_13f7(void) {
    dispatch(0x0b, NULL);
}

void audio_driver_disp_seg1000_far(void) {
    dispatch(2, NULL);
}

void FUN_1000_1405(void) {
    dispatch(0x0c, NULL);
}
