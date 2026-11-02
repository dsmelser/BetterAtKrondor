/*
 * The native Mode-Y video driver: DRIVERS/VMCODE/VGA.ASM in C.
 *
 * The original is a chunk of VMCODE.OVL that video_init loads and calls; it programs
 * the VGA through its ports and planar memory. This driver does the same work on the
 * chunky display-memory model of bak_video.h: a planar byte (segment:offset, plane p)
 * is element p of the four-pixel group at that address, so every routine keeps the
 * original's addressing, its 16-bit offset wraparound and its byte granularity. Only
 * the DAC and the CRTC start address go through the emulated registers, since game
 * code reads those back and presenting depends on them.
 *
 * Routines are ported as the boot path reaches them; until then their slots stop
 * the game with the routine's name. Slots the original leaves empty are no-ops.
 */
#include <string.h>

#include "bak_video.h"
#include "vga.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GFX/RASTER/POLYFILL.H"
#include "bak_game_end.h"

#define PLANAR_ROW 80    /* bytes per row in a plane */
#define ROW_LUT_AT 0x6f2 /* offset of the row-offset LUT in the graphics-context block */

/* Rect-buffer source offset kept by vga_set_buffer_offset for vga_blit_rect_buffer. */
static u16 s_rectBufferOffset;

/* Sprite-write cache word that vga_clear_rect_state resets (cs:222Fh in the original). */
static u16 s_spriteCache;

/* Display memory where the CPU reaches it at seg:off: one planar byte, i.e. the four
 * pixels of planes 0-3. NULL outside the 64 KB VGA window at A000:0000. */
static unsigned char *vram(u16 seg, u16 off) {
    u32 linear = ((u32)seg << 4) + off;
    if (linear - 0xA0000u >= 0x10000u)
        return NULL;
    return &g_bakVram[(linear - 0xA0000u) * 4u];
}

static void latch_load(u16 seg, u16 off) {
    unsigned char *p = vram(seg, off);
    if (p != NULL)
        memcpy(g_bakVgaLatch, p, 4);
}

static void latch_store(u16 seg, u16 off) {
    unsigned char *p = vram(seg, off);
    if (p != NULL)
        memcpy(p, g_bakVgaLatch, 4);
}

void vga_write_latches(u16 seg, u16 off, unsigned mapMask) {
    unsigned char *p = vram(seg, off);
    if (p == NULL)
        return;
    for (unsigned plane = 0; plane < 4; plane++)
        if (mapMask & (1u << plane))
            p[plane] = g_bakVgaLatch[plane];
}

void vga_read_latches(u16 seg, u16 off) {
    latch_load(seg, off);
}

/* The row-offset LUT entry for row y, read as the original does: the word at 0x6f2 + 2y
 * (a 16-bit address) in the graphics-context block, so rows past the table read its
 * neighbours. */
static u16 row_offset(s16 y) {
    u16 at = (u16)(ROW_LUT_AT + 2 * (u16)y);
    if (at + 2u > sizeof g_gfxContextBlock)
        return 0; /* the original reads other DGROUP data there */
    const unsigned char *b = (const unsigned char *)&g_gfxContextBlock + at;
    return (u16)(b[0] | b[1] << 8);
}

u16 vga_row_offset(s16 y) {
    return row_offset(y);
}

/* ---- init ----------------------------------------------------------------------- */

/* INT 10h mode 13h, then Mode Y: chain-4 and odd/even off, byte addressing, all four
 * planes of display memory cleared. The register changes are the original's. */
static void set_mode_y(void) {
    bak_int10_set_mode(0x13);
    bak_port_out8(0x3C4, 4);
    bak_port_out8(0x3C5, 0x06);
    bak_port_out8(0x3CE, 6);
    bak_port_out8(0x3CF, 0x05);
    bak_port_out16(0x3C4, 0x0F02);
    memset(g_bakVram, 0, sizeof g_bakVram);
    bak_port_out8(0x3D4, 0x14);
    bak_port_out8(0x3D5, 0x00);
    bak_port_out8(0x3D4, 0x17);
    bak_port_out8(0x3D5, 0xE3);
    bak_port_out16(0x3D4, 0x000C); /* mode 13h starts scanning at address 0 */
    bak_port_out16(0x3D4, 0x000D);
    bak_port_out16(0x3CE, 0x4005);
    bak_port_out16(0x3CE, 0x0100);
    bak_port_out16(0x3CE, 0xFF08);
}

static void vga_noop(void) {
}

#define TRAP(slot, name)                                                                           \
    static void trap_##name(void) {                                                                \
        BAK_UNIMPLEMENTED("VGA driver slot " #slot ": vga_" #name);                                \
    }
TRAP(17, blit_chunky)
TRAP(18, blit_chunky_planar_4bit)
TRAP(19, fill_textured_span_modey_b)
TRAP(23, blit_rect_buffer)
TRAP(30, blit_sprite_flip_h)
TRAP(31, blit_sprite_flip_v)
TRAP(36, blit_sprite_planar_rotated)
TRAP(37, blit_stretched)
TRAP(38, blit_sprite_chunky_flip)
TRAP(40, fill_textured_span_modey)

static void vga_clear_rect_state(void) {
    s_spriteCache = 0;
}

static RendererVtable s_slots;

