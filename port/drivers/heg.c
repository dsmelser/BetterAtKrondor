/*
 * The native hi-res EGA/VGA video driver: DRIVERS/VMCODE/HEG.ASM in C.
 *
 * The book viewer switches to this driver: 640 pixels across in 16 colours, with eight
 * pixels to a byte in each of the four planes. The original programs the planar memory
 * through the graphics controller (write mode 2 with the bit mask, latch copies in write
 * mode 1, plane-by-plane copies through the map mask or the read map select) and often
 * leaves the registers for the next routine to rely on. So this translation keeps every
 * register write and every display-memory access of the original, in order, and runs
 * them through the platform's VGA (bak_vga_read, bak_vga_write): the result depends on
 * the register state exactly as the original's does.
 *
 * The init has the original's four paths: mode 10h (640x350) for the 350-line height the
 * books ask for; mode 12h with 480 or 400 lines and the 640x200 mode 0Eh, which the
 * platform does not provide. Routines are ported as the book viewer reaches them; until
 * then their slots stop the game with the routine's name. Slots the original leaves
 * empty are no-ops.
 */
#include <stdbool.h>
#include <string.h>

#include "bak_video.h"
#include "heg.h"
#include "vga.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/STREAM/RESLOAD/IMGLOAD.H"
#include "bak_game_end.h"

#define PLANAR_ROW 80        /* bytes per row in a plane */
#define ROW_LUT_AT 0x6f2     /* offset of the row-offset LUT in the graphics-context block */
#define ROW_LUT_ROWS 480     /* the largest mode's rows; the LUT runs on into the render tail */
#define IMAGE_SCRATCH 0xA6D6 /* image conversion area: display memory past page A000's 350 rows */
#define PALETTE_HALF 0x30    /* 16 colours: the palette mirror's second half */

/* g_wHegArchiveCursor (chunk:25F0h): slot 31's running install position, which slot 32
 * resets. */
static u16 s_archiveCursor;

static u16 row_offset(s16 y) {
    return vga_row_offset(y);
}

/*
 * A far pointer as the original normalizes it before a copy: the segment advanced by the
 * offset's paragraphs, the offset kept to 0-15. The routines then step a 16-bit offset
 * from there, so a run past 64 KB wraps to the start of that segment.
 */
static unsigned char *normalized(const void *p, u16 *off) {
    u16 o = FP_OFF(p);
    *off = (u16)(o & 15);
    return (unsigned char *)p - (o & 15);
}

/* ---- init ----------------------------------------------------------------------- */

/* heg_reset_attribute_palette: attribute palette registers 15..1 set to the identity
 * (colour 0 keeps the BIOS's); the index register, whose palette-address-source bit
 * turns the display on, is put back as it was. */
static void reset_attribute_palette(void) {
    uint8_t saved = bak_port_in8(0x3C0);
    for (uint8_t n = 15; n != 0; n--) {
        (void)bak_port_in8(0x3DA);
        bak_port_out8(0x3C0, n);
        bak_port_out8(0x3C0, n);
    }
    bak_port_out8(0x3C0, saved);
}

/* INT 10h AX=1013h BX=0100h (sixteen colour pages of sixteen), then the identity
 * attribute palette. */
static void set_palette_paging(void) {
    bak_int10_palette_paging(0x00, 0x01);
    reset_attribute_palette();
}

static void heg_noop(void) {
}

#define TRAP(slot, name)                                                                           \
    static void trap_##name(void) {                                                                \
        BAK_UNIMPLEMENTED("HEG driver slot " #slot ": heg_" #name);                                \
    }
TRAP(2, line_renderer)
TRAP(10, pixel_run_fill)
TRAP(11, pixel_run_fill_alt)
TRAP(16, image_install_helper)
TRAP(26, set_palette_entry)
TRAP(27, span_fill)
TRAP(30, blit_byte_aligned_latched)
TRAP(31, install_image_archive)
TRAP(33, blit_sprite_planar)
TRAP(36, blit_stretched_row)
TRAP(37, pattern_span_fill)
TRAP(38, chunky_row_blit_pixelwise)

static void heg_reset_archive_install_flag(void) {
    s_archiveCursor = 0;
}

static RendererVtable s_slots;

#define SLOT(fn) ((void *)(fn))

