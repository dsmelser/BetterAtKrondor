/* DRIVERS/VMCODE/VGA.ASM: the native video driver (port/drivers/vga.c). */
#include <string.h>

#include "difftest.h"
#include "bak_video.h"
#include "../../drivers/vga.h"
#include "../../platform/vgaregs.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/STREAM/RESLOAD/IMGLOAD.H"
#include "bak_game_end.h"

#define PALETTE_CACHE 0x600

/* The palette cache, a DOS block like the one PALDRV.C allocates (its far pointer is
 * seg:0000). Poking the context overwrites pPaletteScratchBuf; vga.palette_cache puts
 * this block back. */
static unsigned char *s_paletteCache;

static s16 arg(DtIn *in) {
    return (s16)dt_u16(in);
}

static void cmd_init(DtIn *in, DtOut *out) {
    (void)in;
    (void)out;
    vga_driver_init();
}

/* vga.palette_cache [1536 bytes] -> the cache contents (installed as the context's). */
static void cmd_palette_cache(DtIn *in, DtOut *out) {
    if (s_paletteCache == NULL)
        s_paletteCache = bak_dos_alloc(PALETTE_CACHE, 1);
    if (dt_remaining(in) != 0)
        memcpy(s_paletteCache, dt_bytes(in, PALETTE_CACHE), PALETTE_CACHE);
    g_graphics_context.pPaletteScratchBuf = s_paletteCache;
    dt_put_bytes(out, s_paletteCache, PALETTE_CACHE);
}

/* vga.dac [768 bytes] -> the DAC's 256 entries. */
static void cmd_dac(DtIn *in, DtOut *out) {
    if (dt_remaining(in) != 0)
        memcpy(g_vga.dac, dt_bytes(in, sizeof g_vga.dac), sizeof g_vga.dac);
    dt_put_bytes(out, g_vga.dac, sizeof g_vga.dac);
}

static void cmd_crtc_start(DtIn *in, DtOut *out) {
    (void)in;
    dt_put_u16(out, vga_crtc_start_address());
}

/* vga.glyph y x rows <glyph bytes> */
static void cmd_glyph(DtIn *in, DtOut *out) {
    (void)out;
    s16 y = arg(in), x = arg(in), rows = arg(in);
    vga_blit_glyph_row(y, x, rows, dt_bytes(in, dt_remaining(in)));
}

static void cmd_present(DtIn *in, DtOut *out) {
    (void)out;
    s16 x = arg(in), y = arg(in), w = arg(in), h = arg(in);
    vga_present(x, y, w, h);
}

/* vga.save_rect x y w h -> w*h bytes */
static void cmd_save_rect(DtIn *in, DtOut *out) {
    s16 x = arg(in), y = arg(in), w = arg(in), h = arg(in);
    size_t size = (size_t)(u16)w * (u16)h;
    unsigned char *buf = bak_dos_alloc((uint32_t)size, 1);
    vga_save_rect(buf, x, y, w, h);
    dt_put_bytes(out, buf, size);
    bak_dos_free(buf);
}

/* vga.paste_rect x y w h <w*h bytes> */
static void cmd_paste_rect(DtIn *in, DtOut *out) {
    (void)out;
    s16 x = arg(in), y = arg(in), w = arg(in), h = arg(in);
    size_t size = (size_t)(u16)w * (u16)h;
    unsigned char *buf = bak_dos_alloc((uint32_t)size, 0);
    memcpy(buf, dt_bytes(in, size), size);
    vga_paste_rect(buf, x, y, w, h);
    bak_dos_free(buf);
}

static void cmd_vsync(DtIn *in, DtOut *out) {
    (void)out;
    vga_vsync_wait(arg(in));
}

static void cmd_getpixel(DtIn *in, DtOut *out) {
    s16 x = arg(in), y = arg(in);
    dt_put_u16(out, vga_getpixel(x, y));
}

static void cmd_putpixel(DtIn *in, DtOut *out) {
    (void)out;
    s16 x = arg(in), y = arg(in), color = arg(in);
    vga_putpixel(x, y, color);
}

