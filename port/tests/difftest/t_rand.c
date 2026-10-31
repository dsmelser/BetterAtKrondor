/* RAND.ASM: the game's PRNG (port/asm/rand.c). */
#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/SYS/RAND.H"
#include "bak_game_end.h"

extern u16 g_randPosJ;
extern u16 g_randPosI;
extern u16 g_randLast;
extern u16 g_randState[56];

/* rand.draw <u16 n> -> n draws (rand is bak_rand natively, see bak_native.h) */
static void cmd_draw(DtIn *in, DtOut *out) {
    uint16_t n = dt_u16(in);
    for (uint16_t k = 0; k < n; k++)
        dt_put_u16(out, (uint16_t)bak_rand());
}

static void cmd_reset(DtIn *in, DtOut *out) {
    (void)in;
    (void)out;
    _rand_reset_indices();
}

const DtEntry dt_rand_entries[] = {
    DT_COMMAND("rand.draw", cmd_draw),
    DT_COMMAND("rand.reset_indices", cmd_reset),
    DT_GLOBAL(g_randPosJ),
    DT_GLOBAL(g_randPosI),
    DT_GLOBAL(g_randLast),
    DT_GLOBAL(g_randState),
    DT_END,
};
