/*
 * heg.h - the native hi-res EGA/VGA video driver: DRIVERS/VMCODE/HEG.ASM in C, the book
 * viewer's 640-pixel, 16-colour driver, drawing through the platform's VGA.
 *
 * Slots the original calls with Borland C stack arguments keep their argument lists
 * (far pointers become pointers). Slot 1 takes its registers as parameters, as the VGA
 * driver's does (vga.h).
 */
#ifndef BAK_DRIVERS_HEG_H
#define BAK_DRIVERS_HEG_H

#include "bak_game_begin.h"
#include "structs.h"
#include "bak_game_end.h"

/*
 * heg_init: sets the BIOS mode for the screen height the engine asked for (350: mode
 * 10h), resets the attribute controller's palette to the identity, builds the 480-row
 * offset LUT and the clip rectangle in the graphics context and returns the slot table.
 */
const RendererVtable *heg_driver_init(void);

void heg_blit_glyph_row(s16 y, s16 x, s16 rows, const unsigned char *glyph); /* 1: BP DX CX SI */
/* 3, 29: an installed image from its record (a near pointer in the original), 29 with flip
 * flags (vga.h's SpriteMasterFn). */
void heg_blit_sprite_noflip(const ImageRecord *sprite, s16 x, s16 y);
void heg_blit_sprite_master(const ImageRecord *sprite, s16 x, s16 y, u16 flags);
void heg_present(s16 x, s16 y, s16 width, s16 height);                        /* 4 */
void heg_save_rect(unsigned char *dst, s16 x, s16 y, s16 width, s16 height);  /* 5 */
s32 heg_blit_size(s16 width, s16 height);                                     /* 6 */
void heg_paste_rect(unsigned char *src, s16 x, s16 y, s16 width, s16 height); /* 7 */
void heg_vsync_wait(s16 wait);                                                /* 8 */
u32 heg_image_install(ImageRecord **images, s16 *auxSize);                    /* 13 */
void heg_image_decode(ImageRecord **images, unsigned char *data, u32 packedSize, unsigned char *aux,
                      u32 total);                                              /* 14 */
/* Slot 14's work on data at a far pointer: its segment's paragraph, the bytes from there to
 * the block's end, and the offset (the differential test installs from an offset). */
void heg_image_decode_at(ImageRecord **images, unsigned char *base, u32 extent, u16 off,
                         u32 packedSize);
void heg_blit_chunky(unsigned char *src, s16 x, s16 y, s16 width, s16 height); /* 17 */
void heg_palette_set(unsigned char *palette);                                  /* 20 */
u16 heg_getpixel(s16 x, s16 y);                                                /* 21 */
void heg_putpixel(s16 x, s16 y, s16 color);                                    /* 22 */
void heg_set_border_color(s16 color);                                          /* 25 */
void heg_palette_lerp_dac(u16 start, u16 count, u16 target, u16 weight);       /* 34 */

#endif
