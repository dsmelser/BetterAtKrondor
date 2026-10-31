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

/*
 * The latches, for code outside the driver that moves them with plain memory accesses
 * (the mouse callback parks them at A000:FFFF): a CPU write to planar byte seg:off in
 * write mode 1 stores them into the planes mapMask enables, a CPU read loads all four.
 */
void vga_write_latches(u16 seg, u16 off, unsigned mapMask);
void vga_read_latches(u16 seg, u16 off);

void vga_blit_glyph_row(s16 y, s16 x, s16 rows, const unsigned char *glyph); /* 1: BP DX CX SI */
/* The native forms of register-convention slots that game code calls through the vtable
 * (any driver's, and the replacements the game swaps in): */
typedef void DrawLineFn(u16 seg, u16 x0, u16 y0, u16 x1, u16 y1);
void vga_draw_line(u16 seg, u16 x0, u16 y0, u16 x1, u16 y1); /* 2: ES BX CX DX SI */
/* 10, 11: ES DI BX CX AX (DX): a run of CX pixels from x = BX on the row at byte offset DI,
 * in colour AL, or dithered with AH (0: none) on the row whose parity DX gives. */
void vga_fill_run_a(u16 seg, u16 di, u16 x, u16 count, u16 colors, u16 dx);
void vga_fill_run_b(u16 seg, u16 di, u16 x, u16 count, u16 colors, u16 dx);
/* 27: ES:SI = the spans: the first row, the number of rows, then x0 and x1 of each. */
typedef void FillSpansFn(const unsigned char *spans);
void vga_fill_spans_dithered_modey(const unsigned char *spans);
/* 3, 29: a sprite from its record (a near pointer in the original), 29 with flip flags. */
typedef void SpriteMasterFn(const ImageRecord *sprite, s16 x, s16 y, u16 flags);
void vga_blit_sprite(const ImageRecord *sprite, s16 x, s16 y);
void vga_blit_sprite_master(const ImageRecord *sprite, s16 x, s16 y, u16 flags);
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
