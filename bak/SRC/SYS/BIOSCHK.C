#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/SYS/BIOSCHK.H"

#ifdef BAK_NATIVE
/* The ROM probe (model byte FFh at F000:FFFE, '!' at F000:C000) identifies a Tandy; a
 * native host is never one, so the flag keeps its initial value. */
s16 bios_compat_check(void) {
    return (signed char)g_graphics_context.bBiosCompatible;
}
#else
s16 bios_compat_check(void) {
    asm push es;
    asm push bx;
    asm mov bx, 0F000h;
    asm mov es, bx;
    asm mov bx, 0FFFEh;
    asm mov al, byte ptr es : [bx];
    asm cmp al, 0FFh;
    asm jne done;
    asm mov bx, 0C000h;
    asm mov al, byte ptr es : [bx];
    asm cmp al, 21h;
    asm jne done;
    asm mov byte ptr[g_graphics_context.bBiosCompatible], 1;
done:
    asm mov al, byte ptr[g_graphics_context.bBiosCompatible];
    asm cbw;
    asm pop bx;
    asm pop es;
    return _AX;
}
#endif