#define SLOT(fn) ((void *)(fn))

static void fill_slots(void) {
    RendererVtable *t = &s_slots;
    void *noop = SLOT(vga_noop);
    t->slot_00_null = NULL;
    t->pfn_blit_glyph_row = SLOT(vga_blit_glyph_row);
    t->pfn_draw_line = SLOT(vga_draw_line);
    t->pfn_blit_sprite = SLOT(vga_blit_sprite);
    t->pfn_present = SLOT(vga_present);
    t->pfn_save_rect = SLOT(vga_save_rect);
    t->pfn_rect_byte_size = SLOT(vga_blit_size);
    t->pfn_paste_rect = SLOT(vga_paste_rect);
    t->pfn_vsync_wait = SLOT(vga_vsync_wait);
    t->slot_09_unimplemented = noop;
    t->pfn_fill_run_a = SLOT(vga_fill_run_a);
    t->pfn_fill_run_b = SLOT(vga_fill_run_b);
    t->slot_12_unimplemented = noop;
    t->pfn_image_install = SLOT(vga_image_install);
    t->pfn_image_decode = SLOT(vga_image_decode);
    t->pfn_unpack_nibbles_to_planes = SLOT(vga_unpack_nibbles_to_planes);
    t->slot_16_unimplemented = noop;
    t->pfn_blit_chunky = SLOT(trap_blit_chunky);
    t->pfn_blit_chunky_planar_4bit = SLOT(trap_blit_chunky_planar_4bit);
    t->pfn_fill_textured_span_modey_b = SLOT(trap_fill_textured_span_modey_b);
    t->pfn_palette_set = SLOT(vga_palette_set);
    t->pfn_getpixel = SLOT(vga_getpixel);
    t->pfn_putpixel = SLOT(vga_putpixel);
    t->pfn_blit_rect_buffer = SLOT(trap_blit_rect_buffer);
    t->pfn_set_buffer_offset = SLOT(vga_set_buffer_offset);
    t->pPfn_set_border_color = noop;
    t->slot_26_unimplemented = noop;
    t->pfn_fill_spans_dithered_modey = SLOT(vga_fill_spans_dithered_modey);
    t->slot_28_unimplemented = noop;
    t->pfn_blit_sprite_master = SLOT(vga_blit_sprite_master);
    t->pfn_blit_sprite_flip_h = SLOT(trap_blit_sprite_flip_h);
    t->pfn_blit_sprite_flip_v = SLOT(trap_blit_sprite_flip_v);
    t->pfn_clear_rect_state = SLOT(vga_clear_rect_state);
    t->pfn_blit_sprite_simple = SLOT(vga_blit_sprite_simple);
    t->pfn_palette_lerp_dac = SLOT(vga_palette_lerp_dac);
    t->pfn_dac_write_palette = SLOT(vga_dac_write_palette);
    t->pfn_blit_sprite_planar_rotated = SLOT(trap_blit_sprite_planar_rotated);
    t->pfn_blit_stretched = SLOT(trap_blit_stretched);
    t->pfn_blit_sprite_chunky_flip = SLOT(trap_blit_sprite_chunky_flip);
    t->pfn_fill_world_textured_span_modey = SLOT(vga_fill_world_textured_span_modey);
    t->pfn_fill_textured_span_modey = SLOT(trap_fill_textured_span_modey);
    t->slot_41_unimplemented = noop;
    t->slot_42_unimplemented = noop;
    t->slot_43_unimplemented = noop;
    t->slot_44_unimplemented = noop;
    t->slot_45_unimplemented = noop;
    t->slot_46_unimplemented = noop;
    t->slot_47_unimplemented = noop;
    t->slot_48_unimplemented = noop;
    t->slot_49_null = NULL;
}

const RendererVtable *vga_driver_init(void) {
    g_graphics_context.bActiveVgaMode = 0xff;
    g_graphics_context.bGfxRenderStateFlag++;
    if (g_graphics_context_render.bVideoAutoDetectEnabled != 0)
        set_mode_y();
    g_graphics_context.wVgaFrontPageBase = 0xA000;
    g_graphics_context.wVgaPage2Base = 0xA400;
    g_graphics_context.wVgaPage1Base = 0xA800;
    if (g_wScreen_height == 400)
        BAK_UNIMPLEMENTED("VGA driver: the 400-line mode");
    /* Row k of a page starts k * 80 bytes into each plane. */
    u16 rows = g_wScreen_height;
    for (u16 k = 0; k < rows && k < 200; k++)
        g_pRow_offset_lut[k] = (u16)(k * PLANAR_ROW);
    g_graphics_context.bVideoAdapter = 8;
    fill_slots();
    return &s_slots;
}

/* ---- slot 1: text --------------------------------------------------------------- */

/* One pass of the glyph blit: 8 pixels per row starting at pixel `shift` of planar byte
 * di, from the glyph bits (NULL: a solid cell), rows 80 bytes apart. */
static void glyph_pass(u16 seg, u16 di, unsigned shift, u16 rows, const unsigned char *glyph,
                       unsigned char color) {
    do {
        unsigned bits = glyph != NULL ? *glyph++ : 0xFFu;
        for (unsigned b = 0; b < 8; b++) {
            if (bits & (0x80u >> b)) {
                unsigned k = shift + b;
                unsigned char *p = vram(seg, (u16)(di + (k >> 2)));
                if (p != NULL)
                    p[k & 3] = color;
            }
        }
        di = (u16)(di + PLANAR_ROW);
    } while (--rows != 0);
}

