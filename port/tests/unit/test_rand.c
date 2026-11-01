/* The cosmetic random stream (port/asm/rand.c): the game's generator on a ring of its own. */
#include <stdint.h>
#include <string.h>

#include "unit.h"

int16_t bak_rand(void);
int16_t rand_fx(void);
void _rand_reset_indices(void);
extern uint16_t g_randPosJ, g_randPosI;
extern uint16_t g_randState[56];

void unit_rand_fx_is_the_game_generator_on_its_own_ring(void) {
    /* Nothing has drawn yet: both rings hold the seed. */
    uint16_t ring[56];
    memcpy(ring, g_randState, sizeof ring);
    uint16_t posJ = g_randPosJ, posI = g_randPosI;

    int16_t fx[200];
    for (int i = 0; i < 200; i++)
        fx[i] = rand_fx();
    /* Cosmetic draws leave the game's generator where it was. */
    UNIT_CHECK(memcmp(ring, g_randState, sizeof ring) == 0);
    UNIT_CHECK(g_randPosJ == posJ && g_randPosI == posI);

    /* From the same start, the game's stream gives the same sequence. */
    _rand_reset_indices();
    int same = 1;
    for (int i = 0; i < 200; i++)
        same &= bak_rand() == fx[i];
    UNIT_CHECK(same);
    /* Each goes on from its own place, untouched by the other: the 201st draws agree too. */
    int16_t next = rand_fx();
    UNIT_CHECK(bak_rand() == next);
}
