/* INPUT/KEYBOARD.ASM: the keyboard handler and queries (port/asm/keyboard.c), over the
 * emulated BIOS data area and keyboard port. */
#include <string.h>

#include "difftest.h"
#include "../../platform/platform.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GEN/GFXCTX.H"
#include "SRC/INPUT/KEYBOARD.H"
#include "bak_game_end.h"

/* kbd.bda [256 bytes] -> the BIOS data area */
static void cmd_bda(DtIn *in, DtOut *out) {
    unsigned char *bda = bak_bios_data();
    if (dt_remaining(in) != 0)
        memcpy(bda, dt_bytes(in, 256), 256);
    dt_put_bytes(out, bda, 256);
}

/* kbd.isr u8 byte: the byte arrives at port 60h and the handler runs */
static void cmd_isr(DtIn *in, DtOut *out) {
    (void)out;
    unsigned char byte = dt_u8(in);
    if (bak_get_vector(0x09) != isr_keyboard)
        bak_set_vector(0x09, isr_keyboard);
    bak_keyboard_irq(byte);
}

static void cmd_kbhit(DtIn *in, DtOut *out) {
    (void)in;
    dt_put_u16(out, kbhit_read());
}

static void cmd_is_down(DtIn *in, DtOut *out) {
    dt_put_u16(out, (u16)key_is_down((s16)dt_u16(in)));
}

static void cmd_state_get(DtIn *in, DtOut *out) {
    dt_put_u16(out, (u16)key_state_get_via_2cf8_table((s16)dt_u16(in)));
}

/* kbd.install u16 arg -> u16 result */
static void cmd_install(DtIn *in, DtOut *out) {
    dt_put_u16(out, (u16)keyboard_install((s16)dt_u16(in)));
}

static void cmd_restore(DtIn *in, DtOut *out) {
    (void)in;
    dt_put_u16(out, keyboard_restore_handler());
}

static void cmd_axis(DtIn *in, DtOut *out) {
    (void)in;
    (void)out;
    input_axis_accumulate_isr();
}

const DtEntry dt_keyboard_entries[] = {
    DT_COMMAND("kbd.bda", cmd_bda),
    DT_COMMAND("kbd.isr", cmd_isr),
    DT_COMMAND("kbd.kbhit", cmd_kbhit),
    DT_COMMAND("kbd.is_down", cmd_is_down),
    DT_COMMAND("kbd.state_get", cmd_state_get),
    DT_COMMAND("kbd.install", cmd_install),
    DT_COMMAND("kbd.restore", cmd_restore),
    DT_COMMAND("kbd.axis", cmd_axis),
    DT_GLOBAL(g_abAdapterRowStrideShift),
    DT_GLOBAL(g_keyboard_installed),
    DT_GLOBAL(g_bCapsLockEnabled),
    DT_GLOBAL(g_wIsrLastKeyEventDead),
    DT_GLOBAL(g_input_mode_lut),
    DT_GLOBAL(g_bUnused2cfa),
    DT_GLOBAL(g_key_state),
    DT_GLOBAL(g_ext_scancode_from),
    DT_GLOBAL(g_ext_scancode_to),
    DT_GLOBAL(g_bEnhancedKeyboard),
    DT_END,
};
