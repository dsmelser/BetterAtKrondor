/* DRIVERS/VMCODE/HEG.ASM: the native hi-res EGA/VGA video driver (port/drivers/heg.c). */
#include <string.h>

#include "difftest.h"
#include "bak_video.h"
#include "../../drivers/heg.h"
#include "../../platform/vgaregs.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/STREAM/RESLOAD/IMGLOAD.H"
#include "bak_game_end.h"

#define MIRROR 0x60 /* 16 colours, two halves */
#define MAX_IMAGES 64

/* The palette mirror, a DOS block like the one PALDRV.C allocates (its far pointer is
 * seg:0000). Poking the context overwrites pPaletteScratchBuf; heg.mirror puts this block
 * back. */
static unsigned char *s_mirror;

static s16 arg(DtIn *in) {
    return (s16)dt_u16(in);
}

static void cmd_init(DtIn *in, DtOut *out) {
    (void)in;
    (void)out;
    heg_driver_init();
}

/* heg.regs [99 bytes] -> the VGA's registers (vga_regs_snapshot), set first when given. */
static void cmd_regs(DtIn *in, DtOut *out) {
    uint8_t regs[VGA_REGS_SNAPSHOT];
    if (dt_remaining(in) != 0)
        vga_regs_restore(dt_bytes(in, VGA_REGS_SNAPSHOT));
    vga_regs_snapshot(regs);
    dt_put_bytes(out, regs, sizeof regs);
}

/* heg.mirror [96 bytes] -> the mirror's contents (installed as the context's). */
static void cmd_mirror(DtIn *in, DtOut *out) {
    if (s_mirror == NULL)
        s_mirror = bak_dos_alloc(MIRROR, 1);
    if (dt_remaining(in) != 0)
        memcpy(s_mirror, dt_bytes(in, MIRROR), MIRROR);
    g_graphics_context.pPaletteScratchBuf = s_mirror;
    dt_put_bytes(out, s_mirror, MIRROR);
}

/* A DOS block of size bytes plus off, holding the payload's next size bytes when given;
 * the routine gets the pointer off bytes in. */
static unsigned char *buffer(DtIn *in, u16 off, u32 size, int fill) {
    unsigned char *b = bak_dos_alloc(off + size, 1);
    if (fill)
        memcpy(b, dt_bytes(in, off + size), off + size);
    return b;
}

/* heg.glyph y x rows <glyph bytes> */
static void cmd_glyph(DtIn *in, DtOut *out) {
    (void)out;
    s16 y = arg(in), x = arg(in), rows = arg(in);
    heg_blit_glyph_row(y, x, rows, dt_bytes(in, dt_remaining(in)));
}

static void cmd_present(DtIn *in, DtOut *out) {
    (void)out;
    s16 x = arg(in), y = arg(in), w = arg(in), h = arg(in);
    heg_present(x, y, w, h);
}

/* heg.save_rect x y w h off size -> off + size bytes (the buffer starts zeroed) */
static void cmd_save_rect(DtIn *in, DtOut *out) {
    s16 x = arg(in), y = arg(in), w = arg(in), h = arg(in);
    u16 off = dt_u16(in);
    u32 size = dt_u32(in);
    unsigned char *b = buffer(in, off, size, 0);
    heg_save_rect(b + off, x, y, w, h);
    dt_put_bytes(out, b, off + size);
    bak_dos_free(b);
}

static void cmd_blit_size(DtIn *in, DtOut *out) {
    s16 w = arg(in), h = arg(in);
    dt_put_u32(out, (u32)heg_blit_size(w, h));
}

/* heg.paste_rect x y w h off size <off + size bytes> */
static void cmd_paste_rect(DtIn *in, DtOut *out) {
    (void)out;
    s16 x = arg(in), y = arg(in), w = arg(in), h = arg(in);
    u16 off = dt_u16(in);
    u32 size = dt_u32(in);
    unsigned char *b = buffer(in, off, size, 1);
    heg_paste_rect(b + off, x, y, w, h);
    bak_dos_free(b);
}

