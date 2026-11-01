#include "SRC/GEN/GFXCTX.H"
#include "structs.h"
#include "SRC/STREAM/RESLOAD/FONTLOAD.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/SYS/FARTHUNK.H"
#include "SRC/GFX/FONT/FONT.H"
#include "SRC/SYS/DOSMEM.H"
#include "SRC/STREAM/RESLOAD/IFFREAD.H"
#include "SRC/STREAM/CODEC/STREAM.H"

NearPtr g_font_chunk_tag_ptr = (NearPtr)"FNT:";

s16 font_load(FileRef *filename) {
    s16 openedCache;
    s16 dataSize;
    s16 stream_id;
    s16 error_flag;
    void far *buf;
    void *malloc_result;
    s16 slot;

    for (slot = 2; *(s32 *)&g_font_bitmap_data[slot] && slot < 0x14; slot++)
        ;
    if (slot >= 0x14)
        goto full;

    if (is_file_cached(filename) == 0) {
        openedCache = 1;
        filename = cached_file_open(filename);
    } else {
        openedCache = 0;
    }

    if (chunk_seek(filename, (char *)g_font_chunk_tag_ptr, 0) == -1L)
        goto fail;

    res_fread(&g_graphics_context.pFont_glyph_width_bits[slot], 1, 1, filename);

    if ((char)g_graphics_context.pFont_glyph_width_bits[slot] == -3 ||
        (char)g_graphics_context.pFont_glyph_width_bits[slot] == -1) {

        g_font_format[slot] = -(char)g_graphics_context.pFont_glyph_width_bits[slot];
        res_fread(&g_graphics_context.pFont_glyph_width_bits[slot], 1, 1, filename);
        res_fread(&g_graphics_context.pFont_height[slot], 1, 1, filename);
        res_fread(&g_font_underline_offset[slot], 1, 1, filename);
        res_fread(&g_graphics_context.pFont_base_char[slot], 1, 1, filename);
        res_fread(&g_graphics_context.pFont_glyph_count[slot], 1, 1, filename);
        res_fread(&dataSize, 1, 2, filename);

        error_flag =
            ((stream_id = stream_open(-1, filename, "r", cached_file_chunk_size(filename))) < 0);

        if (error_flag == 0) {
            error_flag = ((u16)stream_size(stream_id) != (u16)dataSize);
        }

        if (error_flag == 0) {
            error_flag = ((buf = alloc_far((u32)(u16)dataSize, 0L)) == 0);
        }

        if (error_flag == 0) {
            error_flag = (stream_read(stream_id, buf, dataSize) != (u16)dataSize);
        }

        if (error_flag == 0) {
            g_font_glyph_offset_table[slot] = (s16 far *)buf;
            buf = (char *)buf + ((u16)g_graphics_context.pFont_glyph_count[slot] * 2);
            g_font_width_table[slot] = (unsigned char far *)buf;
            buf = (char *)buf + ((u16)g_graphics_context.pFont_glyph_count[slot]);
            g_font_bitmap_data[slot] = (unsigned char far *)buf;
        }

        stream_close(stream_id);
        if (error_flag == 0)
            goto epilogue;
        if (buf == NULL)
            goto comp_stub;
        _freemem(buf);
        goto comp_fail;
    comp_stub:;
    } else {

        if ((char)g_graphics_context.pFont_glyph_width_bits[slot] == -2) {
            g_font_format[slot] = 2;
            res_fread(&g_graphics_context.pFont_glyph_width_bits[slot], 1, 1, filename);
            dataSize = (u16)g_graphics_context.pFont_glyph_width_bits[slot];
        } else {
            g_font_format[slot] = 0;
            dataSize = ((s16)g_graphics_context.pFont_glyph_width_bits[slot] + 7) >> 3;
        }
        res_fread(&g_graphics_context.pFont_height[slot], 1, 1, filename);
        res_fread(&g_graphics_context.pFont_base_char[slot], 1, 1, filename);
        res_fread(&g_graphics_context.pFont_glyph_count[slot], 1, 1, filename);

        dataSize *= (s16)(unsigned char)g_graphics_context.pFont_height[slot] *
                    (s16)(unsigned char)g_graphics_context.pFont_glyph_count[slot];

        error_flag = !((malloc_result = my_malloc(dataSize)));

        if (error_flag == 0) {
            res_fread(malloc_result, dataSize, 1, filename);
        }

        if (error_flag == 0) {
            g_font_bitmap_data[slot] = (unsigned char far *)malloc_result;
            *(s32 *)&g_font_glyph_offset_table[slot] = 0L;
            *(s32 *)&g_font_width_table[slot] = 0L;
        }
        if (error_flag == 0) {
            goto epilogue;
        }
        if (malloc_result != 0) {
            my_free(malloc_result);
            goto uncomp_fail;
        }
    uncomp_stub:
    }
comp_fail:
uncomp_fail:
fail:
    slot = 0;
epilogue:
    if (openedCache != 0) {
        cached_file_close(filename);
    }
    goto done;
full:
    slot = 0;
done:
    return slot;
}

void font_unload(register s16 slot) {
    if (font_slot_in_use(slot)) {
        if (((void far *far *)g_font_bitmap_data)[slot] ==
            ((void far *far *)g_font_bitmap_data)[0]) {
            g_font_format[0] = 0;
            g_graphics_context.pFont_base_char[0] = g_graphics_context.pFont_glyph_count[0] = 0;
            g_graphics_context.pFont_glyph_width_bits[0] = g_graphics_context.pFont_height[0] =
                g_font_underline_offset[0] = 0;
            *(s32 *)&g_font_glyph_offset_table[0] = 0L;
            *(s32 *)&g_font_width_table[0] = 0L;
            *(s32 *)&g_font_bitmap_data[0] = 0L;
        }

        if (*(u32 *)&g_font_glyph_offset_table[slot]) {
            _freemem(((unsigned char far *far *)g_font_glyph_offset_table)[slot]);
        } else {
            /* An uncompressed font's bitmap is a near block: the original frees the far
               pointer's offset. */
            my_free((void *)g_font_bitmap_data[slot]);
        }

        g_font_format[slot] = 0;
        *(s32 *)&g_font_bitmap_data[slot] = 0L;
        *(s32 *)&g_font_glyph_offset_table[slot] = 0L;
        *(s32 *)&g_font_width_table[slot] = 0L;
    }
}
