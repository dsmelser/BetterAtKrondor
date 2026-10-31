/*
 * vga.h - the native Mode-Y video driver: DRIVERS/VMCODE/VGA.ASM in C, drawing into
 * the chunky display-memory model of bak_video.h.
 *
 * Slots the original calls with Borland C stack arguments keep their argument lists
 * (far pointers become pointers). Register-convention slots take their registers as
 * parameters, in the order given in each comment.
 */
#ifndef BAK_DRIVERS_VGA_H
#define BAK_DRIVERS_VGA_H

#include "bak_game_begin.h"
#include "structs.h"
#include "bak_game_end.h"

/*
 * vga_init: puts the hardware in Mode Y, sets the page bases and the row-offset LUT in
 * the graphics context, marks the adapter (8) and returns the driver's slot table.
 */
const RendererVtable *vga_driver_init(void);

void vga_blit_glyph_row(s16 y, s16 x, s16 rows, const unsigned char *glyph);  /* 1: BP DX CX SI */
void vga_present(s16 x, s16 y, s16 width, s16 height);                        /* 4 */
void vga_save_rect(unsigned char *dst, s16 x, s16 y, s16 width, s16 height);  /* 5 */
s32 vga_blit_size(s16 width, s16 height);                                     /* 6 */
void vga_paste_rect(unsigned char *src, s16 x, s16 y, s16 width, s16 height); /* 7 */
void vga_vsync_wait(s16 wait);                                                /* 8 */
u32 vga_image_install(ImageRecord **images, s16 *auxSize);                    /* 13 */
void vga_image_decode(ImageRecord **images, unsigned char *data, u32 packedSize, unsigned char *aux,
                      u32 total);                                                     /* 14 */
void vga_unpack_nibbles_to_planes(unsigned char *src, unsigned char *dst, u16 count); /* 15 */
void vga_palette_set(unsigned char *palette);                                         /* 20 */
u16 vga_getpixel(s16 x, s16 y);                                                       /* 21 */
void vga_putpixel(s16 x, s16 y, s16 color);                                           /* 22 */
void vga_set_buffer_offset(s16 x, s16 y);                                             /* 24 */
void vga_palette_lerp_dac(u16 start, u16 count, u16 target, u16 weight);              /* 34 */
void vga_dac_write_palette(unsigned char *rgb, u16 start, u16 count);                 /* 35 */

#endif
