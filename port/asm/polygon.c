/*
 * POLYGON.ASM in C, so far its dispatcher: fill_spans_dispatch hands a span table to the
 * renderer's span slot (27), which takes it in ES:SI. blit_rect_buf_dispatch (slot 23) has
 * no caller in the game. It and the polygon rasterizer (draw_polygon and the edge walkers
 * it calls) trap until they are translated.
 */
#include "../drivers/vga.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/RASTER/POLYGON.H"
#include "bak_game_end.h"

void fill_spans_dispatch(unsigned short off, unsigned short seg) {
    ((FillSpansFn *)g_renderer_vtable.pfn_fill_spans_dithered_modey)(MK_FP(seg, off));
}

void blit_rect_buf_dispatch(unsigned char *far_ptr) {
    (void)far_ptr;
    BAK_UNIMPLEMENTED("POLYGON.ASM: blit_rect_buf_dispatch (no caller in the game)");
}

s16 draw_polygon(s16 count, s16 *xs, s16 *ys) {
    (void)count;
    (void)xs;
    (void)ys;
    BAK_UNIMPLEMENTED("POLYGON.ASM: draw_polygon");
}

void draw_polygon_edges(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: draw_polygon_edges");
}

void state_machine_enter_state2_a(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: state_machine_enter_state2_a");
}

void rasterize_polygon_edge(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: rasterize_polygon_edge");
}

void state_machine_enter_state2_b(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: state_machine_enter_state2_b");
}

void rasterize_polygon_edge_steep(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: rasterize_polygon_edge_steep");
}

void rasterize_polygon_edge_steep_b(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: rasterize_polygon_edge_steep_b");
}

void state_machine_disp_tbl_5d80(void) {
    BAK_UNIMPLEMENTED("POLYGON.ASM: state_machine_disp_tbl_5d80");
}
