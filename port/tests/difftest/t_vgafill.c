/* GFX/RASTER/VGAFILL.ASM: the sky's flat bands (port/asm/vgafill.c). The page comes from
 * the graphics context and the VGA's registers from heg.regs. */
#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GFX/RASTER/VGAFILL.H"
#include "bak_game_end.h"

/* vgafill.band s16 start_row, s16 row_count, s16 colour */
static void cmd_band(DtIn *in, DtOut *out) {
    (void)out;
    s16 start = (s16)dt_u16(in);
    s16 count = (s16)dt_u16(in);
    s16 color = (s16)dt_u16(in);
    vgaplanar_fill_band(start, count, color);
}

/* vgafill.strip s16 offset, s16 row_count */
static void cmd_strip(DtIn *in, DtOut *out) {
    (void)out;
    s16 offset = (s16)dt_u16(in);
    s16 count = (s16)dt_u16(in);
    vgaplanar_clear_column_strip(offset, count);
}

const DtEntry dt_vgafill_entries[] = {
    DT_COMMAND("vgafill.band", cmd_band),
    DT_COMMAND("vgafill.strip", cmd_strip),
    DT_END,
};
