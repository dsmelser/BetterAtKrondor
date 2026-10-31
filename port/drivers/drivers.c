/* The built-in video drivers, by VMCODE.OVL chunk index (see bak_video_driver_init). */
#include "vga.h"

const void *bak_video_driver_init(int16_t chunk) {
    switch (chunk) {
    case 8: /* "VGA:" */
        return vga_driver_init();
    default:
        return NULL;
    }
}

/*
 * The ROM 8x8 font video_init installs as font slot 1. The game switches to its own
 * font before drawing any text, so the glyphs are left blank rather than copying a
 * BIOS font; the slot only has to be occupied for font slot numbering to match.
 */
unsigned char *bak_rom_font_8x8(void) {
    static unsigned char font[128 * 8];
    return font;
}