/* heg.blit_chunky x y w h off size <off + size bytes> */
static void cmd_blit_chunky(DtIn *in, DtOut *out) {
    (void)out;
    s16 x = arg(in), y = arg(in), w = arg(in), h = arg(in);
    u16 off = dt_u16(in);
    u32 size = dt_u32(in);
    unsigned char *b = buffer(in, off, size, 1);
    heg_blit_chunky(b + off, x, y, w, h);
    bak_dos_free(b);
}

static void cmd_vsync(DtIn *in, DtOut *out) {
    (void)out;
    heg_vsync_wait(arg(in));
}

/* Images n, then n (width, height) pairs: a NULL-terminated table of records. */
static ImageRecord **read_images(DtIn *in, ImageRecord *records, ImageRecord **table, u16 n) {
    if (n > MAX_IMAGES)
        dt_fail("at most %d images", MAX_IMAGES);
    for (u16 i = 0; i < n; i++) {
        memset(&records[i], 0, sizeof records[i]);
        records[i].nWidth = arg(in);
        records[i].nHeight = arg(in);
        table[i] = &records[i];
    }
    table[n] = NULL;
    return table;
}

/* A record's segment, relative to the block's at base (the record holds its paragraph). */
static u16 record_seg(const ImageRecord *r, const unsigned char *base) {
    return (u16)((r->pImageBase - base) >> 4);
}

/* heg.image_install n (w h)* -> u32 total, s16 aux size */
static void cmd_image_install(DtIn *in, DtOut *out) {
    ImageRecord records[MAX_IMAGES];
    ImageRecord *table[MAX_IMAGES + 1];
    u16 n = dt_u16(in);
    s16 aux = 0x5555;
    u32 total = heg_image_install(read_images(in, records, table, n), &aux);
    dt_put_u32(out, total);
    dt_put_u16(out, (u16)aux);
}

/*
 * heg.image_decode n (w h)* u32 packed u32 size <size bytes>
 *   -> each record (its segment relative to the buffer's), then the buffer
 */
static void cmd_image_decode(DtIn *in, DtOut *out) {
    ImageRecord records[MAX_IMAGES];
    ImageRecord *table[MAX_IMAGES + 1];
    u16 n = dt_u16(in);
    read_images(in, records, table, n);
    u32 packed = dt_u32(in);
    u32 size = dt_u32(in);
    unsigned char *b = buffer(in, 0, size, 1);
    heg_image_decode(table, b, packed, NULL, size);
    for (u16 i = 0; i < n; i++) {
        dt_put_u16(out, record_seg(&records[i], b));
        dt_put_u16(out, records[i].wImageOff);
        dt_put_u16(out, records[i].wUnk4);
        dt_put_u16(out, (u16)records[i].nWidth);
        dt_put_u16(out, (u16)records[i].nHeight);
    }
    dt_put_bytes(out, b, size);
    bak_dos_free(b);
}

/* The images sprites come from: a DOS block of two segments, so a record whose segment is
 * in the first has all 64 KB of it. */
#define SPRITE_DATA 0x20000
static unsigned char *s_sprites;

/*
 * heg.sprite_data off u32 packed n (w h)* <SPRITE_DATA bytes>
 *   -> each installed record (its segment relative to the block's), then the block
 * The block takes the bytes; with n images, slot 14 then installs them from offset off.
 */
static void cmd_sprite_data(DtIn *in, DtOut *out) {
    ImageRecord records[MAX_IMAGES];
    ImageRecord *table[MAX_IMAGES + 1];
    u16 off = dt_u16(in);
    u32 packed = dt_u32(in);
    u16 n = dt_u16(in);
    read_images(in, records, table, n);
    if (s_sprites == NULL)
        s_sprites = bak_dos_alloc(SPRITE_DATA, 1);
    memcpy(s_sprites, dt_bytes(in, SPRITE_DATA), SPRITE_DATA);
    if (n != 0)
        heg_image_decode_at(table, s_sprites, SPRITE_DATA, off, packed);
    for (u16 i = 0; i < n; i++) {
        dt_put_u16(out, record_seg(&records[i], s_sprites));
        dt_put_u16(out, records[i].wImageOff);
        dt_put_u16(out, records[i].wUnk4);
        dt_put_u16(out, (u16)records[i].nWidth);
        dt_put_u16(out, (u16)records[i].nHeight);
    }
    dt_put_bytes(out, s_sprites, SPRITE_DATA);
}