/* Images n, then n (width, height) pairs: a NULL-terminated table of records. */
static ImageRecord **read_images(DtIn *in, ImageRecord *records, ImageRecord **table, u16 n) {
    for (u16 i = 0; i < n; i++) {
        memset(&records[i], 0, sizeof records[i]);
        records[i].nWidth = arg(in);
        records[i].nHeight = arg(in);
        table[i] = &records[i];
    }
    table[n] = NULL;
    return table;
}

#define MAX_IMAGES 64

/* vga.image_install n (w h)* -> u32 total, s16 aux size */
static void cmd_image_install(DtIn *in, DtOut *out) {
    ImageRecord records[MAX_IMAGES];
    ImageRecord *table[MAX_IMAGES + 1];
    u16 n = dt_u16(in);
    if (n > MAX_IMAGES)
        dt_fail("at most %d images", MAX_IMAGES);
    s16 aux = 0x5555;
    u32 total = vga_image_install(read_images(in, records, table, n), &aux);
    dt_put_u32(out, total);
    dt_put_u16(out, (u16)aux);
}

/* vga.image_decode n (w h)* u32 packed <packed bytes>
 *   -> per image: u32 offset of its pixels in the buffer, u16 its far offset; the buffer */
static void cmd_image_decode(DtIn *in, DtOut *out) {
    ImageRecord records[MAX_IMAGES];
    ImageRecord *table[MAX_IMAGES + 1];
    u16 n = dt_u16(in);
    if (n > MAX_IMAGES)
        dt_fail("at most %d images", MAX_IMAGES);
    read_images(in, records, table, n);
    u32 packed = dt_u32(in);
    unsigned char *buf = bak_dos_alloc(2 * packed, 1);
    memcpy(buf, dt_bytes(in, packed), packed);
    vga_image_decode(table, buf, packed, NULL, 0);
    for (u16 i = 0; i < n; i++) {
        unsigned char *p = records[i].pImageBase + records[i].wImageOff;
        dt_put_u32(out, (u32)(p - buf));
        dt_put_u16(out, records[i].wImageOff);
    }
    dt_put_bytes(out, buf, 2 * packed);
    bak_dos_free(buf);
}

/* vga.unpack count <count src bytes> <2*count dst bytes> -> dst */
static void cmd_unpack(DtIn *in, DtOut *out) {
    u16 count = dt_u16(in);
    if (count == 0)
        dt_fail("count 0 means 65536 in the original; not supported here");
    const uint8_t *src = dt_bytes(in, count);
    unsigned char *dst = bak_dos_alloc(2u * count, 0);
    memcpy(dst, dt_bytes(in, 2u * count), 2u * count);
    vga_unpack_nibbles_to_planes((unsigned char *)src, dst, count);
    dt_put_bytes(out, dst, 2u * count);
    bak_dos_free(dst);
}

/* vga.palette_set <768 bytes> */
static void cmd_palette_set(DtIn *in, DtOut *out) {
    (void)out;
    unsigned char *pal = bak_dos_alloc(0x300, 0);
    memcpy(pal, dt_bytes(in, 0x300), 0x300);
    vga_palette_set(pal);
    bak_dos_free(pal);
}

static void cmd_lerp(DtIn *in, DtOut *out) {
    (void)out;
    u16 start = dt_u16(in), count = dt_u16(in), target = dt_u16(in), weight = dt_u16(in);
    vga_palette_lerp_dac(start, count, target, weight);
}

/* vga.dac_write start count <count*3 bytes> */
static void cmd_dac_write(DtIn *in, DtOut *out) {
    (void)out;
    u16 start = dt_u16(in), count = dt_u16(in);
    vga_dac_write_palette((unsigned char *)dt_bytes(in, 3u * count), start, count);
}

