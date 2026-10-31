/* INPUT/INT00.ASM: the divide-error handler's rule (port/asm/int00.c). */
#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/INPUT/INT00.H"
#include "bak_game_end.h"

/* int00.div u8 signed, u16 ax, u8 divisor -> u8 quotient, u8 remainder */
static void cmd_div(DtIn *in, DtOut *out) {
    unsigned char is_signed = dt_u8(in);
    u16 ax = dt_u16(in);
    unsigned char divisor = dt_u8(in);
    if (is_signed) {
        int8_t rem;
        dt_put_u8(out, (uint8_t)bak_idiv8((int16_t)ax, (int8_t)divisor, &rem));
        dt_put_u8(out, (uint8_t)rem);
    } else {
        uint8_t rem;
        dt_put_u8(out, bak_div8(ax, divisor, &rem));
        dt_put_u8(out, rem);
    }
}

static void cmd_install(DtIn *in, DtOut *out) {
    (void)in;
    (void)out;
    install_int00_handler();
}

static void cmd_restore(DtIn *in, DtOut *out) {
    (void)in;
    (void)out;
    restore_int00_handler();
}

const DtEntry dt_int00_entries[] = {
    DT_COMMAND("int00.div", cmd_div),
    DT_COMMAND("int00.install", cmd_install),
    DT_COMMAND("int00.restore", cmd_restore),
    DT_GLOBAL(g_int00_installed),
    DT_GLOBAL(g_wInt00SaveSeg),
    DT_GLOBAL(g_wInt00SaveOff),
    DT_END,
};