/* An 8-pixel-wide glyph at (x, y) on the destination page, one byte per row, in the
 * text colour; with style flags 0 the cell is first filled with the background colour. */
void vga_blit_glyph_row(s16 y, s16 x, s16 rows, const unsigned char *glyph) {
    u16 seg = g_graphics_context.wGfxBlitDstPage;
    u16 di = (u16)(row_offset(y) + ((u16)x >> 2));
    unsigned shift = (u16)x & 3;
    if (g_graphics_context.bText_style_flags == 0)
        glyph_pass(seg, di, shift, (u16)rows, NULL, g_graphics_context.bText_bg_color);
    glyph_pass(seg, di, shift, (u16)rows, glyph, g_graphics_context.bText_fg_color);
}

/* ---- slots 4-8: page copies, rectangles, flips -------------------------------- */

/* Copy a rectangle from the source page to the destination page through the latches,
 * whole planar bytes (four pixels) at a time. */
void vga_present(s16 x, s16 y, s16 width, s16 height) {
    u16 src = g_graphics_context.wGfxBlitSrcPage;
    u16 dst = g_graphics_context.wGfxBlitDstPage;
    u16 bytes = (u16)((u16)(((u16)x & 3) + (u16)width + 3) >> 2);
    u16 off = (u16)(row_offset(y) + ((u16)x >> 2));
    u16 rows = (u16)height;
    do {
        u16 o = off;
        for (u16 n = bytes; n != 0; n--, o++) {
            latch_load(src, o);
            latch_store(dst, o);
        }
        off = (u16)(off + PLANAR_ROW);
    } while (--rows != 0);
}

/*
 * The four plane passes of a rectangle row: pass k moves the pixels i = k, k+4, ...
 * of the row, which share plane (x + k) & 3 and start at planar byte x/4 + (x&3 + k)/4.
 * Pass 0 always runs (the original has no zero check there: a row 0 pixels wide makes
 * its LOOP run 65536 times; natively such a row does nothing); passes 1-3 stop the row
 * once one has nothing to move, as in the original.
 */
static u16 pass_count(s16 width, unsigned k) {
    return (u16)((u16)((u16)width + 3 - k) >> 2);
}

/* Copy a rectangle of the source page into a buffer, row-major, width bytes per row. */
void vga_save_rect(unsigned char *dst, s16 x, s16 y, s16 width, s16 height) {
    u16 seg = g_graphics_context.wGfxBlitSrcPage;
    u16 rowOff = (u16)(row_offset(y) + ((u16)x >> 2));
    unsigned plane0 = (u16)x & 3;
    u16 rows = (u16)height;
    do {
        u16 byte = rowOff;
        unsigned plane = plane0;
        for (unsigned k = 0; k < 4; k++) {
            u16 n = pass_count(width, k);
            if (n == 0)
                break;
            u16 o = byte;
            unsigned char *d = dst + k;
            for (; n != 0; n--, o++, d += 4) {
                unsigned char *p = vram(seg, o);
                *d = p != NULL ? p[plane] : 0xFF;
            }
            if (++plane == 4) {
                plane = 0;
                byte++;
            }
        }
        dst += (u16)width;
        rowOff = (u16)(rowOff + PLANAR_ROW);
    } while (--rows != 0);
}

s32 vga_blit_size(s16 width, s16 height) {
    return (s32)((u32)(u16)width * (u16)height);
}

/* Copy a buffer (row-major, width bytes per row) into a rectangle of the destination page. */
void vga_paste_rect(unsigned char *src, s16 x, s16 y, s16 width, s16 height) {
    u16 seg = g_graphics_context.wGfxBlitDstPage;
    u16 rowOff = (u16)(row_offset(y) + ((u16)x >> 2));
    unsigned plane0 = (u16)x & 3;
    u16 rows = (u16)height;
    do {
        u16 byte = rowOff;
        unsigned plane = plane0;
        for (unsigned k = 0; k < 4; k++) {
            u16 n = pass_count(width, k);
            if (n == 0)
                break;
            u16 o = byte;
            const unsigned char *s = src + k;
            for (; n != 0; n--, o++, s += 4) {
                unsigned char *p = vram(seg, o);
                if (p != NULL)
                    p[plane] = *s;
            }
            if (++plane == 4) {
                plane = 0;
                byte++;
            }
        }
        src += (u16)width;
        rowOff = (u16)(rowOff + PLANAR_ROW);
    } while (--rows != 0);
}

/* Page flip: the back page becomes the front page and the CRTC starts scanning it;
 * with wait set, return at the start of the next vertical retrace. */
void vga_vsync_wait(s16 wait) {
    u16 back = g_graphics_context.wVgaPage2Base;
    g_graphics_context.wVgaPage2Base = g_graphics_context.wVgaFrontPageBase;
    g_graphics_context.wVgaFrontPageBase = back;
    u16 start = (u16)(back << 4);
    bak_port_out16(0x3D4, (u16)(0x0C | (start & 0xFF00)));
    bak_port_out16(0x3D4, (u16)(0x0D | (start & 0x00FF) << 8));
    if (wait != 0)
        bak_vga_wait_retrace(1);
}