/* heg.sprite slot(3/29) flags x y seg off mask w h: a record whose segment is relative to
 * the sprite block's (slot 3 takes no flags) */
static void cmd_sprite(DtIn *in, DtOut *out) {
    (void)out;
    if (s_sprites == NULL)
        dt_fail("no sprite data (heg.sprite_data)");
    u16 slot = dt_u16(in), flags = dt_u16(in);
    s16 x = arg(in), y = arg(in);
    ImageRecord rec;
    u16 seg = (u16)(FP_SEG(s_sprites) + dt_u16(in));
    imgload_set_far(&rec, seg, dt_u16(in));
    rec.wUnk4 = dt_u16(in);
    rec.nWidth = arg(in);
    rec.nHeight = arg(in);
    if (slot == 3)
        heg_blit_sprite_noflip(&rec, x, y);
    else
        heg_blit_sprite_master(&rec, x, y, flags);
}

/* heg.palette_set [48 bytes]: without them, a null palette */
static void cmd_palette_set(DtIn *in, DtOut *out) {
    (void)out;
    if (dt_remaining(in) == 0) {
        heg_palette_set(NULL);
        return;
    }
    unsigned char *b = buffer(in, 0, 48, 1);
    heg_palette_set(b);
    bak_dos_free(b);
}

static void cmd_getpixel(DtIn *in, DtOut *out) {
    s16 x = arg(in), y = arg(in);
    dt_put_u16(out, heg_getpixel(x, y));
}

static void cmd_putpixel(DtIn *in, DtOut *out) {
    (void)out;
    s16 x = arg(in), y = arg(in), color = arg(in);
    heg_putpixel(x, y, color);
}

static void cmd_border(DtIn *in, DtOut *out) {
    (void)out;
    heg_set_border_color(arg(in));
}

/* heg.lerp start count target weight */
static void cmd_lerp(DtIn *in, DtOut *out) {
    (void)out;
    u16 start = dt_u16(in), count = dt_u16(in), target = dt_u16(in), weight = dt_u16(in);
    heg_palette_lerp_dac(start, count, target, weight);
}

const DtEntry dt_heg_entries[] = {
    DT_COMMAND("heg.init", cmd_init),
    DT_COMMAND("heg.regs", cmd_regs),
    DT_COMMAND("heg.mirror", cmd_mirror),
    DT_COMMAND("heg.glyph", cmd_glyph),
    DT_COMMAND("heg.present", cmd_present),
    DT_COMMAND("heg.save_rect", cmd_save_rect),
    DT_COMMAND("heg.blit_size", cmd_blit_size),
    DT_COMMAND("heg.paste_rect", cmd_paste_rect),
    DT_COMMAND("heg.blit_chunky", cmd_blit_chunky),
    DT_COMMAND("heg.vsync", cmd_vsync),
    DT_COMMAND("heg.image_install", cmd_image_install),
    DT_COMMAND("heg.image_decode", cmd_image_decode),
    DT_COMMAND("heg.sprite_data", cmd_sprite_data),
    DT_COMMAND("heg.sprite", cmd_sprite),
    DT_COMMAND("heg.palette_set", cmd_palette_set),
    DT_COMMAND("heg.getpixel", cmd_getpixel),
    DT_COMMAND("heg.putpixel", cmd_putpixel),
    DT_COMMAND("heg.border", cmd_border),
    DT_COMMAND("heg.lerp", cmd_lerp),
    DT_END,
};
