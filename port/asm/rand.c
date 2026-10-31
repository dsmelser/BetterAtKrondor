/*
 * RAND.ASM in C: the game's lagged-Fibonacci generator.
 *
 * Two cursors walk a 56-word ring backwards. Each draw adds the words under them,
 * stores the sum at the second cursor and returns it. As in the original, the cursors
 * are byte offsets into the ring (0x6e is slot 55) and the ring starts from a fixed
 * seed table, so the sequence is the same on every run.
 */
#include <string.h>

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/SYS/RAND.H"
#include "bak_game_end.h"

#define RING_LAST 0x6e /* byte offset of ring slot 55, where a retreating cursor wraps */

u16 g_randPosJ = 0;
u16 g_randPosI = 0;
u16 g_randLast = 0;
u16 g_randState[56] = {
    0xdd23, 0x9533, 0xf3c8, 0x4bc2, 0xf7e2, 0xa3df, 0xaa70, 0x60aa, 0x144b, 0x8948, 0x1928, 0x6b0d,
    0x9e0b, 0xfbf1, 0x8607, 0x465d, 0xeaf4, 0x69fd, 0x6c44, 0x1b27, 0xdb71, 0xe38f, 0xabb4, 0xdc47,
    0x0fde, 0x4d8e, 0xfc7d, 0x8d38, 0x5048, 0xe36f, 0xaa22, 0x2f26, 0x43fa, 0xb5a5, 0x20eb, 0xed85,
    0xf5c3, 0x819f, 0x2d9c, 0xb26d, 0xe92b, 0x97d5, 0x3da1, 0x89ed, 0x1bf4, 0x74b5, 0x002c, 0xd37d,
    0x3b08, 0x0df5, 0xf27e, 0xc270, 0x2589, 0x2e8b, 0x6352, 0xda2d};

static u16 ring_word(u16 byteOffset) {
    u16 v;
    memcpy(&v, (unsigned char *)g_randState + byteOffset, sizeof v);
    return v;
}

/* One step back; a cursor that goes negative wraps to the last slot. */
static u16 retreat(u16 cursor) {
    cursor = (u16)(cursor - 2);
    return (s16)cursor < 0 ? RING_LAST : cursor;
}

/* The game's rand(); bak_native.h renames it so it cannot collide with the C library's. */
s16 bak_rand(void) {
    u16 j = g_randPosJ;
    u16 i = g_randPosI;
    u16 draw = (u16)(ring_word(j) + ring_word(i));
    memcpy((unsigned char *)g_randState + i, &draw, sizeof draw);
    g_randLast = draw;
    g_randPosJ = retreat(j);
    g_randPosI = retreat(i);
    return (s16)g_randLast;
}

void _rand_reset_indices(void) {
    g_randPosJ = 0x30;
    g_randPosI = RING_LAST;
}
