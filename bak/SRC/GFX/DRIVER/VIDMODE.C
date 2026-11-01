/*
 * VIDMODE.C -- BIOS video-mode field get/set (segment 180c).
 *
 * bios_set_video_mode_bits (0x1adc2, 35 bytes) writes mode_bits<<4 into the BDA
 * equipment-word bits 5:4 (0040:0010), preserving the rest, then INT 10h AX=3
 * BX=3. bios_get_video_mode_bits (0x1ade5, 24 bytes) reads that field back and
 * returns it right-aligned in the low nibble (0=EGA/VGA, 1=CGA-40, 2=CGA-80,
 * 3=MDA). Both are near, .cas pseudo-register bodies (bc31 -O2). set takes an
 * int (the word-wide [bp+4] load, not uchar); get's C statements give it the
 * frame and the load-form 32 E4 zero-extend from `_AH ^= _AH`.
 *
 * Split from VIDMODE.ASM; the third proc bios_video_int10_helper needs a
 * different recipe (-O2 without -k, frameless) and lives in VIDINT10.C, linked
 * immediately after this object.
 */
#include "structs.h"
#include "SRC/GFX/DRIVER/VIDMODE.H"

/* The BIOS equipment-word video field, emulated. A VGA machine boots in 80x25 colour (2);
 * the INT 10h text-mode reset that accompanies a write has no native counterpart. */
static s16 s_equipmentVideoBits = 2;

void bios_set_video_mode_bits(s16 mode_bits) {
    s_equipmentVideoBits = mode_bits & 3;
}

s16 bios_get_video_mode_bits(void) {
    return s_equipmentVideoBits;
}
