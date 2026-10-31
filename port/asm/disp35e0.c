/*
 * DISP35E0.ASM in C: a command to the digital driver at g_pMusicDriver with a fresh
 * 6-byte block for SS:SI, whose words 1 and 2 come back in AX and DX. The game calls
 * neither entry. The first loads command 0Dh and falls into the second, which takes its
 * command in AX: from C there is none to take, so natively it stops if ever called.
 */
#include <stddef.h>

#include "../drivers/dude.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/AUDIO/DRIVER/DISP35E0.H"
#include "SRC/AUDIO/RES/AUDRESIN.H"
#include "bak_game_end.h"

static u32 dispatch(u16 cmd) {
    u16 block[3] = {0, 0, 0}; /* uninitialized stack in the original */
    dude_digital_call(g_pMusicDriver, cmd, block);
    return (u32)block[2] << 16 | block[1];
}

void dispatch_via_35e0_cmd0d_seg1000(void) {
    dispatch(0x0d);
}

void dispatch_via_35e0_seg1000(void) {
    BAK_UNIMPLEMENTED("DISP35E0.ASM: dispatch_via_35e0_seg1000 (its command comes in AX)");
}
