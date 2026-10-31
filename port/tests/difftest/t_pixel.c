/* GFX/RASTER/PIXEL.ASM: clipped pixel access (port/asm/pixel.c). The driver slots are
 * replaced by probes, so a test sees whether the call got through to the driver. */
#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/GEN/RNDVTBL.H"
#include "SRC/GFX/RASTER/PIXEL.H"
#include "bak_game_end.h"

static int s_putCalls;

static u16 probe_getpixel(s16 x, s16 y) {
    (void)x;
    (void)y;
    return 0x1234;
}

static void probe_putpixel(s16 x, s16 y, s16 color) {
    (void)x;
    (void)y;
    (void)color;
    s_putCalls++;
}

/* pixel.run x y -> u16 getpixel result, u8 whether putpixel reached the driver */
static void cmd_run(DtIn *in, DtOut *out) {
    s16 x = (s16)dt_u16(in), y = (s16)dt_u16(in);
    void *savedGet = g_renderer_vtable.pfn_getpixel;
    void *savedPut = g_renderer_vtable.pfn_putpixel;
    g_renderer_vtable.pfn_getpixel = (void *)probe_getpixel;
    g_renderer_vtable.pfn_putpixel = (void *)probe_putpixel;
    s_putCalls = 0;
    dt_put_u16(out, getpixel(x, y));
    putpixel(x, y, 7);
    dt_put_u8(out, (uint8_t)s_putCalls);
    g_renderer_vtable.pfn_getpixel = savedGet;
    g_renderer_vtable.pfn_putpixel = savedPut;
}

const DtEntry dt_pixel_entries[] = {
    DT_COMMAND("pixel.run", cmd_run),
    DT_END,
};
