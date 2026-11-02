/*
 * DOS conventional memory (INT 21h AH=48h/49h) for the game, and plain memory for the data no
 * segment needs to reach.
 *
 * DOS blocks are host allocations registered in the segment table, so the far
 * pointers the game builds from them (seg:0000, segment arithmetic) resolve. The table's
 * conventional range holds about 650 KB, as DOS did.
 *
 * Plain blocks are host allocations outside it, of any size: the images' pixels, which their
 * records reach by pointer, and what the original kept in EMS (SYS/EMS.C), which it reached
 * through the page frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bak_port.h"
#include "platform.h"

/* ---- DOS memory ------------------------------------------------------------------ */

/* What the original saw as conventional memory; it only gates the boot check. */
#define DOS_FREE_BYTES 0xA0000u

/*
 * Slack after every block. The original sometimes writes past the end of a block (the
 * LZW decoder spells long strings beyond its scratch area); in DOS that lands in
 * whatever memory follows, natively it must not reach the host heap.
 */
#define BLOCK_SLACK 0x1000u

/* Every block starts zeroed, asked or not: what the host's allocator left there (which
 * differs between debug, release and sanitized builds) must not reach a run. */
void *bak_dos_alloc(uint32_t size, int zeroFill) {
    (void)zeroFill;
    uint32_t rounded = (size + 15u) & ~15u;
    uint32_t bytes = (rounded ? rounded : 16) + BLOCK_SLACK;
    void *p = calloc(1, bytes);
    if (p == NULL)
        return NULL;
    bak_seg_register(p, rounded);
    return p;
}

int16_t bak_dos_free(void *block) {
    if (block == NULL)
        return 9; /* DOS error 9: invalid memory block address */
    bak_seg_unregister(bak_ptr_seg(block));
    free(block);
    return 0;
}

uint32_t bak_dos_largest_free_block(void) {
    return DOS_FREE_BYTES;
}

/* ---- plain memory ------------------------------------------------------------------ */

/* Zeroed, with a DOS block's slack after it; no segment, but FP_OFF knows a pointer's offset
 * in it (segments.c). */
void *bak_plain_alloc(uint32_t size) {
    uint32_t rounded = (size + 15u) & ~15u;
    void *p = calloc(1, (rounded ? rounded : 16) + BLOCK_SLACK);
    if (p != NULL)
        segments_note_plain(p, rounded);
    return p;
}

void bak_plain_free(void *block) {
    if (block == NULL)
        return;
    segments_forget_plain(block);
    free(block);
}
