#define unpack_2bpp_to_byte_per_pixel _proto_suppressed_unpack_2bpp
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/FONT/FONT.H"
#include "SRC/STREAM/CODEC/CODEC.H"
#include "structs.h"
#include "SRC/STREAM/RESLOAD/IMGLOAD.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/SYS/FARTHUNK.H"
#include "SRC/SYS/DOSMEM.H"
#include "SRC/SYS/FARPTR.H"
#include "SRC/STREAM/RESLOAD/IFFREAD.H"
#include "SRC/STREAM/CODEC/STREAM.H"
#undef unpack_2bpp_to_byte_per_pixel

void unpack_2bpp_to_byte_per_pixel(unsigned char huge *src, unsigned char huge *dst, u16 count);

s16 iff_chunk_loader_2f1c(ResFile *file, unsigned short *count, unsigned short *out_handle) {

    unsigned short *width;
    unsigned short *height;
    s16 i;
    unsigned short n;
    s16 *slot;
    unsigned short *buf;
    s16 rec;

    buf = 0;
    *out_handle = 0;
    if (chunk_seek(file, "BMP:INF:", 0) == -1L)
        goto L_fail;
    if (res_fread(count, 2, 1, file) != 1)
        goto L_fail;
    if (!(*out_handle = (unsigned short)my_calloc((*count + 1) * 2, 1)))
        goto L_cleanup;
    if (!(*(unsigned short *)*out_handle = (unsigned short)my_calloc(10, *count)))
        goto L_cleanup;
    if ((s32)(cached_file_chunk_size(file) + (-2L)) < (s32)(s16)(*count << 2))
        n = 1;
    else
        n = *count;
    if (!(buf = (unsigned short *)my_malloc(n << 2)))
        goto L_cleanup;
    if (res_fread(buf, n << 2, 1, file) != 1)
        goto L_cleanup;
    width = buf;
    height = buf + n;
    rec = *(s16 *)*out_handle;
    slot = (s16 *)*out_handle;
    for (i = 0; i < (s16)*count; i++) {
        *slot = rec;
        *(unsigned short *)(rec + 6) = *width;
        *(unsigned short *)(rec + 8) = *height;
        if (*count == n) {
            width++;
            height++;
        }
        rec += 10;
        slot++;
    }
    *slot = 0;
    my_free(buf);
    return 1;
L_cleanup:
    if (buf != 0)
        my_free(buf);
    if (*out_handle != 0) {
        if (*(s16 *)*out_handle != 0)
            my_free((void *)*(unsigned short *)*out_handle);
        my_free((void *)*out_handle);
    }
L_fail:
    return 0;
}

unsigned short bak_load_image_record_chunked(FileRef *fname) {
    unsigned short *image;
    s16 stream;
    unsigned char huge *buf;
    unsigned char huge *walk;
    unsigned char huge *aux;
    void *scratch;
    unsigned short record_count;
    s16 nread;
    s16 aux_size;
    s16 opened;
    s16 mode;
    s32 size;

    mode = 0;
    opened = 0;
    image = 0;
    aux = buf = 0;
    scratch = 0;
    stream = 0;

    if (!is_file_cached(fname)) {
        opened = 1;
        if ((u16)(fname = cached_file_open(fname)) < 0)
            goto cleanup;
    }
    if (iff_chunk_loader_2f1c(fname, &record_count, (unsigned short *)&image)) {
        size =
            (*(u32(far *)(unsigned short *, s16 *))g_renderer_vtable.pfn_image_install)(
                image, &aux_size);
        if (!(buf = alloc_far(size, 0L)))
            goto cleanup;
        if (aux_size != 0) {
            if (!(aux = alloc_far((s32)aux_size, 0L)))
                goto cleanup;
        }

        if (*(s32 *)&g_pCodecScratchFp == 0) {

            if ((scratch = my_malloc(0x3cc4)) != 0) {
                my_free(scratch);

                if ((scratch = my_malloc(0x3ac4)) != 0) {
                    g_pCodecScratchFp = (unsigned char huge *)scratch;
                    g_pCodecScratchFp += 0x10;
                    g_pCodecScratchFp = normalize_far_ptr_thunk(
                        (unsigned char far *)((u32)g_pCodecScratchFp & 0xfffffff0L));
                }
            }
        }
        if (chunk_seek(fname, "BMP:BIN:", 0) != -1L) {
            if ((stream = stream_open(0, fname, "r", cached_file_chunk_size(fname))) >= 0) {
                walk = buf;
                while (stream_read(stream, walk, 0x7fff) == 0x7fff)
                    walk += 0x7fff;
                (*(void(far *)(unsigned short *, unsigned char far *, u32,
                               unsigned char far *,
                               u32))g_renderer_vtable.pfn_image_decode)(
                    image, buf, stream_size(stream), aux, size);
                stream_close(stream);
                mode = 1;
                if (g_graphics_context.bGfxRenderStateFlag != 0) {
                    if (chunk_seek(fname, "BMP:VGA:", 0) != -1L)
                        mode = 5;
                    else if (chunk_seek(fname, "BMP:AMG:", 0) != -1L)
                        mode = 6;
                    if (mode >= 5) {
                        if ((stream = stream_open(0, fname, "r", cached_file_chunk_size(fname))) >=
                            0) {
                            size = 0x7fff;
                            while (!(aux = alloc_far(size, 0L)))
                                size >>= 1;
                            walk = buf;
                            while ((nread = stream_read(stream, aux, (u16)size)) > 0) {
                                if (mode == 6) {
                                    unpack_2bpp_to_byte_per_pixel(aux, aux, nread);
                                    nread <<= 2;
                                }
                                (*(void(far *)(
                                    unsigned char far *, unsigned char far *,
                                    unsigned short))g_renderer_vtable.pfn_unpack_nibbles_to_planes)(
                                    aux, walk, nread);
                                walk += (u32)size * 2;
                            }
                            stream_close(stream);
                        }
                    }
                }
            }
        }
    }
cleanup:
    if (aux != 0)
        _freemem(aux);
    if (scratch != 0) {
        my_free(scratch);
        g_pCodecScratchFp = 0;
    }
    if (mode == 0) {
        if (buf != 0)
            _freemem(buf);
        if (stream != 0)
            stream_close(stream);
        free_container_pair((s16 *)image);
        image = 0;
    }
    if (opened)
        cached_file_close(fname);
    return (unsigned short)image;
}

void free_container_pair(s16 *container) {
    if (*container != 0)
        my_free((void *)*container);
    if (container != 0)
        my_free(container);
}

void far free_image_record(register ImageRecord **record) {
    s16 *inner;
    if (record) {
        inner = (s16 *)*record;
        _freemem((unsigned char far *)(((u32)inner[0] << 16) + (u16)inner[1]));
        free_container_pair((s16 *)record);
    }
}

s16 null_terminated_count(ImageRecord **table) {
    register s16 i;

    i = 0;
    if (table != 0) {
        for (; table[i] != 0; i++) {
        }
    }
    return i;
}

void unpack_2bpp_to_byte_per_pixel(unsigned char huge *src, unsigned char huge *dst, u16 count) {
    unsigned char b;
    u16 si;

    src += count - 1;
    dst += count * 4 - 1;

    while (count--) {
        b = *src--;
        for (si = 1; si & 0xff; si <<= 1) {
            if (si & 0xaa) {
                *dst |= ((s16)(char)b & si) ? 0x10 : 0;
                dst--;
            } else {
                *dst = ((s16)(char)b & si) ? 1 : 0;
            }
        }
    }
}