/* ---- slots 13-15: images --------------------------------------------------------- */

/* The bytes a list of images needs once decoded (the low 16 bits of each image's
 * width x height, summed); no auxiliary buffer. */
u32 vga_image_install(ImageRecord **images, s16 *auxSize) {
    u32 total = 0;
    for (; *images != NULL; images++)
        total += (u16)((u16)(*images)->nWidth * (u16)(*images)->nHeight);
    *auxSize = 0;
    return total;
}

/*
 * Give each image its place in the decoded buffer (a normalized far pointer: segment,
 * offset 0-15) and expand the packed data, two pixels per byte, high nibble first, in
 * place from the end. The buffer is a block of plain memory, from its start (IMGLOAD.C), and
 * total, an argument for other adapters' drivers in the original, its size.
 */
void vga_image_decode(ImageRecord **images, unsigned char *data, u32 packedSize, unsigned char *aux,
                      u32 total) {
    (void)aux;
    u32 size = (total + 15) & ~15u;
    u32 at = 0;
    for (; *images != NULL; images++) {
        u32 paragraph = at & ~15u;
        (*images)->pImageBase = data + paragraph;
        (*images)->nImageExtent = size > paragraph ? size - paragraph : 0;
        (*images)->wImageOff = (u16)(at & 15);
        at += (u16)((u16)(*images)->nWidth * (u16)(*images)->nHeight);
    }
    if (packedSize == 0)
        return; /* the original would loop 2^32 times */
    if ((packedSize & 0xFFFF) == 0)
        BAK_UNIMPLEMENTED("vga_image_decode: packed size a multiple of 64 KB (the original "
                          "computes the wrong destination there)");
    for (u32 i = packedSize; i-- > 0;) {
        unsigned char b = data[i];
        data[2 * i] = (unsigned char)(b >> 4);
        data[2 * i + 1] = (unsigned char)(b & 0x0F);
    }
}

/* Supply the high nibbles of count pixel pairs (the BMP:VGA chunk of 256-colour images). */
void vga_unpack_nibbles_to_planes(unsigned char *src, unsigned char *dst, u16 count) {
    u32 n = count != 0 ? count : 0x10000u; /* LOOP with CX = 0 runs 65536 times */
    for (u32 i = 0; i < n; i++) {
        unsigned char b = src[i];
        dst[2 * i] |= (unsigned char)(b & 0xF0);
        dst[2 * i + 1] |= (unsigned char)(b << 4);
    }
}

/* ---- slots 20-24: palette, pixels -------------------------------------------------- */

/* Load a 256-colour palette into the DAC and into both halves of the palette cache. */
void vga_palette_set(unsigned char *palette) {
    unsigned char *cache = g_graphics_context.pPaletteScratchBuf;
    if (palette == NULL)
        return;
    vga_dac_write_palette(palette, 0, 0x100);
    if (cache == NULL)
        BAK_UNIMPLEMENTED("vga_palette_set without a palette cache (the original writes to "
                          "0000:0000)");
    memcpy(cache, palette, 0x300);
    memcpy(cache + 0x300, palette, 0x300);
}

u16 vga_getpixel(s16 x, s16 y) {
    u16 off = (u16)(row_offset(y) + ((u16)x >> 2));
    unsigned char *p = vram(g_graphics_context.wGfxBlitSrcPage, off);
    return p != NULL ? p[(u16)x & 3] : 0xFF;
}

void vga_putpixel(s16 x, s16 y, s16 color) {
    u16 off = (u16)(row_offset(y) + ((u16)x >> 2));
    unsigned char *p = vram(g_graphics_context.wGfxBlitDstPage, off);
    if (p != NULL)
        p[(u16)x & 3] = (unsigned char)color;
}

void vga_set_buffer_offset(s16 x, s16 y) {
    s_rectBufferOffset = (u16)((u16)y * PLANAR_ROW + (x >> 2));
}

/* ---- slots 2, 10, 11, 27: lines and runs ------------------------------------------ */

static const unsigned char s_runLeftMask[4] = {0x0f, 0x0e, 0x0c, 0x08};
static const unsigned char s_runRightMask[4] = {0x0f, 0x01, 0x03, 0x07};
static const unsigned char s_runEndMask[4] = {0x00, 0x01, 0x03, 0x07};

/* A byte stored with the map mask set: planes 0 and 2 of mask take even, 1 and 3 odd. */
static void store_planes(u16 seg, u16 off, unsigned mask, unsigned char even, unsigned char odd) {
    unsigned char *p = vram(seg, off);
    if (p == NULL)
        return;
    for (unsigned plane = 0; plane < 4; plane++)
        if (mask & (1u << plane))
            p[plane] = plane & 1 ? odd : even;
}

/* count pixels from x on the row at byte offset di: the first byte under its left mask,
 * whole bytes, the last byte under its end mask; when x and the last pixel agree under
 * sameMask (FFFCh in the run slots, 0FFCh in the span filler), one byte under both
 * masks. Planes 0 and 2 take even, 1 and 3 odd (the same colour for a solid run). */
