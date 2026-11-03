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
 * Beside each pixel of display memory, a tag: the 3-D frame that drew it (its tag, 1-255, from
 * scene.c), or 0 for any other drawing. Every write stores the tag of the moment, which is
 * nonzero only while a world pass of the 3-D renderer draws its view; a copy through the
 * latches carries its source's tags, but while a view is drawn the tag of the moment (the
 * ground's strip texture is copied onto the page through the latches); a copy of a page's
 * rows carries its tags. The window shows the GPU's view of a frame where the picture's
 * pixels carry its tag (M7).
 */
extern uint8_t g_bakVramTag[BAK_VRAM_SIZE];
extern uint8_t g_bakVramTagNow;
extern uint8_t g_bakVgaLatchTag[4];

/* A pixel of display memory written, with the tag of the moment. */
static inline void bak_vram_put(uint8_t *p, uint8_t value) {
    *p = value;
    g_bakVramTag[p - g_bakVram] = g_bakVramTagNow;
}

/* The tag a write of a latch stores: the tag of the moment if there is one (a view's draw under
 * way: the ground's strip texture copied onto the page), else its source's. */
static inline uint8_t bak_vram_latch_tag(unsigned plane) {
    return g_bakVramTagNow != 0 ? g_bakVramTagNow : g_bakVgaLatchTag[plane];
}

/* The tags beside display memory from p on. */
static inline uint8_t *bak_vram_tags(uint8_t *p) {
    return &g_bakVramTag[p - g_bakVram];
}

/*
 * Wait for vertical retrace on the virtual 70 Hz VGA clock and show the frame the
 * hardware would now display. next = 0 returns at once when retrace is already under
 * way (a "wait until retrace" loop); next = 1 waits for the next one (a "wait until
 * not retrace, then until retrace" loop).
 */
void bak_vga_wait_retrace(int next);

/* A sequencer register's value (2 is the map mask), without touching the index port. */
uint8_t bak_vga_sequencer(uint8_t reg);

/* The four latches: every CPU read of display memory loads them, write mode 1 stores
 * them, and the other write modes' logical operations and bit mask combine with them. */
extern uint8_t g_bakVgaLatch[4];

/*
 * A CPU read of display memory at seg:off, through the graphics controller: loads the
 * latches and returns the read map's plane (read mode 0) or the colour-compare result
 * (read mode 1). Past the 64 KB window it reads FFh.
 */
uint8_t bak_vga_read(uint16_t seg, uint16_t off);

/* A CPU write: write modes 0-3 (rotation, set/reset, logical operation, bit mask) into
 * the planes the map mask enables. Past the window it is lost. */
void bak_vga_write(uint16_t seg, uint16_t off, uint8_t value);

/* INT 10h AH=00h for mode 10h or 13h: the BIOS's registers, display memory cleared. */
void bak_int10_set_mode(uint8_t mode);

/* INT 10h AX=1013h: BL = 0 selects the colour paging mode BH, BL = 1 the page BH. */
void bak_int10_palette_paging(uint8_t bl, uint8_t bh);

/* INT 10h AX=1001h: the overscan (border) colour. */
void bak_int10_set_border(uint8_t color);

/* The chunky element of planar byte (seg:off, plane). */
static inline uint8_t *bak_vram_planar(uint16_t seg, uint16_t off, unsigned plane) {
    uint32_t o = (uint32_t)(uint16_t)(seg - 0xA000u) * 16u + off;
    return &g_bakVram[((o * 4u) + plane) & (BAK_VRAM_SIZE - 1)];
}

/* The same where the CPU reaches it: NULL past the 64 KB VGA window at A000:0000, where
 * a VGA-only PC has nothing (a page segment plus an offset can point there). */
static inline uint8_t *bak_vram_window(uint16_t seg, uint16_t off, unsigned plane) {
    uint32_t linear = ((uint32_t)seg << 4) + off;
    if (linear - 0xA0000u >= 0x10000u)
        return (uint8_t *)0;
    return &g_bakVram[(linear - 0xA0000u) * 4u + plane];
}

#endif