static void fill_slots(void) {
    RendererVtable *t = &s_slots;
    void *noop = SLOT(heg_noop);
    memset(t, 0, sizeof *t); /* slots 0 and 39-49 are null */
    t->pfn_blit_glyph_row = SLOT(heg_blit_glyph_row);
    t->pfn_draw_line = SLOT(trap_line_renderer);
    t->pfn_blit_sprite = SLOT(heg_blit_sprite_noflip);
    t->pfn_present = SLOT(heg_present);
    t->pfn_save_rect = SLOT(heg_save_rect);
    t->pfn_rect_byte_size = SLOT(heg_blit_size);
    t->pfn_paste_rect = SLOT(heg_paste_rect);
    t->pfn_vsync_wait = SLOT(heg_vsync_wait);
    t->slot_09_unimplemented = noop;
    t->pfn_fill_run_a = SLOT(trap_pixel_run_fill);
    t->pfn_fill_run_b = SLOT(trap_pixel_run_fill_alt);
    t->slot_12_unimplemented = noop;
    t->pfn_image_install = SLOT(heg_image_install);
    t->pfn_image_decode = SLOT(heg_image_decode);
    t->pfn_unpack_nibbles_to_planes = noop;
    t->slot_16_unimplemented = SLOT(trap_image_install_helper);
    t->pfn_blit_chunky = SLOT(heg_blit_chunky);
    t->pfn_blit_chunky_planar_4bit = noop;
    t->pfn_fill_textured_span_modey_b = noop;
    t->pfn_palette_set = SLOT(heg_palette_set);
    t->pfn_getpixel = SLOT(heg_getpixel);
    t->pfn_putpixel = SLOT(heg_putpixel);
    t->pfn_blit_rect_buffer = noop;
    t->pfn_set_buffer_offset = noop; /* heg_tail_retf_stub */
    t->pPfn_set_border_color = SLOT(heg_set_border_color);
    t->slot_26_unimplemented = SLOT(trap_set_palette_entry);
    t->pfn_fill_spans_dithered_modey = SLOT(trap_span_fill);
    t->slot_28_unimplemented = noop;
    t->pfn_blit_sprite_master = SLOT(heg_blit_sprite_master);
    t->pfn_blit_sprite_flip_h = SLOT(trap_blit_byte_aligned_latched);
    t->pfn_blit_sprite_flip_v = SLOT(trap_install_image_archive);
    t->pfn_clear_rect_state = SLOT(heg_reset_archive_install_flag);
    t->pfn_blit_sprite_simple = SLOT(trap_blit_sprite_planar);
    t->pfn_palette_lerp_dac = SLOT(heg_palette_lerp_dac);
    t->pfn_dac_write_palette = noop;
    t->pfn_blit_sprite_planar_rotated = SLOT(trap_blit_stretched_row);
    t->pfn_blit_stretched = SLOT(trap_pattern_span_fill);
    t->pfn_blit_sprite_chunky_flip = SLOT(trap_chunky_row_blit_pixelwise);
}

const RendererVtable *heg_driver_init(void) {
    g_graphics_context.bYResDoubled = 1;
    g_graphics_context.bActiveVgaMode = 0x10;
    g_graphics_context.wVgaFrontPageBase = 0xA000;
    g_graphics_context.wVgaPage2Base = 0xA800;
    g_graphics_context.wVgaPage1Base = 0xA800;
    switch (g_wScreen_height) {
    case 0x15E: /* 640x350 */
        bak_int10_set_mode(0x10);
        set_palette_paging();
        break;
    case 0x1E0: /* 640x480 */
        bak_int10_set_mode(0x12);
        set_palette_paging();
        g_graphics_context.wVgaPage2Base = 0xA000;
        g_graphics_context.wVgaPage1Base = 0xA000;
        break;
    case 0x190: /* mode 12h's 480 lines cut to 400, scanning from F1F0h */
        bak_int10_set_mode(0x12);
        set_palette_paging();
        g_graphics_context.wVgaPage2Base = 0xA800;
        g_graphics_context.wVgaPage1Base = 0xA800;
        bak_port_out8(0x3D4, 0x12);
        bak_port_out8(0x3D5, (uint8_t)(bak_port_in8(0x3D5) - 0x23));
        bak_port_out8(0x3D4, 0x15);
        (void)bak_port_in8(0x3D5); /* read and dropped */
        bak_port_out8(0x3D4, 0x16);
        bak_port_out8(0x3D5, (uint8_t)(bak_port_in8(0x3D5) + 0x28));
        bak_port_out8(0x3D4, 0x0C);
        bak_port_out8(0x3D5, 0xF1);
        bak_port_out8(0x3D4, 0x0D);
        bak_port_out8(0x3D5, 0xF0);
        break;
    default: /* 640x200 */
        bak_int10_set_mode(0x0E);
        g_graphics_context.wVgaPage2Base = 0xA400;
        break;
    }
    /* Row k starts k * 80 bytes into each plane, for the largest mode's 480 rows: the LUT's
     * last 280 entries fill the render tail after it (GraphicsContextRender's reserved
     * bytes), up to the span-table segment. */
    unsigned char *lut = (unsigned char *)&g_gfxContextBlock + ROW_LUT_AT;
    for (u16 k = 0; k < ROW_LUT_ROWS; k++) {
        u16 v = (u16)(k * PLANAR_ROW);
        lut[2 * k] = (unsigned char)v;
        lut[2 * k + 1] = (unsigned char)(v >> 8);
    }
    bak_port_out16(0x3C4, 0x0F02);
    bak_port_out16(0x3CE, 0x0205);
    g_wScreen_width = 0x280;
    g_graphics_context.clip.xmax = 0x27F;
    g_graphics_context.clip.ymax = (s16)(g_wScreen_height - 1);
    fill_slots();
    return &s_slots;
}

/* ---- slot 1: text --------------------------------------------------------------- */

