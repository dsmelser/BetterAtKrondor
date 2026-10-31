/*
 * FARPTR.ASM in C: the far-pointer runtime.
 *
 * Natively a far pointer is a host pointer: normalizing leaves it as it is, comparing
 * compares addresses, and the 0x7d00-byte chunking that kept offsets from wrapping has
 * nothing to do. What the copies keep is their order of moves, which overlapping
 * copies can observe: fmemcpy_far copies words, then a trailing byte, ascending.
 * The register-convention helpers (normalize
 * and flatten, add/subtract an offset, the two block copies) serve only fmemmove in
 * the original and have no C callers.
 */
#include <string.h>

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/SYS/FARPTR.H"
#include "bak_game_end.h"

/* The normalized form seg:off (offset 0-15) of a far pointer, as a u32. */
u32 normalize_far_ptr(unsigned short offset, unsigned short seg) {
    return (u32)(u16)(seg + (offset >> 4)) << 16 | (offset & 15u);
}

/* memmove: copies forward when the source lies above the destination, backward when
 * below, nothing when they coincide. */
void fmemmove(unsigned char far *dst, unsigned char far *src, u32 n) {
    memmove(dst, src, n);
}

/* The original means to copy one byte first when the destination is odd, but tests it
 * with TEST then JNC, and TEST always clears CF: it always copies words from the start. */
void fmemcpy_far(u32 dst, u32 src, u16 count) {
    unsigned char *d = (unsigned char *)(uintptr_t)dst;
    const unsigned char *s = (const unsigned char *)(uintptr_t)src;
    u16 n = count;
    for (u16 words = n >> 1; words != 0; words--) {
        unsigned char lo = s[0], hi = s[1]; /* MOVSW reads the word, then writes it */
        d[0] = lo;
        d[1] = hi;
        d += 2;
        s += 2;
    }
    if (n & 1)
        *d = *s;
}

void memset_far(void far *dst, s16 value, u32 count) {
    memset(dst, (unsigned char)value, count);
}

s16 far_ptr_cmp(void far *a, void far *b) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x > y ? 1 : x < y ? -1 : 0;
}

unsigned char far *normalize_far_ptr_thunk(unsigned char far *p) {
    return p;
}
