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
#include "SRC/SYS/EMS.H"

/* While an image's table sits in EMS (RESBLIT.C's storage modes 1 and 2) its pixels are out of
 * reach until a blit maps the table's page chain: the page frame then stands in the record for
 * the call. The original swaps the frame's segment in for the chain's number (its record's
 * first word) and back. */
static u16 emsimg_map(ImageRecord *rec) {
    u16 chain;
    unsigned char far *mapped;

    chain = rec->wEmsChain;
    if (chain != 0) {
        mapped = ems_map_resource_pages(chain);
        imgload_set_far(rec, FP_SEG(mapped), rec->wImageOff);
    }
    return chain;
}

static void emsimg_unmap(ImageRecord *rec, u16 chain) {
    if (chain != 0) {
        rec->pImageBase = NULL;
        rec->nImageExtent = 0;
        rec->wEmsChain = chain;
    }
}

/* A table from RESBLIT.C, freed: the original gives a table in EMS a fresh 16-byte block in
 * place of its chain, for free_image_record to free. */
void far emsimg_free_paged(void *pImgRecord) {
    void far *freshBlock;
    ImageRecord **list;

    list = (ImageRecord **)pImgRecord;

    if ((*list)->wEmsChain != 0) {
        ems_free_pages((*list)->wEmsChain);
        freshBlock = alloc_far(0x10L, 0L);
        imgload_set_far(*list, FP_SEG(freshBlock), (*list)->wImageOff);
    }
    free_image_record(list);
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
    u16 chain;

    chain = emsimg_map(texture);
    emsimg_polygon_quad_canonicalize(xs, ys);
    polyrast_gouraud_blit_clipped(texture, xs, ys);
    emsimg_unmap(texture, chain);
}

void emsimg_sprite_blit_scaled_paged(ImageRecord *sprite, s16 dst_x, s16 dst_y, u16 frame,
                                     s16 width, s16 height) {
    u16 chain;

    chain = emsimg_map(sprite);
    (*(void (*)(void *, s16, s16, u16, s16, s16))polyrast_spr_scaled_blit_planar)(
        sprite, dst_x, dst_y, frame, width, height);
    emsimg_unmap(sprite, chain);
}

void emsimg_putsprite_ems_swap(ImageRecord *sprite, s16 x, s16 y) {
    u16 chain;

    chain = emsimg_map(sprite);
    gfx_putsprite_fp(sprite, x, y);
    emsimg_unmap(sprite, chain);
}

void emsimg_map_then_call_180c(ImageRecord *sprite, s16 x, s16 y, u16 flip_flags) {
    u16 chain;

    chain = emsimg_map(sprite);
    blit_sprite_indirect((NearPtr)sprite, x, y, flip_flags);
    emsimg_unmap(sprite, chain);
}