/* The 16-bit value AL = v, AH = 0 rotated right by n (ROR AX, CL). */
static u16 ror16(u16 v, unsigned n) {
    n &= 15;
    return n == 0 ? v : (u16)(v >> n | v << (16 - n));
}

/*
 * An 8-pixel-wide glyph at (x, y) on the destination page, one byte per row, in write mode
 * 2 through the bit mask: the glyph's bits spread over the two bytes it straddles. With
 * text style 0 each row's cell is first filled with the background colour.
 */
void heg_blit_glyph_row(s16 y, s16 x, s16 rows, const unsigned char *glyph) {
    u16 es = g_graphics_context.wGfxBlitDstPage;
    unsigned char fg = g_graphics_context.bText_fg_color;
    unsigned char bg = g_graphics_context.bText_bg_color;
    u16 di = (u16)(row_offset(y) + ((u16)x >> 3));
    unsigned shift = (u16)x & 7;
    u16 n = (u16)rows;
    if (n == 0)
        BAK_UNIMPLEMENTED("heg_glyph_row_blit with no rows (the original draws 65536)");
    bak_port_out8(0x3CE, 8);
    if (g_graphics_context.bText_style_flags != 0) {
        do {
            u16 bits = ror16(*glyph++, shift);
            bak_port_out8(0x3CF, (uint8_t)bits);
            (void)bak_vga_read(es, di);
            bak_vga_write(es, di++, fg);
            bak_port_out8(0x3CF, (uint8_t)(bits >> 8));
            (void)bak_vga_read(es, di);
            bak_vga_write(es, di++, fg);
            di = (u16)(di + PLANAR_ROW - 2);
        } while (--n != 0);
        return;
    }
    do {
        (void)bak_vga_read(es, di);
        u16 bits = ror16(*glyph++, shift);
        u16 cell = ror16(0x00FF, shift);
        bak_port_out8(0x3CF, (uint8_t)cell);
        bak_vga_write(es, di, bg);
        bak_port_out8(0x3CF, (uint8_t)bits);
        (void)bak_vga_read(es, di);
        bak_vga_write(es, di++, fg);
        (void)bak_vga_read(es, di);
        bak_port_out8(0x3CF, (uint8_t)(cell >> 8));
        bak_vga_write(es, di, bg);
        bak_port_out8(0x3CF, (uint8_t)(bits >> 8));
        (void)bak_vga_read(es, di);
        bak_vga_write(es, di++, fg);
        di = (u16)(di + PLANAR_ROW - 2);
    } while (--n != 0);
}

/* ---- slots 3 and 29: sprites ------------------------------------------------------- */

/*
 * A sprite as the master hands it to the row cores. The original keeps these in words of
 * its code segment (chunk:1710h-171Fh) that it saves on entry and restores on the way out,
 * so they belong to one call.
 */
typedef struct SpriteBlit {
    const unsigned char *src; /* the image's segment */
    u32 extent;               /* the bytes of it the port has */
    u16 planes;               /* plane 0's first byte */
    u16 mask;                 /* the transparency mask's first byte */
    u16 es, di;               /* the destination page and the first byte there */
    unsigned shift;           /* x & 7: the source moves right by this many bits */
    uint8_t count;            /* bytes a row, before its tail byte */
    u16 stride;               /* width / 8: the image's bytes from row to row */
    u16 planeSize;            /* the bytes from plane to plane */
    u16 rows;
    bool hflip, vflip;
    bool leftClipped, rightClipped;
} SpriteBlit;

/* A byte of the image's segment; past the memory the port has there, 0. */
static uint8_t sprite_byte(const SpriteBlit *s, u16 off) {
    return off < s->extent ? s->src[off] : 0;
}

/* A byte's bits in the opposite order (the flipping cores' eight SHR AL, 1 / RCL r, 1). */
static uint8_t reverse_bits(uint8_t v) {
    uint8_t r = 0;
    for (unsigned i = 0; i < 8; i++, v >>= 1)
        r = (uint8_t)(r << 1 | (v & 1));
    return r;
}

/* The first row's offset as the master computes it: the LUT entry for y, but y * 80 when
 * bit 14 of y is set (y from -16384 to -1, or 16384 and up). */
static u16 sprite_row_offset(s16 y) {
    if ((u16)y & 0x4000)
        return (u16)((u16)y * PLANAR_ROW);
    return row_offset(y);
}

/*
 * One pass of the row cores over the sprite's rows, from source offset si. The cut-out
 * pass reads the mask inverted and writes 0 through it as the bit mask, in the write mode
 * the caller left (the driver keeps mode 2: the opaque pixels become colour 0); a plane
 * pass writes the plane's bytes, which the OR set up before it adds to the latched
 * destination. Each byte written is the next source byte shifted right by x & 7, the bits
 * shifted out of the one before coming in at the top. A row is count bytes, then a tail
 * byte with the last bits shifted out, which a clipped right edge drops. A left-clipped
 * row starts with the bits of the byte before its first (after it, flipped horizontally);
 * with no bytes left it draws only the tail. The horizontal flip walks each row backwards
 * and reverses each byte's bits; the vertical flip walks the rows upwards.
 */
