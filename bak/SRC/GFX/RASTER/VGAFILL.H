#ifndef BAK_VGAFILL_H
#define BAK_VGAFILL_H

#include "structs.h"

extern void vgaplanar_clear_column_strip(s16 vga_base_offset, s16 row_count);
extern void vgaplanar_fill_band(s16 start_row, s16 row_count, s16 color);

#endif
