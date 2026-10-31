#include "SRC/GEN/GFXCTX.H"
#include "SRC/COMBAT/SPELL/CSPELL.H"
#include "SRC/COMBAT/STATS/CBSTAT.H"
#include "structs.h"
#include "SRC/COMBAT/SPELL/HEXANIM.H"
#include "SRC/GFX/RASTER/DRAWLINE.H"
#include "SRC/GFX/DRIVER/VIDINIT.H"
#include "SRC/GFX/SCREEN/SCREEN.H"

static void far hexanim_draw_hexagon_outline(s16 *xs, s16 *ys, s16 color) {
    s16 i;

    g_graphics_context.bGfx_outline_color = color;
    i = 0;
    do {
        draw_line(xs[i], ys[i], xs[i + 1], ys[i + 1]);
        i = i + 1;
    } while (i < 5);
    draw_line(*xs, *ys, xs[5], ys[5]);
}

void far hexanim_move_tiles(s16 srcSchool, s16 dstSchool) {
    s16 divisor;
    s16 outer;
    s16 *ptr_ax;
    s16 *ptr_ay;
    s16 *ptr_bx;
    s16 *ptr_by;
    s16 curX[6];
    s16 curY[6];
    s16 stepX[6];
    s16 stepY[6];
    s16 x_anim[7][6];
    s16 y_anim[7][6];
    register s16 j;
    s16 k;

    ptr_ax = g_apHexVertexX[srcSchool];
    ptr_ay = g_apHexVertexY[srcSchool];
    ptr_bx = g_apHexVertexX[dstSchool];
    ptr_by = g_apHexVertexY[dstSchool];
    outer = 0;
    do {
        curX[outer] = ptr_ax[outer];
        curY[outer] = ptr_ay[outer];
        j = 0;
        do {
            x_anim[j][outer] = curX[outer];
            y_anim[j][outer] = curY[outer];
            j = j + 1;
        } while (j < 7);
        outer++;
    } while (outer < 6);
    if (srcSchool != dstSchool) {
        outer = 0;
        do {
            divisor = 0x1e - outer;
            j = 0;
            do {
                if (0 < divisor) {
                    stepX[j] = (ptr_bx[j] - curX[j]) / divisor;
                    stepY[j] = (ptr_by[j] - curY[j]) / divisor;
                }
                for (k = 1; k < 7; k = k + 1) {
                    x_anim[k - 1][j] = x_anim[k][j];
                    y_anim[k - 1][j] = y_anim[k][j];
                }
                x_anim[6][j] = curX[j];
                y_anim[6][j] = curY[j];
                j = j + 1;
            } while (j < 6);
            g_graphics_context.wGfxBlitDstPage = g_graphics_context.wVgaPage2Base;
            cga_rect_paste_from_buffer(g_pSpellScreenBuffer, 0x12, 0xf, 0x6f, 0x5d);
            j = 0;
            do {
                hexanim_draw_hexagon_outline(x_anim[j], y_anim[j], j + 0x83);
                j = j + 1;
            } while (j < 7);
            if (outer < 0x1e) {
                j = 0;
                do {
                    curX[j] += stepX[j];
                    curY[j] += stepY[j];
                    j = j + 1;
                } while (j < 6);
            }
            screen_frame_present();
            outer++;
        } while (outer < 0x25);
    } else {
        g_graphics_context.wGfxBlitDstPage = g_graphics_context.wVgaPage2Base;
        cga_rect_paste_from_buffer(g_pSpellScreenBuffer, 0x12, 0xf, 0x6f, 0x5d);
        hexanim_draw_hexagon_outline(ptr_bx, ptr_by, 0x89);
        screen_frame_present();
    }
    g_graphics_context.wGfxBlitDstPage = g_graphics_context.wVgaPage2Base;
    cga_rect_paste_from_buffer(g_pSpellScreenBuffer, 0x12, 0xf, 0x6f, 0x5d);
    hexanim_draw_hexagon_outline(ptr_bx, ptr_by, 0x89);
    screen_frame_present();
}
