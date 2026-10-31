#ifndef BAK_VGABLIT_H
#define BAK_VGABLIT_H

#include "structs.h"

extern void vga_planar_fill_rects(void);
#ifdef BAK_NATIVE
/* Register-convention routines the game swaps into the renderer's span (27) and line (2)
 * slots: natively they take those slots' arguments (port/drivers/vga.h). */
extern void vgablit_planar_runs(const unsigned char *spans);
extern void vgablit_span_emit_textured(u16 seg, u16 x0, u16 y0, u16 x1, u16 y1);
extern void vgablit_planar_runs_untiled(const unsigned char *spans);
#else
extern void vgablit_planar_runs(void);
extern void vgablit_span_emit_textured(void);
extern void vgablit_planar_runs_untiled();
#endif

#endif
