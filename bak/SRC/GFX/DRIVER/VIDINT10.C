/*
 * VIDINT10.C -- bios_video_int10_helper (0x1adfd, 20 bytes, segment 180c).
 *
 * Forces 80x25 colour text: rewrites the BDA equipment-word bits 5:4
 * (0040:0010) to 10b, then INT 10h AX=3 BX=3. ES is an implicit input (callers
 * load the 0x40 BDA segment before the near call), so the body is pure asm
 * statements -- and an asm-only body with no params is exactly the shape bcc
 * leaves frameless under -O2 without -k (cf. BIOSCHK.C's bios_compat_check),
 * which is why this proc uses a different recipe from VIDMODE.C's get/set and
 * keeps its own TU.
 *
 * Split from VIDMODE.ASM; linked immediately after VIDMODE.OBJ so it stays at
 * 0x1adfd.
 */
#include "structs.h"
#include "SRC/GFX/DRIVER/VIDMODE.H"

/* Forcing 80x25 colour text mode has no native counterpart. */
void bios_video_int10_helper(void) {
}