static void sprite_pass(const SpriteBlit *s, u16 si, bool cut) {
    uint8_t invert = cut ? 0xFF : 0x00;
    unsigned back = 8 - s->shift;
    u16 di = s->di;
    u16 rows = s->rows;
    do {
        u16 src = si, dst = di;
        uint8_t n = s->count;
        uint8_t carry = 0;
        bool tail = true;
        if (s->leftClipped) {
            uint8_t b = sprite_byte(s, s->hflip ? (u16)(src + 1) : (u16)(src - 1));
            b ^= invert;
            carry = s->hflip ? reverse_bits(b) : b;
        }
        if (!s->leftClipped || n != 0) {
            do { /* n = 0 is 256 bytes */
                uint8_t b = (uint8_t)(sprite_byte(s, src) ^ invert);
                if (s->hflip) {
                    b = reverse_bits(b);
                    src--;
                } else {
                    src++;
                }
                u16 ax = ror16((u16)(carry << 8 | b), s->shift);
                if (cut)
                    bak_port_out8(0x3CF, (uint8_t)ax);
                (void)bak_vga_read(s->es, dst);
                bak_vga_write(s->es, dst++, cut ? 0 : (uint8_t)ax);
                carry = (uint8_t)((uint8_t)(ax >> 8) >> back);
            } while (--n != 0);
            tail = !s->rightClipped;
        }
        if (tail) {
            u16 ax = ror16((u16)(carry << 8), s->shift);
            if (cut)
                bak_port_out8(0x3CF, (uint8_t)ax);
            (void)bak_vga_read(s->es, dst);
            bak_vga_write(s->es, dst, cut ? 0 : (uint8_t)ax);
        }
        si = (u16)(s->vflip ? si - s->stride : si + s->stride);
        di = (u16)(di + PLANAR_ROW);
    } while (--rows != 0);
}

/* The row cores: the cut-out pass over the mask (bit mask left at FFh), then write mode 0
 * with OR and planes 0-3, each through its read map select and map mask. */
static void sprite_cores(const SpriteBlit *s) {
    bak_port_out8(0x3CE, 8);
    sprite_pass(s, s->mask, true);
    bak_port_out8(0x3CF, 0xFF);
    bak_port_out16(0x3CE, 0x0005);
    bak_port_out16(0x3CE, 0x1003);
    u16 si = s->planes;
    for (unsigned plane = 0; plane < 4; plane++) {
        bak_port_out16(0x3CE, (u16)(plane << 8 | 4));
        bak_port_out16(0x3C4, (u16)((1u << plane) << 8 | 2));
        sprite_pass(s, si, false);
        si = (u16)(si + s->planeSize);
    }
}

/*
 * The clip rectangle as the master applies it, in whole bytes: false when the sprite is
 * outside it. Right: the bytes wholly past xmax come off each row, and the tail byte with
 * them, even when nothing passes it (x + width = xmax + 1). Left: the bytes before xmin
 * are skipped, rounded up. So an edge inside a byte draws up to 7 pixels past it or drops
 * up to 7 inside it. The rows cut off the top move the source by their count's low byte
 * times the stride's (an 8-bit MUL), wrong from 256 rows or a 2048-pixel width on. The
 * bottom and top tests take the sign of a difference (JS), not a comparison.
 */
static bool sprite_clip(SpriteBlit *s, s16 x, s16 y, u16 width, u16 height) {
    const ClipRect *c = &g_graphics_context.clip;
    u16 limit = (u16)(c->xmax + 1);
    u16 end = (u16)(x + width);
    if (!((s16)limit > (s16)end)) {
        u16 over = (u16)(end - limit);
        if (!((s16)over < (s16)width))
            return false;
        s->count = (uint8_t)(s->count - (uint8_t)(over >> 3));
        s->rightClipped = true;
    }
    u16 left = (u16)(x - c->xmin);
    if ((s16)left < 0) {
        if (!((s32)(s16)left + (s32)(s16)width > 0))
            return false;
        s->leftClipped = true;
        u16 skip = (u16)((u16)(-left + 7) >> 3);
        s->count = (uint8_t)(s->count - (uint8_t)skip);
        s->di = (u16)(s->di + skip);
        if (s->hflip)
            skip = (u16)-skip;
        s->planes = (u16)(s->planes + skip);
        s->mask = (u16)(s->mask + skip);
    }
    u16 bottom = (u16)(y + height);
    if (!((s16)bottom <= c->ymax)) {
        u16 over = (u16)(bottom - c->ymax);
        if (!((s16)(u16)(over - height) < 0))
            return false;
        s->rows = (u16)(s->rows - (u16)(over - 1));
    }
    u16 top = (u16)(c->ymin - y);
    if ((s16)top >= 0) {
        if (!((s16)(u16)(top - height) < 0))
            return false;
        s->rows = (u16)(s->rows - top);
        s->di = (u16)(s->di + (u16)(top << 4) + (u16)(top << 6));
        u16 skip = (u16)((uint8_t)top * (uint8_t)s->stride);
        if (s->vflip)
            skip = (u16)-skip;
        s->planes = (u16)(s->planes + skip);
        s->mask = (u16)(s->mask + skip);
    }
    return true;
}

