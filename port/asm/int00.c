/*
 * INT00.ASM in C: the divide-error handler.
 *
 * Installed, the handler catches a DIV or IDIV whose quotient does not fit (or whose
 * divisor is zero), shifts the dividend right by one bit (arithmetically: AX for the
 * byte forms, DX:AX for the word forms) and returns to the same instruction, which runs
 * again. An overflow thus resolves to the quotient of a halved dividend; a zero divisor,
 * or an unsigned dividend that the arithmetic shift cannot shrink, never resolves and
 * the original hangs. Restoring never writes the vector back (the reads that should
 * have done it are dead code).
 *
 * Natively no CPU exception reaches the handler; translated assembly that divides goes
 * through bak_div8/bak_idiv8/bak_idiv16 below, which apply the same rule and stop with a message
 * where the original would hang.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/INPUT/INT00.H"
#include "bak_game_end.h"

void install_int00_handler(void) {
    g_int00_installed = 1;
    /* The vector's previous contents: none natively. */
    g_wInt00SaveOff = 0;
    g_wInt00SaveSeg = 0;
}

void restore_int00_handler(void) {
    if (g_int00_installed != 0)
        g_int00_installed = 0;
}

static _Noreturn void hangs(const char *what) {
    bak_unimplemented(what, __FILE__, __LINE__);
}

uint8_t bak_div8(uint16_t ax, uint8_t divisor, uint8_t *remainder) {
    for (;;) {
        if (divisor == 0)
            hangs("DIV by zero: the original hangs in its divide-error handler");
        if (ax / divisor <= 0xff) {
            if (remainder != NULL)
                *remainder = (uint8_t)(ax % divisor);
            return (uint8_t)(ax / divisor);
        }
        uint16_t shifted = (uint16_t)((int16_t)ax >> 1);
        if (shifted == ax)
            hangs("DIV overflow the divide-error handler cannot resolve: the original hangs");
        ax = shifted;
    }
}

int16_t bak_idiv16(int32_t dxax, int16_t divisor, int16_t *remainder) {
    for (;;) {
        if (divisor == 0)
            hangs("IDIV by zero: the original hangs in its divide-error handler");
        int64_t q = (int64_t)dxax / divisor;
        if (q >= -32768 && q <= 32767) {
            if (remainder != NULL)
                *remainder = (int16_t)((int64_t)dxax % divisor);
            return (int16_t)q;
        }
        dxax >>= 1;
    }
}

int8_t bak_idiv8(int16_t ax, int8_t divisor, int8_t *remainder) {
    for (;;) {
        if (divisor == 0)
            hangs("IDIV by zero: the original hangs in its divide-error handler");
        int q = ax / divisor;
        if (q >= -128 && q <= 127) {
            if (remainder != NULL)
                *remainder = (int8_t)(ax % divisor);
            return (int8_t)q;
        }
        ax = (int16_t)(ax >> 1);
    }
}
