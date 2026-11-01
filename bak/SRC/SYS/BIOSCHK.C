#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/SYS/BIOSCHK.H"

/* The ROM probe (model byte FFh at F000:FFFE, '!' at F000:C000) identifies a Tandy; a
 * native host is never one, so the flag keeps its initial value. */
s16 bios_compat_check(void) {
    return (signed char)g_graphics_context.bBiosCompatible;
}