/* The cores' common exit, also taken when the sprite is clipped away: write mode 2, no
 * rotation or logical operation, all planes. */
static void sprite_epilogue(void) {
    bak_port_out16(0x3CE, 0x0205);
    bak_port_out16(0x3CE, 0x0003);
    bak_port_out16(0x3C4, 0x0F02);
}

/*
 * Slot 29: an image installed by slot 14 at (x, y) on the destination page, through its
 * transparency mask, flipped vertically (flags bit 0) and/or horizontally (bit 1), clipped
 * to the clip rectangle when clipping is on. The opaque pixels are cut out to colour 0 in
 * the current write mode, then the four planes are ORed in. Each row takes (width + 7) / 8
 * bytes of each plane (a byte count kept in 8 bits) but the rows are width / 8 bytes
 * apart, so a width that is not a multiple of 8 reads into the next row (and before its
 * row, flipped horizontally). Only the flags' low byte counts. (The original copies the
 * record into its stack frame through DS: it relies on DS = SS, as in the game.)
 */
void heg_blit_sprite_master(const ImageRecord *sprite, s16 x, s16 y, u16 flags) {
    uint8_t flip = (uint8_t)flags;
    u16 width = (u16)sprite->nWidth, height = (u16)sprite->nHeight;
    SpriteBlit s;
    s.es = g_graphics_context.wGfxBlitDstPage;
    s.planeSize = (u16)((u16)(sprite->wUnk4 - sprite->wImageOff) >> 2);
    s.di = (u16)(sprite_row_offset(y) + (u16)(x >> 3));
    s.shift = (u16)x & 7;
    s.stride = width >> 3;
    s.count = (uint8_t)((u16)(width + 7) >> 3);
    s.rows = height;
    s.planes = sprite->wImageOff;
    s.mask = sprite->wUnk4;
    s.vflip = (flip & 1) != 0;
    s.hflip = (flip & 2) != 0;
    s.leftClipped = s.rightClipped = false;
    if (s.vflip) { /* from the last row */
        u16 last = (u16)((u32)(u16)(height - 1) * s.stride);
        s.planes = (u16)(s.planes + last);
        s.mask = (u16)(s.mask + last);
    }
    if (s.hflip) { /* from the last byte of the row */
        s.planes = (u16)(s.planes + s.stride - 1);
        s.mask = (u16)(s.mask + s.stride - 1);
    }
    if (g_graphics_context.bClip_enabled != 0 && !sprite_clip(&s, x, y, width, height)) {
        sprite_epilogue();
        return;
    }
    if (flip > 3)
        BAK_UNIMPLEMENTED("heg_blit_sprite_master with flip flags above 3 (the original jumps "
                          "through the words after its dispatch table)");
    if (s.rows == 0)
        BAK_UNIMPLEMENTED("heg_blit_sprite_master with no rows (the original draws 65536)");
    s.src = sprite->pImageBase;
    s.extent = sprite->nImageExtent;
    sprite_cores(&s);
    sprite_epilogue();
}

/* Slot 3: slot 29 without flips. */
void heg_blit_sprite_noflip(const ImageRecord *sprite, s16 x, s16 y) {
    heg_blit_sprite_master(sprite, x, y, 0);
}

/* ---- slots 4-8: rectangles and pages ---------------------------------------------- */

/* A rectangle from the source page to the destination page, in whole bytes (x rounded
 * down, x + width up to a multiple of 8): latch copies in write mode 1. */
void heg_present(s16 x, s16 y, s16 width, s16 height) {
    bak_port_out16(0x3CE, 0x0105);
    u16 end = (u16)(((u16)((u16)width + (u16)x) + 7) & 0xFFF8);
    u16 start = (u16)((u16)x & 0xFFF8);
    u16 bytes = (u16)((u16)(end - start) >> 3);
    u16 rows = (u16)height;
    u16 di = (u16)(row_offset(y) + (start >> 3));
    u16 es = g_graphics_context.wGfxBlitDstPage;
    u16 ds = g_graphics_context.wGfxBlitSrcPage;
    if (rows == 0)
        BAK_UNIMPLEMENTED("heg_present_rect with no rows (the original copies 65536)");
    do {
        u16 d = di;
        for (u16 c = bytes; c != 0; c--, d++)
            bak_vga_write(es, d, bak_vga_read(ds, d));
        di = (u16)(di + PLANAR_ROW);
    } while (--rows != 0);
    bak_port_out16(0x3CE, 0x0205);
}

/* The words a row of the rectangle spans: the bytes from x's to (x + width)'s, rounded up
 * to a whole word. */
static u16 rect_words(s16 x, s16 width, u16 *firstByte) {
    u16 first = (u16)x >> 3;
    u16 bytes = (u16)((u16)((u16)((u16)width + (u16)x) >> 3) - first + 1);
    *firstByte = first;
    return (u16)((bytes >> 1) + (bytes & 1));
}

/* Save a rectangle of the source page, plane 3 to plane 0 (read map select), row after
 * row, to a buffer: write mode 0, bit mask FFh and set/reset 0 are set on the way. */
