/* GFX/DRIVER/VIDDET.ASM: adapter detection and video shutdown (port/asm/viddet.c). */
#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GFX/DRIVER/VIDDET.H"
#include "SRC/GFX/DRIVER/VIDINIT.H"
#include "bak_game_end.h"

/* viddet.detect u8 requested, u16 auto-detect word -> u16 adapter */
static void cmd_detect(DtIn *in, DtOut *out) {
    g_bRequestedVideoMode = dt_u8(in);
    u16 word = dt_u16(in);
    g_graphics_context_render.bVideoAutoDetectEnabled = (unsigned char)word;
    g_graphics_context_render.bReserved_0xab5 = (unsigned char)(word >> 8);
    dt_put_u16(out, (u16)video_detect_adapter());
}

/* viddet.shutdown u8 saved mode -> u16 result, u8 saved mode afterwards */
static void cmd_shutdown(DtIn *in, DtOut *out) {
    g_bSavedBiosVideoMode = dt_u8(in);
    dt_put_u16(out, (u16)video_shutdown());
    dt_put_u8(out, g_bSavedBiosVideoMode);
}

const DtEntry dt_viddet_entries[] = {
    DT_COMMAND("viddet.detect", cmd_detect),
    DT_COMMAND("viddet.shutdown", cmd_shutdown),
    DT_END,
};
