#ifndef BAK_VTHUNKS_H
#define BAK_VTHUNKS_H

#include "structs.h"

extern void gfx_dispatch_noop(void);
extern void set_border_color(s16 color);
extern void renderer_vslot26_dispatch();
extern void vsync_hook(s16 a);
extern void cga_save_rect_to_buffer(unsigned char far *pDest, s16 x_start, s16 row, s16 width,
                                    s16 row_count);
extern s32 rect_byte_size(s16 width, s16 height);

#endif
