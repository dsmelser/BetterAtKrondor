/*
 * PALBLEND.ASM in C: the colour halfway between two palette entries, and the entry from 10h
 * to E6h nearest to it, for the anti-aliased edges of the dialogs' portraits.
 *
 * Each channel's distance must stay under a threshold that starts at 12h and drops to the
 * best total found so far; a total of 2 or less ends the search at once, and a best total from
 * 28h up gives -1 (no colour close enough). The original indexes the palette from the far
 * pointer's segment, ignoring its offset: the palettes it is given come from alloc_far, which
 * returns offset 0, so the pointer itself is the same place.
 */
#include <stdint.h>

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GFX/SPRITE/BLITAA.H"
#include "SRC/GFX/SPRITE/PALBLEND.H"
#include "bak_game_end.h"

#define FIRST_ENTRY 0x10
#define END_ENTRY 0xe7
#define START_THRESHOLD 0x12
#define START_BEST 0x300
#define CLOSE_ENOUGH 2
#define MAX_TOTAL 0x28

s16 palette_blend_lookup(u16 idx_a, u16 idx_b) {
    const unsigned char *pal = (const unsigned char *)(uintptr_t)g_pAaPaletteBuf;
    const unsigned char *a = pal + (idx_a & 0xff) * 3;
    const unsigned char *b = pal + (idx_b & 0xff) * 3;
    unsigned char avg[3];
    u16 best = 0, bestTotal = START_BEST, threshold = START_THRESHOLD;

    for (int c = 0; c < 3; c++)
        avg[c] = (unsigned char)((a[c] + b[c]) >> 1);
    for (u16 i = FIRST_ENTRY; i != END_ENTRY; i++) {
        const unsigned char *entry = pal + i * 3;
        u16 total = 0;
        int c;
        for (c = 0; c < 3; c++) {
            u16 diff = avg[c] >= entry[c] ? avg[c] - entry[c] : entry[c] - avg[c];
            if (diff >= threshold)
                break;
            total += diff;
        }
        if (c < 3 || total >= bestTotal)
            continue;
        if (total <= CLOSE_ENOUGH)
            return (s16)i;
        bestTotal = total;
        best = i;
        if (total < threshold)
            threshold = total;
    }
    return bestTotal < MAX_TOTAL ? (s16)best : -1;
}