void heg_save_rect(unsigned char *dst, s16 x, s16 y, s16 width, s16 height) {
    bak_port_out16(0x3CE, 0x0005);
    bak_port_out16(0x3CE, 0xFF08);
    bak_port_out16(0x3CE, 0x0000);
    u16 di;
    unsigned char *es = normalized(dst, &di);
    u16 first;
    u16 words = rect_words(x, width, &first);
    u16 rows = (u16)height;
    u16 si = (u16)(row_offset(y) + first);
    u16 ds = g_graphics_context.wGfxBlitSrcPage;
    if (rows == 0)
        BAK_UNIMPLEMENTED("heg_save_rect with no rows (the original copies 65536)");
    for (s16 plane = 3; plane >= 0; plane--) {
        bak_port_out16(0x3CE, (u16)(plane << 8 | 4));
        u16 row = si;
        for (u16 n = rows; n != 0; n--) {
            u16 s = row;
            for (u16 c = words; c != 0; c--) {
                es[di++] = bak_vga_read(ds, s++);
                es[di++] = bak_vga_read(ds, s++);
            }
            row = (u16)(row + PLANAR_ROW);
        }
    }
    bak_port_out16(0x3CE, 0x0205);
}

/* A saved rectangle's size: 4 planes of whole words per row, each row's bytes counted
 * from x = 0 plus one, as DX:AX. */
s32 heg_blit_size(s16 width, s16 height) {
    u16 w = (u16)width;
    u16 bytes = (u16)((w >> 3) + 1);
    if (w & 7)
        bytes++;
    if (bytes & 1)
        bytes++;
    u32 size = (u32)(u16)height * bytes;
    return (s32)(size << 2);
}

/* Paste a saved rectangle back onto the destination page, plane 3 to plane 0 through the
 * map mask; the map mask is left at all planes. */
void heg_paste_rect(unsigned char *src, s16 x, s16 y, s16 width, s16 height) {
    bak_port_out16(0x3CE, 0x0005);
    bak_port_out16(0x3CE, 0xFF08);
    bak_port_out16(0x3CE, 0x0000);
    u16 si;
    const unsigned char *ds = normalized(src, &si);
    u16 first;
    u16 words = rect_words(x, width, &first);
    u16 rows = (u16)height;
    u16 di = (u16)(row_offset(y) + first);
    u16 es = g_graphics_context.wGfxBlitDstPage;
    if (rows == 0)
        BAK_UNIMPLEMENTED("heg_blit_chunky_to_video with no rows (the original copies 65536)");
    for (unsigned mask = 8; mask != 0; mask >>= 1) {
        bak_port_out16(0x3C4, (u16)(mask << 8 | 2));
        u16 row = di;
        for (u16 n = rows; n != 0; n--) {
            u16 d = row;
            for (u16 c = words; c != 0; c--) {
                bak_vga_write(es, d++, ds[si++]);
                bak_vga_write(es, d++, ds[si++]);
            }
            row = (u16)(row + PLANAR_ROW);
        }
    }
    bak_port_out16(0x3CE, 0x0205);
    bak_port_out16(0x3C4, 0x0F02);
}

/* Swap the front and back pages and scan out the new front one (only the start address's
 * high byte is written: the pages sit on 2 KB boundaries), then optionally wait for the
 * next vertical retrace. */
void heg_vsync_wait(s16 wait) {
    u16 back = g_graphics_context.wVgaPage2Base;
    g_graphics_context.wVgaPage2Base = g_graphics_context.wVgaFrontPageBase;
    g_graphics_context.wVgaFrontPageBase = back;
    u16 start = (u16)(back >> 4);
    if (g_wScreen_height == 0x190)
        start = (u16)(start - 0xF);
    const unsigned char *bda = bak_bios_data();
    u16 crtc = (u16)(bda[0x63] | bda[0x64] << 8);
    bak_port_out16(crtc, (u16)(0x0C | (start & 0xFF) << 8));
    if (wait != 0)
        bak_vga_wait_retrace(1);
}

/* ---- slots 13 and 14: images --------------------------------------------------------- */

/* The bytes a list of images needs once installed: the low 16 bits of each image's
 * width / 2 x height summed, plus a quarter for the masks; no auxiliary buffer. */
u32 heg_image_install(ImageRecord **images, s16 *auxSize) {
    u32 total = 0;
    for (; *images != NULL; images++)
        total += (u16)((u16)((u16)(*images)->nWidth >> 1) * (u16)(*images)->nHeight);
    *auxSize = 0;
    return total + (total >> 2);
}

/* The bit-plane bytes of 8 packed pixels (two to a byte, high nibble first). */
static void transpose(const unsigned char px[4], unsigned char planes[4]) {
    memset(planes, 0, 4);
    for (unsigned i = 0; i < 8; i++) {
        unsigned nibble = (px[i >> 1] >> ((i & 1) ? 0 : 4)) & 15;
        for (unsigned p = 0; p < 4; p++)
            planes[p] = (unsigned char)(planes[p] << 1 | ((nibble >> p) & 1));
    }
}

/* Write the 4 plane bytes at es:di in write mode 0, one plane at a time through the map
 * mask (the sequencer's index must be at 2). */