static void fill_run(u16 seg, u16 di, u16 x, u16 count, unsigned char even, unsigned char odd,
                     u16 sameMask) {
    di = (u16)(di + (x >> 2));
    if ((((u16)(x + count - 1) ^ x) & sameMask) == 0) {
        unsigned mask = (unsigned char)(0xff << (x & 3)) & s_runRightMask[(u16)(x + count) & 3];
        store_planes(seg, di, mask, even, odd);
        return;
    }
    u16 middle = (u16)(count - 4 + (x & 3));
    store_planes(seg, di++, s_runLeftMask[x & 3], even, odd);
    for (u16 n = middle >> 2; n != 0; n--)
        store_planes(seg, di++, 0x0f, even, odd);
    store_planes(seg, di, s_runEndMask[middle & 3], even, odd);
}

/* Slot 11: dithered, the colours swapped on odd rows (DX). It takes the count as it
 * comes: zero or less wraps to a run of up to 64K pixels' bytes. */
void vga_fill_run_b(u16 seg, u16 di, u16 x, u16 count, u16 colors, u16 dx) {
    if (dx & 1)
        colors = (u16)(colors << 8 | colors >> 8);
    fill_run(seg, di, x, count, (unsigned char)(colors >> 8), (unsigned char)colors, 0xfffc);
}

/* Slot 10: nothing for a count of zero or less; a dither colour (AH) goes to slot 11. */
void vga_fill_run_a(u16 seg, u16 di, u16 x, u16 count, u16 colors, u16 dx) {
    if ((s16)count <= 0)
        return;
    if (colors >> 8) {
        vga_fill_run_b(seg, di, x, count, colors, dx);
        return;
    }
    fill_run(seg, di, x, count, (unsigned char)colors, (unsigned char)colors, 0xfffc);
}

/* Slot 27: rows from the first one on, each a run x0..x1 (none when x1 < x0, compared
 * signed) in the fill colour; with a dither colour, planes 0 and 2 take the dither and
 * 1 and 3 the fill colour, swapped on every other row, starting swapped when the first
 * row plus the first run's x0 is even. At least one row is drawn, whatever the count.
 * The original takes the spans at ES:SI. */
void vga_fill_spans_dithered_modey(const unsigned char *spans) {
    const unsigned char *si = spans;
    u16 bp = (u16)((si[0] | si[1] << 8) << 1);
    u16 counter = (u16)(si[2] | si[3] << 8);
    si += 4;
    u16 seg = g_graphics_context.wGfxBlitDstPage;
    unsigned char fill = g_graphics_context.bGfx_fill_color;
    unsigned char dither = g_graphics_context.bGfx_dither_color;
    int swapped = dither != 0 && !(((u16)(bp >> 1) + (u16)(si[0] | si[1] << 8)) & 1);
    for (;;) {
        u16 x0 = (u16)(si[0] | si[1] << 8), x1 = (u16)(si[2] | si[3] << 8);
        si += 4;
        if ((s16)x1 >= (s16)x0) { /* SUB AX,BX; JGE */
            u16 di = row_offset((s16)bp >> 1);
            u16 count = (u16)(x1 - x0 + 1);
            if (dither == 0)
                fill_run(seg, di, x0, count, fill, fill, 0x0ffc);
            else if (swapped)
                fill_run(seg, di, x0, count, fill, dither, 0x0ffc);
            else
                fill_run(seg, di, x0, count, dither, fill, 0x0ffc);
        }
        s32 left = (s32)(s16)counter - 1;
        counter--;
        if (left <= 0)
            return;
        bp = (u16)(bp + 2);
        swapped = dither != 0 && !swapped;
    }
}

/*
 * Slot 39, in slot 27's place while draw_polygon_textured runs (the camp clock's hand): the
 * pixels under each span translated through a 256-byte table, one at a time. The spans are slot
 * 27's: the first row, the number of rows (0 is 65536), then each row's x_start and x_end, x_end
 * not drawn; a row is drawn when x_end is above x_start, signed. Rows follow each other 80 bytes
 * apart from the first one's offset in the row table. A pixel is read through the read map
 * select and written under the map mask, both its plane, so the write goes through the graphics
 * controller as the original's does, and the registers are left as it leaves them. The table is
 * g_pTexturedSpanLut; the original reads it through SS at the context's wTextureIdx.
 */
void vga_fill_world_textured_span_modey(const unsigned char *spans) {
    static const unsigned char planeMask[4] = {0x01, 0x02, 0x04, 0x08};
    const unsigned char *si = spans;
    const unsigned char *lut = g_pTexturedSpanLut;
    u16 seg = g_graphics_context.wGfxBlitDstPage;
    u16 di = row_offset((s16)(si[0] | si[1] << 8));
    bak_port_out8(0x3C4, 2);
    bak_port_out8(0x3CE, 4);
    u16 rows = (u16)(si[2] | si[3] << 8);
    si += 4;
    do {
        u16 x0 = (u16)(si[0] | si[1] << 8), x1 = (u16)(si[2] | si[3] << 8);
        u16 row = di;
        if ((s16)x1 > (s16)x0) { /* SUB CX,AX; JLE */
            u16 count = (u16)(x1 - x0);
            unsigned char plane = (unsigned char)(x0 & 3);
            di = (u16)(di + (x0 >> 2));
            do {
                bak_port_out8(0x3CF, plane);
                bak_port_out8(0x3C5, planeMask[plane]);
                unsigned char px = bak_vga_read(seg, di);
                bak_vga_write(seg, di, lut[px]);
                plane = (unsigned char)((plane + 1) & 3);
                if (plane == 0)
                    di++;
            } while (--count != 0);
        }
        di = (u16)(row + 0x50);
        si += 4;
    } while (--rows != 0);
}

