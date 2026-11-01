/* The system cursor (cursor.c): the game's cursor images as the two video drivers keep them. */
#include <string.h>

#include <SDL3/SDL.h>

#include "unit.h"

#include "../../platform/platform.h"
#include "../../platform/vgaregs.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "bak_game_end.h"

#define C CURSOR_CLEAR

/* The VGA driver's: row-major bytes from the offset rounded down to 4, rows width & ~3 long. */
void unit_cursor_reads_the_vga_drivers_images(void) {
    static uint8_t block[64];
    memset(block, 0xEE, sizeof block);
    static const uint8_t rows[] = {0, 5, 0, 7, 1, 2, 3, 0};
    memcpy(block + 8, rows, sizeof rows);
    uint16_t seg = bak_seg_register(block, sizeof block);
    ImageRecord image = {seg, 9, 0, 6, 2}; /* 6 wide: the last 2 of a row are never drawn */
    int w = 0, h = 0;
    uint16_t px[CURSOR_MAX * CURSOR_MAX];
    UNIT_CHECK(cursor_decode(&image, true, &w, &h, px));
    UNIT_CHECK(w == 4 && h == 2);
    static const uint16_t want[] = {C, 5, C, 7, 1, 2, 3, C};
    UNIT_CHECK(memcmp(px, want, sizeof want) == 0);
    /* Past the block: transparent. */
    image.wImageOff = 60;
    UNIT_CHECK(cursor_decode(&image, true, &w, &h, px));
    UNIT_CHECK(px[0] == 0xEE && px[3] == 0xEE && px[4] == C && px[7] == C);
    /* Too big, or nothing to draw. */
    image.nHeight = CURSOR_MAX + 1;
    UNIT_CHECK(!cursor_decode(&image, true, &w, &h, px));
    image.nHeight = 2;
    image.nWidth = 3;
    UNIT_CHECK(!cursor_decode(&image, true, &w, &h, px));
    image.nWidth = CURSOR_MAX + 3; /* 64 drawn */
    UNIT_CHECK(cursor_decode(&image, true, &w, &h, px) && w == CURSOR_MAX);
    bak_seg_unregister(seg);
}

/* The 16-colour driver's: four bit planes, width / 8 bytes a row, colours through the
 * attribute controller (the mode 13h registers map each to itself). */
void unit_cursor_reads_the_16_colour_drivers_images(void) {
    static uint8_t block[32];
    memset(block, 0, sizeof block);
    /* 16 x 2: 4 bytes a plane. (0,0) is colour 3 (planes 0 and 1), (15,1) colour 6 (1, 2). */
    block[0 * 4 + 0] |= 0x80;
    block[1 * 4 + 0] |= 0x80;
    block[1 * 4 + 3] |= 0x01;
    block[2 * 4 + 3] |= 0x01;
    uint16_t seg = bak_seg_register(block, sizeof block);
    ImageRecord image = {seg, 0, 16, 16, 2}; /* the mask after the planes */
    int w = 0, h = 0;
    uint16_t px[CURSOR_MAX * CURSOR_MAX];
    UNIT_CHECK(cursor_decode(&image, false, &w, &h, px));
    UNIT_CHECK(w == 16 && h == 2);
    UNIT_CHECK(px[0] == 3 && px[16 + 15] == 6);
    int opaque = 0;
    for (int i = 0; i < 32; i++)
        opaque += px[i] != C;
    UNIT_CHECK(opaque == 2);
    /* A palette register that maps colour 3 elsewhere. */
    uint8_t kept = g_vga.attr[3];
    g_vga.attr[3] = 0x21;
    UNIT_CHECK(cursor_decode(&image, false, &w, &h, px) && px[0] == 0x21 && px[16 + 15] == 6);
    g_vga.attr[3] = kept;
    image.nWidth = CURSOR_MAX + 8;
    UNIT_CHECK(!cursor_decode(&image, false, &w, &h, px));
    bak_seg_unregister(seg);
}

/* With video.cursor = game the game draws its cursor; else the system shows it. */
void unit_cursor_goes_to_the_system_unless_set_otherwise(void) {
    static uint8_t block[8] = {0, 1, 2, 3};
    uint16_t seg = bak_seg_register(block, sizeof block);
    ImageRecord image = {seg, 0, 0, 4, 1};
    settings_load_text("", 0);
    UNIT_CHECK(bak_cursor_system(&image, 0, 0));
    UNIT_CHECK(bak_cursor_system(NULL, 0, 0));
    char why[100];
    UNIT_CHECK(settings_override("video.cursor=game", why, sizeof why));
    UNIT_CHECK(!bak_cursor_system(&image, 0, 0));
    settings_load_text("", 0);
    bak_seg_unregister(seg);
}
