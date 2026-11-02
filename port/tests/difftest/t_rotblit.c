/* GFX/SPRITE/ROTBLIT.ASM: a sprite drawn rotated and scaled (port/asm/rotblit.c), each span
 * logged by a stand-in for the VGA driver's slot 36 instead of drawn. */
#include <stdint.h>
#include <string.h>

#include "difftest.h"
#include "../../drivers/vga.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/SPRITE/ROTBLIT.H"
#include "bak_game_end.h"

static DtOut *s_log;
static const ImageRecord *s_sprite;

/* A span as the original's stand-in logs it: page, row offset, x, count, row base, whether the
 * sprite is the one drawn, the source table's index, then count words of the table. */
static void log_span(u16 seg, u16 di, const ImageRecord *sprite, u16 rowBase, const s16 *columns,
                     s16 count, s16 x) {
    dt_put_u16(s_log, seg);
    dt_put_u16(s_log, di);
    dt_put_u16(s_log, (u16)x);
    dt_put_u16(s_log, (u16)count);
    dt_put_u16(s_log, rowBase);
    dt_put_u16(s_log, sprite == s_sprite);
    dt_put_u16(s_log, (u16)(columns - g_aStretchXSrcTable));
    for (s16 i = 0; i < count; i++)
        dt_put_u16(s_log, (u16)columns[i]);
}

/* rotblit.draw s16 width, height, x, y, angle, scale_x, scale_y, half_w, half_h -> the spans */
static void cmd_draw(DtIn *in, DtOut *out) {
    static ImageRecord rec;
    s16 a[7];
    memset(&rec, 0, sizeof rec);
    rec.nWidth = (s16)dt_u16(in);
    rec.nHeight = (s16)dt_u16(in);
    for (int i = 0; i < 7; i++)
        a[i] = (s16)dt_u16(in);
    void *saved = (void *)g_renderer_vtable.pfn_blit_sprite_planar_rotated;
    g_renderer_vtable.pfn_blit_sprite_planar_rotated = (Slot36Fn *)(void *)log_span;
    s_log = out;
    s_sprite = &rec;
    jmp_via_2f48((unsigned char *)&rec, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
    g_renderer_vtable.pfn_blit_sprite_planar_rotated = (Slot36Fn *)saved;
}

/* rotblit.corners -> u16 x of the opposite clockwise corner, y of the top, x of the next
 * clockwise one, y of the bottom (read_4words_from_2tables) */
static void cmd_corners(DtIn *in, DtOut *out) {
    (void)in;
    unsigned short w[4];
    read_4words_from_2tables(&w[0], &w[1], &w[2], &w[3]);
    for (int i = 0; i < 4; i++)
        dt_put_u16(out, w[i]);
}

const DtEntry dt_rotblit_entries[] = {
    DT_COMMAND("rotblit.draw", cmd_draw),
    DT_COMMAND("rotblit.corners", cmd_corners),
    DT_GLOBAL(g_anRotBlitState),
    DT_GLOBAL(g_aStretchSrcTables),
    DT_END,
};
