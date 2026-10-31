/* R3D/CORE/R3DTRIG.ASM: table trigonometry (port/asm/r3dtrig.c). Each command takes a run
 * of arguments and answers with one result word for each. */
#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/R3D/CORE/R3D.H"
#include "bak_game_end.h"

static s16 arg(DtIn *in) {
    return (s16)dt_u16(in);
}

/* r3dtrig.sin angle* -> sin* */
static void cmd_sin(DtIn *in, DtOut *out) {
    while (dt_remaining(in) != 0)
        dt_put_u16(out, (u16)r3d_tbl_sin(arg(in)));
}

/* r3dtrig.cos angle* -> cos* */
static void cmd_cos(DtIn *in, DtOut *out) {
    while (dt_remaining(in) != 0)
        dt_put_u16(out, (u16)r3d_tbl_cos(arg(in)));
}

/* r3dtrig.asin value* -> angle* */
static void cmd_asin(DtIn *in, DtOut *out) {
    while (dt_remaining(in) != 0)
        dt_put_u16(out, (u16)r3d_tbl_asin(arg(in)));
}

/* r3dtrig.atan2 (y x)* -> angle* */
static void cmd_atan2(DtIn *in, DtOut *out) {
    while (dt_remaining(in) != 0) {
        s16 y = arg(in), x = arg(in);
        dt_put_u16(out, (u16)r3d_tbl_atan2(y, x));
    }
}

/* r3dtrig.atan_raw idx* -> entry* */
static void cmd_atan_raw(DtIn *in, DtOut *out) {
    while (dt_remaining(in) != 0)
        dt_put_u16(out, (u16)r3d_tbl_atan_lookup_raw(arg(in)));
}

const DtEntry dt_r3dtrig_entries[] = {
    DT_COMMAND("r3dtrig.sin", cmd_sin),           DT_COMMAND("r3dtrig.cos", cmd_cos),
    DT_COMMAND("r3dtrig.asin", cmd_asin),         DT_COMMAND("r3dtrig.atan2", cmd_atan2),
    DT_COMMAND("r3dtrig.atan_raw", cmd_atan_raw), DT_END,
};
