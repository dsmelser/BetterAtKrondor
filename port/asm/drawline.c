/*
 * DRAWLINE.ASM in C: draw_line clips a line to the clip rectangle (when clipping is on)
 * and hands it to the driver's slot 2, left end first.
 *
 * The clip runs against ymin, xmin, ymax and xmax in turn: a line wholly outside one of
 * them is dropped, one crossing it has its outside end moved onto it by a 16-bit IMUL and
 * IDIV (through the divide-error handler's rule). The min tests compare signed, the max
 * tests unsigned, so a coordinate below 0 there counts as past the maximum.
 */
#include "../drivers/vga.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/RASTER/DRAWLINE.H"
#include "bak_game_end.h"

static void swap(u16 *a, u16 *b) {
    u16 t = *a;
    *a = *b;
    *b = t;
}

/* AX = (s16)num * (s16)k / (s16)den: IMUL, then IDIV. */
static u16 scaled(u16 num, u16 k, u16 den) {
    return (u16)bak_idiv16((s32)(s16)num * (s16)k, (s16)den, NULL);
}

void draw_line(s16 x1, s16 y1, s16 x2, s16 y2) {
    u16 bx = (u16)x1, cx = (u16)y1, si = (u16)x2, di = (u16)y2;
    if (g_graphics_context.bClip_enabled) {
        const ClipRect *c = &g_graphics_context.clip;
        u16 lim = (u16)c->ymin;
        if ((s16)cx < (s16)lim) {
            if ((s16)di < (s16)lim)
                return;
        } else if ((s16)di >= (s16)lim) {
            goto x_min;
        } else {
            swap(&bx, &si);
            swap(&cx, &di);
        }
        bx = (u16)(bx + scaled((u16)(si - bx), (u16)(lim - cx), (u16)(di - cx)));
        cx = lim;
    x_min:
        lim = (u16)c->xmin;
        if ((s16)bx < (s16)lim) {
            if ((s16)si < (s16)lim)
                return;
        } else if ((s16)si >= (s16)lim) {
            goto y_max;
        } else {
            swap(&bx, &si);
            swap(&cx, &di);
        }
        cx = (u16)(cx + scaled((u16)(di - cx), (u16)(lim - bx), (u16)(si - bx)));
        bx = lim;
    y_max:
        lim = (u16)c->ymax;
        if (cx > lim) {
            if (di > lim)
                return;
        } else if (di <= lim) {
            goto x_max;
        } else {
            swap(&bx, &si);
            swap(&cx, &di);
        }
        bx = (u16)(bx + scaled((u16)(si - bx), (u16)(lim - cx), (u16)(di - cx)));
        cx = lim;
    x_max:
        lim = (u16)c->xmax;
        if (bx > lim) {
            if (si > lim)
                return;
        } else if (si <= lim) {
            goto draw;
        } else {
            swap(&bx, &si);
            swap(&cx, &di);
        }
        cx = (u16)(cx + scaled((u16)(di - cx), (u16)(lim - bx), (u16)(si - bx)));
        bx = lim;
    }
draw:
    if (bx > si) {
        swap(&bx, &si);
        swap(&cx, &di);
    }
    ((DrawLineFn *)g_renderer_vtable.pfn_draw_line)(g_graphics_context.wGfxBlitDstPage, bx, cx, si,
                                                    di);
}
