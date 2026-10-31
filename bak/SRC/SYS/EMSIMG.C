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

ImageRecord **emsimg_migrate_to_ems(ImageRecord **image_record_list) {
    u32 sizeBytes;
    s16 i;
    s16 page_id;
    unsigned char far *firstbuf;
    unsigned char far *dst;
    u32 linear_base;
    u32 linear_cur;

    sizeBytes = (*(u32(far *)(u16 *,
                                        u16 *))g_renderer_vtable.pfn_image_install)(
        (u16 *)image_record_list, (u16 *)&i);

    if ((page_id = ems_alloc_pages(sizeBytes)) != 0) {

        dst = ems_map_resource_pages(page_id);

        firstbuf =
            (unsigned char far *)MK_FP(image_record_list[0]->wImageData, image_record_list[0]->wImageOff);

        fmemcpy_far((u32)dst, (u32)firstbuf, (u16)sizeBytes);

        linear_base =
            ((u32)image_record_list[0]->wImageData << 4) | image_record_list[0]->wImageOff;

        i = null_terminated_count(image_record_list) - 1;
        while (i >= 0) {

            linear_cur =
                ((u32)image_record_list[i]->wImageData << 4) | image_record_list[i]->wImageOff;

            image_record_list[i]->wImageData = page_id;
            image_record_list[i]->wImageOff = linear_cur - linear_base;
            i--;
        }

        _freemem(firstbuf);
        return image_record_list;

    } else {
        return image_record_list;
    }
}

s16 *far emsimg_load_to_ems(char *fname) {
    s16 *image_record_list;

    image_record_list = (s16 *)bak_load_image_record_chunked(fname);
    if (image_record_list != (s16 *)0x0) {
        emsimg_migrate_to_ems((ImageRecord **)image_record_list);
    }
    return image_record_list;
}

void far emsimg_free_paged(void *pImgRecord) {
    void far *freshBlock;
    ImageRecord **list;

    list = (ImageRecord **)pImgRecord;

    if ((*list)->wImageData < 300) {
        ems_free_pages((*list)->wImageData);
        freshBlock = alloc_far(0x10L, 0L);
        (*list)->wImageData = FP_SEG(freshBlock);
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

void emsimg_gouraud_blit_paged(u16 *page_ptr, s16 *xs, s16 *ys) {
    u16 page_id;
    unsigned char far *mapped;

    page_id = *page_ptr;
    if (page_id < 300) {
        mapped = ems_map_resource_pages(page_id);
        *page_ptr = FP_SEG(mapped);
    }
    emsimg_polygon_quad_canonicalize(xs, ys);
    polyrast_gouraud_blit_clipped(page_ptr, xs, ys);
    *page_ptr = page_id;
}

void emsimg_sprite_blit_scaled_paged(ImageRecord *sprite, s16 dst_x, s16 dst_y, u16 frame,
                                     s16 width, s16 height) {
    u16 page_id;
    unsigned char far *mapped;

    page_id = sprite->wImageData;
    if (page_id < 300) {
        mapped = ems_map_resource_pages(page_id);
        sprite->wImageData = FP_SEG(mapped);
    }

    (*(void (*)(void *, s16, s16, u16, s16, s16))polyrast_spr_scaled_blit_planar)(
        sprite, dst_x, dst_y, frame, width, height);
    sprite->wImageData = page_id;
}

void emsimg_putsprite_ems_swap(u16 *img_ptr, s16 x, s16 y) {
    u16 page_id;
    unsigned char far *mapped;

    page_id = *img_ptr;
    if (page_id < 300) {
        mapped = ems_map_resource_pages(page_id);
        *img_ptr = FP_SEG(mapped);
    }
    gfx_putsprite_fp((ImageRecord *)img_ptr, x, y);
    *img_ptr = page_id;
}

void emsimg_map_then_call_180c(u16 *p_id_or_ptr, s16 x, s16 y, u16 flip_flags) {
    u16 page_id;
    unsigned char far *mapped;

    page_id = *p_id_or_ptr;
    if (page_id < 300) {
        mapped = ems_map_resource_pages(page_id);
        *p_id_or_ptr = FP_SEG(mapped);
    }
    blit_sprite_indirect((s16)p_id_or_ptr, x, y, flip_flags);
    *p_id_or_ptr = page_id;
}