/* One pixel of a diagonal: di counts pixels from the page start (row * 320 + x). The
 * original reads the byte first, which loads the latches. */
static void line_pixel(u16 seg, u16 di, unsigned char color) {
    latch_load(seg, (u16)(di >> 2));
    store_planes(seg, (u16)(di >> 2), 1u << (di & 3), color, color);
}

/* Slot 2: a line from (BX, CX) to (DX, SI) in the outline colour, which it also leaves in
 * the graphics context's word at 6C2h. A horizontal one is a slot 10 run. */
void vga_draw_line(u16 seg, u16 x0, u16 y0, u16 x1, u16 y1) {
    unsigned char color = g_graphics_context.bGfx_outline_color;
    unsigned char *saved = (unsigned char *)&g_gfxContextBlock + 0x6c2;
    saved[0] = color;
    saved[1] = 0;
    u16 di = row_offset((s16)y0);
    if (x0 == x1 && y0 == y1) {
        di = (u16)(di + (x0 >> 2));
        latch_load(seg, di);
        store_planes(seg, di, 1u << (x0 & 3), color, color);
        return;
    }
    if (y0 == y1) {
        vga_fill_run_a(seg, di, x0, (u16)(x1 - x0 + 1), color, (u16)(x1 - x0));
        return;
    }
    if (x0 == x1) {
        di = (u16)(di + (x0 >> 2));
        unsigned mask = 1u << (x0 & 3);
        u16 dy = (u16)(y1 - y0);
        if (!((s16)y1 > (s16)y0)) { /* SUB SI,CX; JG: upwards, from the top end */
            dy = (u16)-dy;
            di = (u16)(di - row_offset((s16)dy));
            dy &= 0x7fff; /* SHL SI,1 / SHR SI,1 around the LUT read drop bit 15 */
        }
        u16 n = (u16)(dy + 1);
        do {
            latch_load(seg, di);
            store_planes(seg, di, mask, color, color);
            di = (u16)(di + PLANAR_ROW);
        } while (--n != 0);
        return;
    }
    di = (u16)((di << 2) + x0);
    u16 row = 0x140;
    u16 dx = (u16)(x1 - x0), dy = (u16)(y1 - y0);
    if (!((s16)y1 > (s16)y0)) {
        dy = (u16)-dy;
        row = (u16)-row;
    }
    if (dy == dx) { /* 45 degrees */
        u16 n = (u16)(dx + 1);
        do {
            line_pixel(seg, di, color);
            di = (u16)(di + 1 + row);
        } while (--n != 0);
        return;
    }
    u16 n, step, d, again, other;
    if (dy < dx) { /* x-major: x by x, a row now and then */
        n = dx;
        again = (u16)(dy << 1);
        d = (u16)(again - dx);
        other = (u16)(d - dx);
        step = row;
        line_pixel(seg, di++, color);
        do {
            if ((s16)d >= 0) {
                di = (u16)(di + step);
                d = (u16)(d + other);
            } else {
                d = (u16)(d + again);
            }
            line_pixel(seg, di++, color);
        } while (--n != 0);
        return;
    }
    /* y-major: row by row, downwards; upwards ones start from the far end */
    step = 1;
    if ((s16)row < 0) {
        step = 0xffff;
        di = (u16)(di - (u16)(0x140u * dy) + dx);
    }
    n = dy;
    again = (u16)(dx << 1);
    d = (u16)(again - dy);
    other = (u16)(d - dy);
    line_pixel(seg, di++, color);
    do {
        di = (u16)(di + 0x13f);
        if ((s16)d >= 0) {
            di = (u16)(di + step);
            d = (u16)(d + other);
        } else {
            d = (u16)(d + again);
        }
        line_pixel(seg, di++, color);
    } while (--n != 0);
}

/* ---- slots 3, 29: sprites --------------------------------------------------------- */

/* A source byte of the sprite's segment; past the memory the port has there, 0. */
static unsigned char sprite_byte(const unsigned char *seg, u32 extent, u16 si) {
    return si < extent ? seg[si] : 0;
}

/* The map mask for the next plane lane: planes from the first visible pixel's on, the
 * destination moving to the next byte after plane 3. */
static unsigned next_plane(u16 *plane, u16 *dst) {
    unsigned mask = 1u << (*plane & 0xff);
    (*plane)++;
    if (mask == 0x10) {
        (*dst)++;
        mask = 1;
        *plane = 1;
    }
    bak_port_out8(0x3C5, (unsigned char)mask);
    return mask == 1 ? 0 : mask == 2 ? 1 : mask == 4 ? 2 : 3;
}

