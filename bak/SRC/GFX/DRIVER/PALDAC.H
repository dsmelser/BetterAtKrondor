#ifndef BAK_PALDAC_H
#define BAK_PALDAC_H

#include "structs.h"

extern unsigned char bios_palette_read(s16 reg_index);
extern void vga_read_palette(unsigned char *buf, s16 start, s16 count);
extern void vga_write_palette(unsigned char *src, s16 start_index, s16 num_colors);

#endif
