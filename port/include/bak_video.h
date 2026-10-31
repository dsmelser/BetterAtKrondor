/*
 * bak_video.h - the emulated VGA display memory, for game and port code.
 *
 * The original runs the VGA in Mode Y: four 64 KB planes behind the A000 window, a
 * pixel at (x, y) of the page at segment S living in plane x & 3 at offset
 * (S - A000h) * 16 + y * 80 + x / 4. Natively the 256 KB are one chunky array in
 * which planar byte (offset o, plane p) is element o * 4 + p. A page is then a plain
 * 320-byte-stride bitmap starting at element (S - A000h) * 64, and everything the
 * planar layout implies carries over unchanged: pages sit 64 KB apart (A000, A400,
 * A800 and the AC00 scratch page), a write past the end of a page lands in the next
 * one, and the CRTC start address selects the visible window anywhere in it.
 */
#ifndef BAK_VIDEO_H
#define BAK_VIDEO_H

#include <stdint.h>

#define BAK_VGA_WIDTH 320
#define BAK_VGA_HEIGHT 200
#define BAK_VGA_STRIDE 320          /* chunky bytes per row (80 planar bytes x 4 planes) */
#define BAK_VRAM_SIZE (4 * 0x10000) /* the VGA's 256 KB */

extern uint8_t g_bakVram[BAK_VRAM_SIZE];

/* Pixel 0 of the Mode-Y page whose segment handle is pageSeg (A000h..AFFFh). */
uint8_t *bak_video_page(uint16_t pageSeg);

/*
 * Wait for vertical retrace on the virtual 70 Hz VGA clock and show the frame the
 * hardware would now display. next = 0 returns at once when retrace is already under
 * way (a "wait until retrace" loop); next = 1 waits for the next one (a "wait until
 * not retrace, then until retrace" loop).
 */
void bak_vga_wait_retrace(int next);

/* The chunky element of planar byte (seg:off, plane). */
static inline uint8_t *bak_vram_planar(uint16_t seg, uint16_t off, unsigned plane) {
    uint32_t o = (uint32_t)(uint16_t)(seg - 0xA000u) * 16u + off;
    return &g_bakVram[((o * 4u) + plane) & (BAK_VRAM_SIZE - 1)];
}

#endif