/*
 * Slot 29: a sprite at (x, y), flipped vertically (flags bit 0) and/or horizontally (bit
 * 1); 0 is transparent. Its pixels are row-major with rows width & ~3 bytes long (the last
 * width & 3 pixels of a row are never drawn). The clipped sprite is drawn in four plane
 * lanes, each taking every fourth pixel of every row from the first visible one on, so the
 * source steps four bytes a pixel and the destination one byte; between lanes the source
 * moves a pixel along (back, flipped) and the destination plane one on.
 *
 * Kept from the original: the source position is quad-aligned plus a phase, so a record
 * offset that is not a multiple of 4 shifts the picture; clipping on both sides can leave
 * a negative width, whose lanes then run up to 16K bytes a row; a height of 0 unclipped is
 * 65536 rows.
 */
void vga_blit_sprite_master(const ImageRecord *sprite, s16 x, s16 y, u16 flags) {
    u16 width = (u16)sprite->nWidth, quads = width >> 2;
    u16 rows = (u16)sprite->nHeight;
    u16 total = (u16)((u16)(quads << 2) * rows);
    int hflip = (flags & 2) != 0, vflip = (flags & 1) != 0;
    u16 si = 0;
    if (hflip)
        si = (u16)(width - 1);
    if (vflip)
        si = (u16)(si + total - width);
    /* The pixel index of (x, y): the row offset times 4, whose bit 16 the original keeps
     * in the carry of the shift. */
    u16 row = row_offset(y);
    unsigned carry = row >> 14 & 1;
    u16 di = (u16)((u16)(row << 2) + x);
    u16 dx = (u16)(quads << 2);
    u16 skipped = 0; /* source pixels clipped off a row, right and left */
    if (g_graphics_context.bClip_enabled) {
        const ClipRect *c = &g_graphics_context.clip;
        s16 over = (s16)(c->xmax - x - dx);
        if (over < 0) {
            u16 n = (u16)(-over - 1);
            skipped = n;
            dx = (u16)(dx - n);
            if ((u16)(n >> 2) >= quads)
                return;
        }
        over = (s16)(c->ymax - y - rows);
        if (over < 0) {
            u16 n = (u16)(-over - 1);
            if (!(rows > n))
                return;
            rows = (u16)(rows - n);
        }
        if (c->ymin > y) {
            u16 cut = (u16)(c->ymin - y);
            rows = (u16)(rows - cut);
            u16 by = (u16)((u16)(cut * quads) << 2);
            si = (u16)(si + (vflip ? -by : by));
            row = row_offset(c->ymin);
            carry = row >> 14 & 1;
            di = (u16)((u16)(row << 2) + x);
            if (rows == 0 || (s16)rows < 0)
                return;
        }
        if (c->xmin > x) {
            u16 cut = (u16)(c->xmin - x);
            di = (u16)(di + cut);
            skipped = (u16)(skipped + cut);
            si = (u16)(si + (hflip ? -cut : cut));
            dx = (u16)(dx - cut);
            if ((u16)(cut >> 2) >= quads)
                return;
        }
    }
    u16 plane = di & 3;
    u16 phase = si & 3;
    u16 dst = (u16)((u16)(carry << 15 | di >> 1) >> 1);
    u16 quad = (u16)((si + sprite->wImageOff) & 0xfffc);

    u16 seg = g_graphics_context.wGfxBlitDstPage;
    const unsigned char *src = sprite->pImageBase;
    u32 extent = sprite->nImageExtent;
    bak_port_out8(0x3C4, 2);
    for (unsigned k = 0; k < 4; k++) {
        if (k > 0 && !hflip && phase == 0)
            quad = (u16)(quad + 4);
        if (k > 0 && hflip && phase == 3)
            quad = (u16)(quad - 4);
        unsigned p = next_plane(&plane, &dst);
        si = (u16)(quad + phase);
        phase = (u16)((phase + (hflip ? 3 : 1)) & 3);

        u16 n = (u16)((u16)(dx + 3 - k) >> 2);
        u16 step = (u16)((skipped + k) & 0xfffc);
        if (vflip && !hflip)
            step = (u16)(step - 2 * width);
        else if (hflip && !vflip)
            step = (u16)(2 * width - step);
        else if (hflip && vflip)
            step = (u16)-step;
        if (n == 0)
            continue;
        u16 d = dst;
        u16 r = rows;
        do {
            for (u16 count = n; count != 0; count--) {
                unsigned char al = sprite_byte(src, extent, si);
                si = (u16)(si + (hflip ? -4 : 4));
                if (al != 0) {
                    unsigned char *q = vram(seg, d);
                    if (q != NULL)
                        q[p] = al;
                }
                d++;
            }
            si = (u16)(si + step);
            d = (u16)(d + 0x50 - n);
        } while (--r != 0);
    }
}

void vga_blit_sprite(const ImageRecord *sprite, s16 x, s16 y) {
    vga_blit_sprite_master(sprite, x, y, 0);
}

/* ---- slot 33: opaque sprites -------------------------------------------------------- */

/*
 * Slot 33 (the sky's panorama): slot 29's shape unflipped, but opaque, every pixel copied
 * (0 too). Its top clip moves the source one row less than it cuts (it decrements the
 * count before its multiply), so a sprite cut at the top starts a row early and loses its
 * last row. The pixel index of (x, y) loses the row offset's bit 14, which slot 29 keeps.
 */
