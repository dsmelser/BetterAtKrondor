#include <dos.h>
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/FONT/FONT.H"
#include "structs.h"
#include "SRC/SYS/EMSIMG.H"
#include "SRC/GFX/RASTER/POLYRAST.H"
#include "SRC/SYS/DOSMEM.H"
#include "SRC/SYS/FARPTR.H"
#include "SRC/STREAM/RESLOAD/IMGLOAD.H"
#include "SRC/GFX/SPRITE/SPRTHNKS.H"

/* The original's blits of images whose table may sit in EMS (RESBLIT.C's storage modes 1 and
 * 2): each maps the table's page chain into the page frame and swaps the frame's segment in for
 * the chain's number for the call. Natively every table lives in plain memory, and these call
 * straight through. */

/* A table from RESBLIT.C, freed. The original gives one in EMS a fresh 16-byte block in place of
 * its chain, for free_image_record to free. */
void far emsimg_free_paged(void *pImgRecord) {
    free_image_record((ImageRecord **)pImgRecord);
}

void emsimg_polygon_quad_canonicalize(s16 *xs, s16 *ys) {
    s16 tmp;
    s16 i;
    s16 tmpY;

    if ((*xs == xs[1]) && (xs[2] == xs[3])) {
        tmp = *xs;
        tmpY = *ys;
        for (i = 1; i < 4; i++) {
            xs[i - 1] = xs[i];
            ys[i - 1] = ys[i];
        }
        xs[3] = tmp;
        ys[3] = tmpY;
    }
    if ((*xs == xs[3]) && (xs[1] == xs[2])) {
        if (*ys > ys[3]) {
            tmp = *xs;
            *xs = xs[3];
            xs[3] = tmp;
            tmp = *ys;
            *ys = ys[3];
            ys[3] = tmp;
        }
        if (ys[1] > ys[2]) {
            tmp = xs[1];
            xs[1] = xs[2];
            xs[2] = tmp;
            tmp = ys[1];
            ys[1] = ys[2];
            ys[2] = tmp;
        }
    }
}

void emsimg_gouraud_blit_paged(ImageRecord *texture, s16 *xs, s16 *ys) {
    emsimg_polygon_quad_canonicalize(xs, ys);
    polyrast_gouraud_blit_clipped(texture, xs, ys);
}

void emsimg_sprite_blit_scaled_paged(ImageRecord *sprite, s16 dst_x, s16 dst_y, u16 frame,
                                     s16 width, s16 height) {
    (*(void (*)(void *, s16, s16, u16, s16, s16))polyrast_spr_scaled_blit_planar)(
        sprite, dst_x, dst_y, frame, width, height);
}

void emsimg_putsprite_ems_swap(ImageRecord *sprite, s16 x, s16 y) {
    gfx_putsprite_fp(sprite, x, y);
}

void emsimg_map_then_call_180c(ImageRecord *sprite, s16 x, s16 y, u16 flip_flags) {
    blit_sprite_indirect((NearPtr)sprite, x, y, flip_flags);
}
