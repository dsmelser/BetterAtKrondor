/*
 * SNDDRV.ASM in C: calls into the digital driver (SNDINST.C's "music driver": the Sound
 * Blaster's ASB, ...) with AX = command and SI = data.
 *
 * snddrv_patch_farptr keeps the driver's chunk; both dispatches do nothing while
 * g_bMusicDriverInstalled is clear. The cached one returns the driver's AX, or without a
 * driver DGROUP's segment (it loads it into AX for DS before the test). That depends on
 * where DOS loaded the game; natively it is 29DDh, DGROUP's paragraph in the 1.02 image.
 * Only a poll of a digital stream looks (MIDI.ASM): both bytes nonzero, as for all but
 * one load address in 256, restart the track's clock and end the stream. The game never
 * gets there: every synth driver reports no digital capability, so streams exist only
 * with a digital driver installed.
 */
#include <stdint.h>

#include "../drivers/dude.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/AUDIO/DRIVER/SNDDRV.H"
#include "SRC/AUDIO/RES/AUDRESIN.H"
#include "bak_game_end.h"

static unsigned char *s_chunk; /* snddrv_pfnDispatch */

#define DGROUP_SEGMENT 0x29dd

/* The chunk's far pointer, as offset and segment words: a host pointer's bits. The
 * driver must be ported: checked now rather than at its first call. */
void snddrv_patch_farptr(unsigned short offset, unsigned short segment) {
    s_chunk = (unsigned char *)(uintptr_t)((u32)segment << 16 | offset);
    if (dude_digital_entry(s_chunk) == NULL)
        dude_digital_call(s_chunk, 0xffff, NULL); /* stops, naming the driver */
}

unsigned short snddrv_dispatch_cached(s16 cmd, void *data) {
    if (g_bMusicDriverInstalled == 0)
        return DGROUP_SEGMENT;
    return dude_digital_call(s_chunk, (u16)cmd, data);
}

/* Not called by the game. */
void snddrv_dispatch(s16 cmd, s16 data) {
    if (g_bMusicDriverInstalled != 0)
        dude_digital_call(s_chunk, (u16)cmd, (void *)(uintptr_t)(u16)data);
}
