/* The Borland runtime's ctype as the game uses it (port/compat/bakrtl.c): the classification
 * table with the DGROUP bytes before it, toupper and tolower. */
#include <stdint.h>

#include "difftest.h"
#include "bak_rtl.h"

/* rtl.toupper / rtl.tolower <u16 c>... -> 2 x u16 each: c is Borland's 16-bit int, which
 * the game's code may widen either way (a char sign-extended, a u16 zero-extended), so it
 * is passed both ways. */
static void convert(DtIn *in, DtOut *out, int (*fn)(int)) {
    while (dt_remaining(in) != 0) {
        uint16_t c = dt_u16(in);
        dt_put_u16(out, (uint16_t)fn((int16_t)c));
        dt_put_u16(out, (uint16_t)fn(c));
    }
}

static void cmd_toupper(DtIn *in, DtOut *out) {
    convert(in, out, bakrtl_toupper);
}

static void cmd_tolower(DtIn *in, DtOut *out) {
    convert(in, out, bakrtl_tolower);
}

const DtEntry dt_rtl_entries[] = {
    DT_COMMAND("rtl.toupper", cmd_toupper),
    DT_COMMAND("rtl.tolower", cmd_tolower),
    DT_GLOBAL(bakrtl_ctype_block),
    DT_END,
};
