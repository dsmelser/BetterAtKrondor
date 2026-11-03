/* The tags beside display memory (bak_video.h): a write stores the tag of the moment, a copy
 * through the latches carries its source's tags (but during a draw the draw's), and a rectangle
 * saved into a buffer and pasted back gets its tags again while the buffer still holds what was
 * saved. */
#include <string.h>

#include "unit.h"

#include "bak_video.h"
#include "../../drivers/vga.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "bak_game_end.h"

#define PAGE_A 0xA000
#define PAGE_B 0xA400

static uint8_t tag_at(uint16_t page, int x, int y) {
    return bak_vram_tags(bak_video_page(page))[y * BAK_VGA_STRIDE + x];
}

static uint8_t pixel_at(uint16_t page, int x, int y) {
    return bak_video_page(page)[y * BAK_VGA_STRIDE + x];
}

void unit_vram_tags_follow_the_pixels(void) {
    for (int y = 0; y < BAK_VGA_HEIGHT; y++)
        g_pRow_offset_lut[y] = (uint16_t)(y * 80);
    g_graphics_context.wGfxBlitSrcPage = g_graphics_context.wGfxBlitDstPage = PAGE_A;

    /* writes */
    g_bakVramTagNow = 7;
    vga_putpixel(10, 5, 42);
    g_bakVramTagNow = 0;
    vga_putpixel(11, 5, 43);
    UNIT_CHECK(pixel_at(PAGE_A, 10, 5) == 42 && tag_at(PAGE_A, 10, 5) == 7);
    UNIT_CHECK(pixel_at(PAGE_A, 11, 5) == 43 && tag_at(PAGE_A, 11, 5) == 0);

    /* a latch copy of pixels 8-11 into the other page */
    uint16_t off = (uint16_t)(5 * 80 + 8 / 4);
    vga_read_latches(PAGE_A, off);
    vga_write_latches(PAGE_B, off, 0x0f);
    UNIT_CHECK(pixel_at(PAGE_B, 10, 5) == 42 && tag_at(PAGE_B, 10, 5) == 7);
    UNIT_CHECK(tag_at(PAGE_B, 11, 5) == 0);
    /* during a draw (the ground's strip texture copied onto the page) the draw's tag instead */
    g_bakVramTagNow = 9;
    vga_read_latches(PAGE_A, off);
    vga_write_latches(PAGE_B, (uint16_t)(off + 80), 0x0f);
    g_bakVramTagNow = 0;
    UNIT_CHECK(pixel_at(PAGE_B, 10, 6) == 42 && tag_at(PAGE_B, 10, 6) == 9);
    UNIT_CHECK(tag_at(PAGE_B, 11, 6) == 9);

    /* saved, drawn over, pasted back */
    unsigned char buf[8];
    vga_save_rect(buf, 8, 5, 8, 1);
    for (int x = 8; x < 16; x++)
        vga_putpixel((s16)x, 5, 1);
    UNIT_CHECK(tag_at(PAGE_A, 10, 5) == 0);
    vga_paste_rect(buf, 8, 5, 8, 1);
    UNIT_CHECK(pixel_at(PAGE_A, 10, 5) == 42 && tag_at(PAGE_A, 10, 5) == 7);
    UNIT_CHECK(tag_at(PAGE_A, 11, 5) == 0);
    /* a buffer that holds something else since takes the tag of the moment */
    buf[2] = 99;
    g_bakVramTagNow = 3;
    vga_paste_rect(buf, 8, 5, 8, 1);
    UNIT_CHECK(pixel_at(PAGE_A, 10, 5) == 99 && tag_at(PAGE_A, 10, 5) == 3);
    UNIT_CHECK(tag_at(PAGE_A, 11, 5) == 3);
    g_bakVramTagNow = 0;
}
