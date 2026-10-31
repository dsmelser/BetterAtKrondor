/* INPUT/MOUSE.ASM: the mouse module (port/asm/mouse.c) over the emulated INT 33h driver.
 * Driver calls are recorded; the game's handler is a probe. */
#include <stdlib.h>
#include <string.h>

#include "difftest.h"
#include "bak_video.h"
#include "../../drivers/vga.h"
#include "../../platform/vgaregs.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/INPUT/MOUSE.H"
#include "bak_game_end.h"

static DtOut *s_trace;
static int s_handlerCalls;

static void record(uint16_t ax, uint16_t cx, uint16_t dx) {
    dt_put_u16(s_trace, ax);
    dt_put_u16(s_trace, cx);
    dt_put_u16(s_trace, dx);
}

static void probe_handler(void) {
    s_handlerCalls++;
}

/* Replies start with the result, then the recorded driver calls (6 bytes each). */
static void begin(DtOut *out) {
    s_trace = out;
    g_int33Trace = record;
}

static void end(void) {
    g_int33Trace = NULL;
}

/* mouse.install -> s16 result, calls */
static void cmd_install(DtIn *in, DtOut *out) {
    DtOut calls = {0};
    (void)in;
    begin(&calls);
    s16 r = mouse_install();
    end();
    dt_put_u16(out, (u16)r);
    dt_put_bytes(out, calls.data, calls.size);
    free(calls.data);
}

/* mouse.clip x y w h -> calls */
static void cmd_clip(DtIn *in, DtOut *out) {
    s16 x = (s16)dt_u16(in), y = (s16)dt_u16(in), w = (s16)dt_u16(in), h = (s16)dt_u16(in);
    begin(out);
    mouse_set_cursor_clip_rect(x, y, w, h);
    end();
}

/* mouse.restore -> s16 result, calls */
static void cmd_restore(DtIn *in, DtOut *out) {
    DtOut calls = {0};
    (void)in;
    begin(&calls);
    s16 r = mouse_restore_handler();
    end();
    dt_put_u16(out, (u16)r);
    dt_put_bytes(out, calls.data, calls.size);
    free(calls.data);
}

/* mouse.get_position u16 x, u16 y (initial out values) -> u16 result, u16 x, u16 y */
static void cmd_get_position(DtIn *in, DtOut *out) {
    u16 x = dt_u16(in), y = dt_u16(in);
    u16 r = mouse_get_position(&x, &y);
    dt_put_u16(out, r);
    dt_put_u16(out, x);
    dt_put_u16(out, y);
}

/* mouse.set_position x y -> s16 result, calls */
static void cmd_set_position(DtIn *in, DtOut *out) {
    DtOut calls = {0};
    s16 x = (s16)dt_u16(in), y = (s16)dt_u16(in);
    begin(&calls);
    s16 r = mouse_set_position(x, y);
    end();
    dt_put_u16(out, (u16)r);
    dt_put_bytes(out, calls.data, calls.size);
    free(calls.data);
}

static void cmd_button(DtIn *in, DtOut *out) {
    dt_put_u16(out, (u16)mouse_button_pressed((s16)dt_u16(in)));
}

/* mouse.set_probe u8 on: the game's handler is the probe (on) or none */
static void cmd_set_probe(DtIn *in, DtOut *out) {
    (void)out;
    g_pMouseHandlerCb = dt_u8(in) ? (void *)probe_handler : NULL;
}

/* mouse.callback x y buttons -> u16 handler calls */
static void cmd_callback(DtIn *in, DtOut *out) {
    u16 x = dt_u16(in), y = dt_u16(in), b = dt_u16(in);
    s_handlerCalls = 0;
    mouse_event_callback(x, y, b);
    dt_put_u16(out, (u16)s_handlerCalls);
}

/* mouse.vga [u8 seqIndex, u8 gcIndex, seq[8], gc[16]] -> the same, as they are now */
static void cmd_vga(DtIn *in, DtOut *out) {
    if (dt_remaining(in) != 0) {
        unsigned char seqIndex = dt_u8(in), gcIndex = dt_u8(in);
        memcpy(g_vga.seq, dt_bytes(in, 8), 8);
        memcpy(g_vga.gc, dt_bytes(in, 16), 16);
        bak_port_out8(0x3C4, seqIndex);
        bak_port_out8(0x3CE, gcIndex);
    }
    dt_put_u8(out, bak_port_in8(0x3C4));
    dt_put_u8(out, bak_port_in8(0x3CE));
    dt_put_bytes(out, g_vga.seq, 8);
    dt_put_bytes(out, g_vga.gc, 16);
}

/* mouse.latches u16 load_from, u16 store_to: load the latches from A000:load_from (a
 * read) and store them at A000:store_to (write mode 1, all planes) */
static void cmd_latches(DtIn *in, DtOut *out) {
    (void)out;
    u16 from = dt_u16(in), to = dt_u16(in);
    if (from != 0xFFFF || to != 0xFFFF) {
        if (from != 0xFFFF)
            vga_read_latches(0xA000, from);
        if (to != 0xFFFF)
            vga_write_latches(0xA000, to, 0x0F);
    }
}

const DtEntry dt_mouse_entries[] = {
    DT_COMMAND("mouse.install", cmd_install),
    DT_COMMAND("mouse.clip", cmd_clip),
    DT_COMMAND("mouse.restore", cmd_restore),
    DT_COMMAND("mouse.get_position", cmd_get_position),
    DT_COMMAND("mouse.set_position", cmd_set_position),
    DT_COMMAND("mouse.button", cmd_button),
    DT_COMMAND("mouse.set_probe", cmd_set_probe),
    DT_COMMAND("mouse.callback", cmd_callback),
    DT_COMMAND("mouse.vga", cmd_vga),
    DT_COMMAND("mouse.latches", cmd_latches),
    DT_GLOBAL(g_mouse_x_mickeys),
    DT_GLOBAL(g_mouse_y_mickeys),
    DT_GLOBAL(g_mouse_installed),
    DT_GLOBAL(g_mouse_buttons),
    DT_GLOBAL(g_bVgaGcReg5ModeA),
    DT_GLOBAL(g_bVgaGcReg5ModeB),
    DT_GLOBAL(g_wSavedVgaGcReg0AndIdx),
    DT_GLOBAL(g_wSavedVgaGcReg1And4),
    DT_GLOBAL(g_wSavedVgaGcReg5And8),
    DT_GLOBAL(g_wSavedVgaScReg2AndIdx),
    DT_GLOBAL(g_bSavedVgaGcReg3),
    DT_END,
};
