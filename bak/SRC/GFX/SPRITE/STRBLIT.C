#include "SRC/GEN/GFXCTX.H"
#include "SRC/INPUT/KEYBOARD.H"
#include "SRC/GFX/SPRITE/ROTBLIT.H"
#include "SRC/GFX/SPRITE/SPRTHNKS.H"
#include "structs.h"
#include "SRC/GFX/SPRITE/STRBLIT.H"
#include "SRC/GFX/RASTER/VGARESET.H"
#include <conio.h>

/* The step from the first source pixel (pos's high word) to the last (step's) over dest_steps
 * destination pixels, and where the walk starts: half a pixel in when a step is under half a
 * pixel (a stretch), else the step's fraction. */
s16 stretch_step_init(register StretchStep *step, register s16 dest_steps) {
    char negate = 0;

    if (dest_steps <= 0) {
        step->w[3] = 0;
        step->w[2] = 0;
        return (step->w[0] = 0);
    }
    step->w[0] = 0;
    step->w[2] = 0;
    step->l[1] -= step->l[0];
    step->l[1] = step->l[1] / (s32)dest_steps;
    if (step->l[1] < 0) {
        step->l[1] = -step->l[1];
        negate = 1;
    }
    if (((u32)step->l[1] & 0xffff8000UL) == 0)
        step->w[0] = 0x8000;
    else
        step->w[0] = step->w[2];
    if (negate)
        step->l[1] = -step->l[1];
    return 1;
}

/* A sprite drawn dst_w by dst_h pixels (a negative size mirrors it), clipped: each row's
 * pixels come from the source row and columns the two stretch tables name, drawn by the
 * driver's slot 37 (the original called the slot with its registers; natively through
 * SPRTHNKS's thunk). */
void blit_image_stretched(ImageRecord *image, s16 dst_x, s16 dst_y, u16 flags, s16 dst_w,
                          s16 dst_h) {
    const unsigned char *src;
    s16 i;
    s16 row;
    s16 rowStride;
    s16 dstRight;
    s16 dstBottom;
    s16 srcY;
    s16 local_16;
    s16 local_18;
    s16 dstLeft;
    StretchStep step;

    if (dst_w < 0) {
        dst_w = (s16)-dst_w;
        flags |= 2;
    }
    if (dst_h < 0) {
        dst_h = (s16)-dst_h;
        flags |= 1;
    }

    dstRight = (dst_w < 0x280) ? dst_w : 0x280;
    dstBottom = (dst_h < 0x190) ? dst_h : 0x190;

    if (flags & 2) {
        step.w[1] = image->nWidth - 1;
        step.w[3] = 0;
    } else {
        step.w[1] = 0;
        step.w[3] = image->nWidth - 1;
    }
    stretch_step_init(&step, dst_w - 1);
    for (i = 0; i < dstRight; i++) {
        g_aStretchXSrcTable[i] = step.w[1];
        step.l[0] += step.l[1];
    }
    g_aStretchXSrcTable[i]++;

    step.w[1] = 0;
    step.w[3] = image->nHeight - 1;
    stretch_step_init(&step, dst_h - 1);
    rowStride = image->nWidth >> g_abAdapterRowStrideShift[(char)g_graphics_context.bVideoAdapter];
    row = local_16 = local_18 = 0;
    for (; row < dstBottom; row++) {
        srcY = step.w[1];
        step.l[0] += step.l[1];
        while (srcY > local_16) {
            local_16++;
            local_18 += rowStride;
        }
        if (flags & 1)
            g_aStretchYSrcTable[dstBottom - row - 1] = local_18;
        else
            g_aStretchYSrcTable[row] = local_18;
    }

    dstBottom += dst_y;
    dstRight += dst_x;
    local_18 = dst_y;
    dstLeft = dst_x;
    local_16 = 0;
    if (g_graphics_context.bClip_enabled != 0) {
        if (dstRight > g_graphics_context.clip.xmax)
            dstRight -= dstRight - g_graphics_context.clip.xmax - 1;
        if (dstBottom > g_graphics_context.clip.ymax)
            dstBottom -= dstBottom - g_graphics_context.clip.ymax - 1;
        if (local_18 < g_graphics_context.clip.ymin)
            local_18 = g_graphics_context.clip.ymin;
        if (dstLeft < g_graphics_context.clip.xmin) {
            local_16 = g_graphics_context.clip.xmin - dstLeft;
            dstLeft = g_graphics_context.clip.xmin;
        }
    }

    src = image->pImageBase + image->wImageOff;
    if (0 < dstBottom - local_18 && 1 < dstRight - dstLeft) {
        if (g_graphics_context.bActiveVgaMode == 0x10) {
            outpw(0x3ce, 1);
            outpw(0x3ce, 5);
            outp(0x3ce, 8);
        }
        for (row = local_18; row < dstBottom; row++) {
            blit_stretched_dispatch(g_wGfxBlitDstPage, g_pRow_offset_lut[row],
                                    src + (u16)g_aStretchYSrcTable[row - dst_y],
                                    &g_aStretchXSrcTable[local_16], (u16)(dstRight - dstLeft),
                                    (u16)dstLeft);
        }
        vga_reset_gc_default();
    }
}

void blit_image_scaled_centered(register ImageRecord *image, s16 cx, s16 cy, u32 scale_q16) {
    register s16 scale_hi;
    s16 dst_w;
    s16 dst_h;

    scale_hi = (s16)(scale_q16 >> 16);
    dst_w = (s16)((s32)image->nWidth * (s32)scale_hi >> 10);
    dst_h = (s16)((s32)image->nHeight * (s32)scale_hi >> 10);
    blit_image_stretched(image, cx - (dst_w >> 1), cy - (dst_h >> 1), (u16)scale_q16, dst_w,
                         dst_h);
    return;
}