static void write_planes(u16 es, u16 di, const unsigned char planes[4]) {
    for (unsigned p = 0; p < 4; p++) {
        bak_port_out8(0x3C5, (uint8_t)(1u << p));
        bak_vga_write(es, di, planes[p]);
    }
}

/*
 * heg_chunky_to_planar: count groups of 8 packed pixels from seg:off to the planes at
 * IMAGE_SCRATCH:0000. The source offset steps 4 bytes a group, and the segment advances
 * 64 KB when the second word's step carries out of the offset (a wrap at the first word
 * reads the same segment's start, as the original does).
 */
static void chunky_to_planar(u16 seg, u16 off, u16 count) {
    bak_port_out16(0x3CE, 0x0205);
    bak_port_out16(0x3CE, 0xFF08);
    bak_port_out16(0x3CE, 0x0005);
    bak_port_out16(0x3C4, 0x0102);
    const unsigned char *base = MK_FP(seg, 0);
    u16 si = off, di = 0;
    do {
        unsigned char px[4], planes[4];
        px[0] = base[si];
        px[1] = base[(u16)(si + 1)];
        si = (u16)(si + 2);
        px[2] = base[si];
        px[3] = base[(u16)(si + 1)];
        bool carry = si >= 0xFFFE;
        si = (u16)(si + 2);
        transpose(px, planes);
        if (carry) {
            seg = (u16)(seg + 0x1000);
            base = MK_FP(seg, 0);
        }
        write_planes(IMAGE_SCRATCH, di++, planes);
    } while (--count != 0);
    bak_port_out16(0x3CE, 0x0205);
    bak_port_out16(0x3C4, 0x0F02);
}

/* heg_install_image_planes: n bytes of each plane, 0 to 3 (read map select), from the
 * scratch area to seg:di, one after the other. */
static void copy_planes(u16 si, u16 seg, u16 di, u16 n) {
    unsigned char *es = MK_FP(seg, 0);
    for (uint8_t plane = 0; plane < 4; plane++) {
        bak_port_out16(0x3CE, (u16)(plane << 8 | 4));
        u16 s = si;
        for (u16 c = n; c != 0; c--)
            es[di++] = bak_vga_read(IMAGE_SCRATCH, s++);
    }
}

/* heg_read_planar_to_buffer: n mask bytes at seg:di, set where a pixel is colour 0 in all
 * planes (transparent). */
static void build_mask(u16 si, u16 seg, u16 di, u16 n) {
    unsigned char *es = MK_FP(seg, 0);
    bak_port_out16(0x3CE, 0x0004);
    do {
        uint8_t any = 0;
        for (uint8_t plane = 0; plane < 4; plane++) {
            bak_port_out8(0x3CF, plane);
            any |= bak_vga_read(IMAGE_SCRATCH, si);
        }
        es[di++] = (unsigned char)~any;
        si++;
    } while (--n != 0);
}

/*
 * Install packed images: the whole set goes to bit planes in the scratch area of display
 * memory first, then each image comes back in place of the packed data as its four planes
 * and a transparency mask (a quarter more than the packed size). Each record gets the
 * planes' segment and offset and the mask's offset. The trailing arguments are for other
 * adapters' drivers.
 */
void heg_image_decode(ImageRecord **images, unsigned char *data, u32 packedSize, unsigned char *aux,
                      u32 total) {
    (void)aux;
    (void)total;
    u16 off = FP_OFF(data), seg = FP_SEG(data);
    u16 count = (u16)(packedSize >> 2);
    if (count == 0)
        BAK_UNIMPLEMENTED("heg_install_image_set with under 4 packed bytes (the original "
                          "converts 65536 groups)");
    chunky_to_planar(seg, off, count);
    u16 di = 0;
    for (; *images != NULL; images++) {
        ImageRecord *r = *images;
        u16 n = (u16)((u16)((u16)((u16)r->nWidth >> 1) * (u16)r->nHeight) >> 2);
        u16 mask = (u16)((u16)(n << 2) + off);
        imgload_set_far(r, seg, off);
        r->wUnk4 = mask;
        if (n == 0)
            BAK_UNIMPLEMENTED("heg_install_image_set: an image under 8 pixels (the original "
                              "builds a 64 KB mask)");
        copy_planes(di, seg, off, n);
        build_mask(di, seg, mask, n);
        di = (u16)(di + n);
        u16 next = (u16)(n + mask);
        seg = (u16)(seg + (next >> 4));
        off = (u16)(next & 15);
    }
}

/* ---- slot 17: packed rows --------------------------------------------------------- */

/*
 * Packed 4-bit pixels (two to a byte, high nibble first) to the destination page at
 * (x, y): width / 8 groups of 8 pixels a row, height rows (at least one of each), each
 * group's bit-plane bytes written plane by plane through the map mask in write mode 0.
 * Ends with the map mask at all planes, write mode 2 and no data rotation.
 */
