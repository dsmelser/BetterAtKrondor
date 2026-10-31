/*
 * bak_video.h - the emulated VGA display memory, for game code compiled natively.
 *
 * The original draws into four Mode-Y pages identified by segment handles
 * (A000, A400, A800 and the AC00 scratch page). Natively each page is a chunky
 * 320x200 buffer, one byte (palette index) per pixel, row-major.
 */
#ifndef BAK_VIDEO_H
#define BAK_VIDEO_H

#include <stdint.h>

#define BAK_VGA_WIDTH 320
#define BAK_VGA_HEIGHT 200

/* The chunky pixel buffer of the page whose Mode-Y segment handle is pageSeg. */
uint8_t *bak_video_page(uint16_t pageSeg);

#endif