void vga_blit_sprite_simple(const ImageRecord *sprite, s16 x, s16 y) {
    u16 width = (u16)sprite->nWidth, quads = width >> 2;
    u16 rows = (u16)sprite->nHeight;
    u16 si = 0;
    u16 di = (u16)((u16)(row_offset(y) << 2) + x);
    u16 dx = (u16)(quads << 2);
    u16 skipped = 0; /* source pixels clipped off a row, right and left */
    if (g_graphics_context.bClip_enabled) {
        const ClipRect *c = &g_graphics_context.clip;
        s16 over = (s16)(c->xmax - x - dx);
        if (over < 0) {
            u16 n = (u16)(-over - 1);
            skipped = n;
            dx = (u16)(dx - n);
            if ((u16)(n >> 2) >= quads)
                return;
        }
        over = (s16)(c->ymax - y - rows);
        if (over < 0) {
            u16 n = (u16)(-over - 1);
            if (!(rows > n))
                return;
            rows = (u16)(rows - n);
        }
        if (c->ymin > y) {
            u16 cut = (u16)(c->ymin - y);
            rows = (u16)(rows - cut);
            si = (u16)(si + (u16)((u16)((u16)(cut - 1) * quads) << 2));
            di = (u16)((u16)(row_offset(c->ymin) << 2) + x);
            if (rows == 0 || (s16)rows < 0)
                return;
        }
        if (c->xmin > x) {
            u16 cut = (u16)(c->xmin - x);
            di = (u16)(di + cut);
            skipped = (u16)(skipped + cut);
            dx = (u16)(dx - cut);
            si = (u16)(si + cut);
            if ((u16)(cut >> 2) >= quads)
                return;
        }
    }
    u16 plane = di & 3;
    u16 phase = si & 3;
    u16 dst = di >> 2;
    u16 quad = (u16)((si + sprite->wImageOff) & 0xfffc);

    u16 seg = g_graphics_context.wGfxBlitDstPage;
    const unsigned char *src = sprite->pImageBase;
    u32 extent = sprite->nImageExtent;
    bak_port_out8(0x3C4, 2);
    for (unsigned k = 0; k < 4; k++) {
        if (k > 0 && phase == 0)
            quad = (u16)(quad + 4);
        unsigned p = next_plane(&plane, &dst);
        si = (u16)(quad + phase);
        phase = (u16)((phase + 1) & 3);

        u16 n = (u16)((u16)(dx + 3 - k) >> 2);
        u16 step = (u16)((skipped + k) & 0xfffc);
        if (n == 0)
            continue;
        u16 d = dst;
        u16 r = rows;
        do {
            for (u16 count = n; count != 0; count--) { /* MOVSB, then 3 more */
                unsigned char al = sprite_byte(src, extent, si);
                unsigned char *q = vram(seg, d);
                if (q != NULL)
                    q[p] = al;
                si = (u16)(si + 4);
                d++;
            }
            si = (u16)(si + step);
            d = (u16)(d + 0x50 - n);
        } while (--r != 0);
    }
}

/* ---- slots 34-35: DAC ------------------------------------------------------------- */

/* AL = AX / 63, a DIV with an 8-bit divisor (overflow goes to the divide-error handler). */
static unsigned char div63(u16 ax) {
    return bak_div8(ax, 0x3F, NULL);
}

/*
 * Blend count palette entries from start towards colour `target`, weight/63 of the
 * cached colour and the rest of the target, into the working half of the palette cache,
 * then upload them. Both halves are addressed from the cache's segment, offset 0.
 */
void vga_palette_lerp_dac(u16 start, u16 count, u16 target, u16 weight) {
    unsigned char *cache = MK_FP(FP_SEG(g_graphics_context.pPaletteScratchBuf), 0);
    unsigned char w = (unsigned char)weight;
    unsigned char inv = (unsigned char)(0x3F - w);
    u16 out = (u16)(start * 3);
    u16 in = (u16)(out + 0x300);
    u16 tgt = (u16)(0x300 + target * 3);
    u16 n = (u16)(count * 3);
    unsigned phase = 0;
    /* count = 0 blends 64 KB in the original (LOOP with CX = 0); natively nothing. */
    for (; n != 0; n--) {
        unsigned char term = div63((u16)(cache[in++] * w));
        cache[out++] = (unsigned char)(term + div63((u16)(cache[tgt] * inv)));
        tgt++;
        if (++phase == 3) {
            phase = 0;
            tgt = (u16)(tgt - 3);
        }
    }
    vga_dac_write_palette(cache + (u16)(start * 3), start, count);
}

/* Upload count RGB triples to the DAC from entry start, at vertical retrace. */
void vga_dac_write_palette(unsigned char *rgb, u16 start, u16 count) {
    u16 n = (u16)(count * 3);
    bak_vga_wait_retrace(0);
    /* The original "resyncs" a DAC that is not in read mode with a stray data write;
     * the DAC drops it, a partial triple being discarded once the index is set. */
    unsigned char state = bak_port_in8(0x3C7) & 3;
    if (state != 3)
        bak_port_out8(0x3C9, state);
    bak_port_out8(0x3C8, (unsigned char)start);
    /* count = 0 uploads 64 KB of whatever follows in the original; natively nothing. */
    for (; n != 0; n--)
        bak_port_out8(0x3C9, *rgb++);
}