void heg_blit_chunky(unsigned char *src, s16 x, s16 y, s16 width, s16 height) {
    bak_port_out16(0x3CE, 0x0005);
    bak_port_out16(0x3CE, 0xFF08);
    bak_port_out8(0x3C4, 2);
    u16 di = (u16)(row_offset(y) + ((u16)x >> 3));
    u16 es = g_graphics_context.wGfxBlitDstPage;
    s16 rows = height;
    u16 si;
    const unsigned char *ds = normalized(src, &si);
    u16 columns = (u16)width >> 3;
    u16 skip = (u16)(PLANAR_ROW - columns);
    for (;;) {
        s16 n = (s16)columns;
        do {
            unsigned char px[4], planes[4];
            for (unsigned k = 0; k < 4; k++)
                px[k] = ds[si++];
            transpose(px, planes);
            write_planes(es, di++, planes);
        } while (--n > 0);
        if (--rows <= 0)
            break;
        di = (u16)(di + skip);
    }
    bak_port_out8(0x3C5, 0x0F);
    bak_port_out16(0x3CE, 0x0205);
    bak_port_out16(0x3CE, 0x0003);
}

/* ---- slots 20-22, 25, 34: palette and pixels ------------------------------------------ */

/* Load the 16 colours into the DAC and into both halves of the palette mirror. A palette
 * whose segment is 0 is ignored. */
void heg_palette_set(unsigned char *palette) {
    unsigned char *mirror = g_graphics_context.pPaletteScratchBuf;
    if (FP_SEG(palette) == 0)
        return;
    vga_dac_write_palette(palette, 0, 0x10);
    if (mirror == NULL)
        BAK_UNIMPLEMENTED("heg_install_palette without a palette mirror (the original writes "
                          "to 0000:0000)");
    memcpy(mirror, palette, PALETTE_HALF);
    memcpy(mirror + PALETTE_HALF, palette, PALETTE_HALF);
}

/* A pixel's colour from the source page: its bit in each plane (read map select). */
u16 heg_getpixel(s16 x, s16 y) {
    u16 es = g_graphics_context.wGfxBlitSrcPage;
    u16 bx = (u16)(row_offset(y) + ((u16)x >> 3));
    bak_port_out16(0x3CE, 0x0105);
    bak_port_out16(0x3CE, 0x0004);
    uint8_t bit = (uint8_t)(0x80 >> ((u16)x & 7));
    u16 color = 0;
    for (uint8_t plane = 0; plane < 4; plane++) {
        if (plane != 0)
            bak_port_out8(0x3CF, plane);
        if (bak_vga_read(es, bx) & bit)
            color |= (u16)(1u << plane);
    }
    bak_port_out16(0x3CE, 0x0205);
    return color;
}

/* A pixel in write mode 2 through a one-bit mask; the bit mask is left at FFh. */
void heg_putpixel(s16 x, s16 y, s16 color) {
    u16 es = g_graphics_context.wGfxBlitDstPage;
    u16 bx = (u16)(row_offset(y) + ((u16)x >> 3));
    uint8_t bit = (uint8_t)(0x80 >> ((u16)x & 7));
    bak_port_out16(0x3CE, (u16)(bit << 8 | 8));
    bak_port_out16(0x3CE, 0x0205);
    (void)bak_vga_read(es, bx);
    bak_vga_write(es, bx, (uint8_t)color);
    bak_port_out16(0x3CE, 0xFF08);
}

/* INT 10h AX=1001h: the overscan colour, the colour's low nibble (with 8 more for a
 * bright colour when the adapter is 2). */
void heg_set_border_color(s16 color) {
    uint8_t bh = (uint8_t)(color & 0x0F);
    if (g_graphics_context.bVideoAdapter == 2 && bh >= 8)
        bh = (uint8_t)(bh + 8);
    bak_int10_set_border(bh);
}

/* AL = AX / 63, a DIV with an 8-bit divisor (overflow goes to the divide-error handler). */
static unsigned char div63(u16 ax) {
    return bak_div8(ax, 0x3F, NULL);
}

/*
 * Blend count colours from start towards colour `target`, weight/63 of the mirror's
 * second half and the rest of the target (each term rounded down on its own), into the
 * mirror's first half, then upload them. Both halves are addressed from the mirror's
 * segment, offset 0.
 */
void heg_palette_lerp_dac(u16 start, u16 count, u16 target, u16 weight) {
    unsigned char *mirror = MK_FP(FP_SEG(g_graphics_context.pPaletteScratchBuf), 0);
    unsigned char w = (unsigned char)weight;
    unsigned char inv = (unsigned char)(0x3F - w);
    u16 out = (u16)(start * 3);
    u16 in = (u16)(out + PALETTE_HALF);
    u16 tgt = (u16)(PALETTE_HALF + target * 3);
    u16 n = (u16)(count * 3);
    unsigned phase = 0;
    /* count = 0 blends 64 KB in the original (LOOP with CX = 0); natively nothing. */
    for (; n != 0; n--) {
        unsigned char term = div63((u16)(mirror[in++] * w));
        mirror[out++] = (unsigned char)(term + div63((u16)(mirror[tgt] * inv)));
        tgt++;
        if (++phase == 3) {
            phase = 0;
            tgt = (u16)(tgt - 3);
        }
    }
    vga_dac_write_palette(mirror + (u16)(start * 3), start, count);
}