/* vga.draw_line seg x0 y0 x1 y1 */
static void cmd_draw_line(DtIn *in, DtOut *out) {
    (void)out;
    u16 seg = dt_u16(in), x0 = dt_u16(in), y0 = dt_u16(in), x1 = dt_u16(in), y1 = dt_u16(in);
    vga_draw_line(seg, x0, y0, x1, y1);
}

/* vga.fill_run slot(10/11) seg di x count colors dx */
static void cmd_fill_run(DtIn *in, DtOut *out) {
    (void)out;
    u16 slot = dt_u16(in), seg = dt_u16(in), di = dt_u16(in), x = dt_u16(in);
    u16 count = dt_u16(in), colors = dt_u16(in), dx = dt_u16(in);
    if (slot == 10)
        vga_fill_run_a(seg, di, x, count, colors, dx);
    else
        vga_fill_run_b(seg, di, x, count, colors, dx);
}

/* vga.fill_spans <span table> */
static void cmd_fill_spans(DtIn *in, DtOut *out) {
    (void)out;
    size_t n = dt_remaining(in);
    unsigned char *spans = bak_dos_alloc((uint32_t)n, 0);
    memcpy(spans, dt_bytes(in, n), n);
    vga_fill_spans_dithered_modey(spans);
    bak_dos_free(spans);
}

/* vga.sprite slot(3/29/33) flags x y width height offset <pixels>: the record's segment is a
 * zeroed 64 KB block with the pixels at the offset. */
static void cmd_sprite(DtIn *in, DtOut *out) {
    (void)out;
    u16 slot = dt_u16(in), flags = dt_u16(in);
    s16 x = arg(in), y = arg(in);
    ImageRecord rec;
    memset(&rec, 0, sizeof rec);
    rec.nWidth = arg(in);
    rec.nHeight = arg(in);
    rec.wImageOff = dt_u16(in);
    size_t n = dt_remaining(in);
    if (rec.wImageOff + n > 0x10000)
        dt_fail("the pixels run past the segment");
    unsigned char *block = bak_dos_alloc(0x10000, 1);
    memcpy(block + rec.wImageOff, dt_bytes(in, n), n);
    imgload_set_far(&rec, FP_SEG(block), rec.wImageOff);
    if (slot == 3)
        vga_blit_sprite(&rec, x, y);
    else if (slot == 33)
        vga_blit_sprite_simple(&rec, x, y);
    else
        vga_blit_sprite_master(&rec, x, y, flags);
    bak_dos_free(block);
}

const DtEntry dt_vga_entries[] = {
    DT_COMMAND("vga.init", cmd_init),
    DT_COMMAND("vga.palette_cache", cmd_palette_cache),
    DT_COMMAND("vga.dac", cmd_dac),
    DT_COMMAND("vga.crtc_start", cmd_crtc_start),
    DT_COMMAND("vga.glyph", cmd_glyph),
    DT_COMMAND("vga.present", cmd_present),
    DT_COMMAND("vga.save_rect", cmd_save_rect),
    DT_COMMAND("vga.paste_rect", cmd_paste_rect),
    DT_COMMAND("vga.vsync", cmd_vsync),
    DT_COMMAND("vga.getpixel", cmd_getpixel),
    DT_COMMAND("vga.putpixel", cmd_putpixel),
    DT_COMMAND("vga.image_install", cmd_image_install),
    DT_COMMAND("vga.image_decode", cmd_image_decode),
    DT_COMMAND("vga.unpack", cmd_unpack),
    DT_COMMAND("vga.palette_set", cmd_palette_set),
    DT_COMMAND("vga.lerp", cmd_lerp),
    DT_COMMAND("vga.dac_write", cmd_dac_write),
    DT_COMMAND("vga.draw_line", cmd_draw_line),
    DT_COMMAND("vga.fill_run", cmd_fill_run),
    DT_COMMAND("vga.fill_spans", cmd_fill_spans),
    DT_COMMAND("vga.sprite", cmd_sprite),
    DT_GLOBAL(g_gfxContextBlock),
    DT_GLOBAL(g_bakVram),
    DT_END,
};
