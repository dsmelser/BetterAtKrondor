/* GFX/SPRITE/PALBLEND.ASM: the blended colour's nearest palette entry (port/asm/palblend.c). */
#include <stdint.h>

#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GFX/SPRITE/BLITAA.H"
#include "SRC/GFX/SPRITE/PALBLEND.H"
#include "bak_game_end.h"

/* palblend.lookup <768-byte palette> <u16 idx_a, idx_b>... -> u16 each */
static void cmd_lookup(DtIn *in, DtOut *out) {
    static unsigned char palette[768];
    const uint8_t *p = dt_bytes(in, sizeof palette);
    for (size_t i = 0; i < sizeof palette; i++)
        palette[i] = p[i];
    g_pAaPaletteBuf = (u32)(uintptr_t)palette;
    while (dt_remaining(in) != 0) {
        u16 a = dt_u16(in);
        u16 b = dt_u16(in);
        dt_put_u16(out, (uint16_t)palette_blend_lookup(a, b));
    }
}

const DtEntry dt_palblend_entries[] = {
    DT_COMMAND("palblend.lookup", cmd_lookup),
    DT_END,
};
