/*
 * VIDDET.ASM in C: video shutdown and adapter detection.
 *
 * The original asks the BIOS and probes CRTC registers. A native host is a VGA with a
 * colour monitor, so each query has the answer a VGA gives: INT 10h AX=1A00h reports
 * display code 8 (BL), the EGA information call reports an active colour adapter
 * (BX != 10h, equipment bit 3 clear), the colour CRTC at 3D4h is writable and there is
 * no monochrome CRTC at 3B4h. What remains is the original's mapping from the
 * requested mode to an adapter number.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GFX/DRIVER/VIDDET.H"
#include "SRC/GFX/DRIVER/VIDINIT.H"
#include "SRC/GFX/DRIVER/VIDMODE.H"
#include "SRC/SYS/BIOSCHK.H"
#include "bak_game_end.h"

/* Restore the BIOS video mode video_init saved, once. Returns AX as the original leaves
 * it: the restored mode bits, or 0xFF when there was nothing to restore. */
s16 video_shutdown(void) {
    s16 mode = g_bSavedBiosVideoMode;
    if (mode != 0xff) {
        bios_set_video_mode_bits(mode);
        g_bSavedBiosVideoMode = 0xff;
    }
    return mode;
}

s16 video_detect_adapter(void) {
    unsigned char requested = g_bRequestedVideoMode;
    /* A word compare in the original: the flag and the byte after it. */
    if ((g_graphics_context_render.bVideoAutoDetectEnabled |
         g_graphics_context_render.bReserved_0xab5) == 0)
        return requested;
    switch (requested) {
    case 0x00:
    case 0x08:
    case 0x0a:
    case 0x0c:
    case 0x0d:
    case 0x0e:
    case 0x0f:
        /* VGA/MCGA query: the active display is a colour VGA. */
        return requested != 0 ? requested : 8;
    case 0x09:
        bios_video_int10_helper();
        return 9;
    case 0x05:
        /* Same query, EGA wanted: a VGA serves. */
        return 5;
    case 0x02:
    case 0x07:
    case 0x0b:
        /* EGA information: a colour EGA-compatible adapter is active. */
        return requested;
    case 0x01:
    case 0x03: {
        /* CGA wanted: the colour CRTC answers the register probe. */
        signed char compat;
        bios_video_int10_helper();
        compat = (signed char)bios_compat_check();
        if (requested != 1 && compat != 0)
            return 3;
        return requested == 3 ? 0 : 1;
    }
    default:
        /* Hercules/MDA (4) and unknown modes: no monochrome CRTC to find. */
        return 0;
    }
}

/* Not used natively: the CRTC probes above have their answers built in. */
unsigned char crtc_probe_data_register(void) {
    return 0;
}
