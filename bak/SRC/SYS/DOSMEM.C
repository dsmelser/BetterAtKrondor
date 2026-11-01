/**
 * @file  DOSMEM.C
 * @brief Far (conventional-memory) allocator over DOS INT 21h AH=48h/49h.
 *
 * @details Inline-asm implementations of @ref alloc_far and @ref _freemem; both
 *          are documented at their declarations in @ref DOSMEM.H. @ref alloc_far
 *          converts a byte size to paragraphs by a shr/rcr shift with a
 *          round-up on any 0..15-byte remainder, and reuses the AH=48h call in
 *          two ways: a normal allocation, and a deliberately-oversized 0xFFFF
 *          paragraph request whose failure reports the largest free block.
 */

#include "structs.h"
#include "defines.h"
#include "SRC/SYS/DOSMEM.H"
#include "SRC/SYS/FARPTR.H" /* memset_far — referenced only in inline asm below */

/* Host-backed DOS memory (port/platform/dosmem.c). alloc_far(0xFFFFFFFF) is the
 * original's query: its "pointer" result is the largest free block size in bytes. */
void far *alloc_far(u32 size, s32 flags) {
    if (size == 0xFFFFFFFFUL)
        return (void far *)(uintptr_t)bak_dos_largest_free_block();
    return bak_dos_alloc(size, (flags & ALLOC_FAR_ZERO_FILL) != 0);
}

s16 _freemem(void far *block) {
    return bak_dos_free(block);
}
