/*
 * Real-mode segment emulation behind MK_FP / FP_SEG / FP_OFF.
 *
 * Every host block the game can address with a segment owns a range of
 * paragraph numbers, as it would in DOS: a block of N bytes registered at
 * segment S covers S .. S + ceil(N/16) - 1, so MK_FP(S + k, off) is
 * base + 16*k + off. That keeps the game's segment arithmetic working.
 *
 * FP_SEG/FP_OFF of a pointer return the block's base segment and the offset
 * from the block start - the form a far pointer derived from an allocation has
 * in the original (far pointer arithmetic only moves the offset). Offsets past
 * 64 KB fall back to the normalized form, like a huge pointer. Values below 64 KB
 * are no host address: they are segment 0 (MK_FP(0, n) is n), which is what the
 * game leaves when it stores a word into a null far pointer's offset.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bak_port.h"

typedef struct SegBlock {
    uint16_t seg;
    uint32_t paragraphs;
    uint8_t *base;
    uint32_t size;
} SegBlock;

#define MAX_BLOCKS 4096
/* Dynamic registrations take paragraphs from the conventional-memory range, below the
 * video and ROM segments (A000 and up) that the platform registers at fixed values. */
#define FIRST_DYNAMIC_SEG 0x0100
#define LAST_DYNAMIC_SEG 0x9FFF

static SegBlock s_blocks[MAX_BLOCKS];
static int s_blockCount;

static SegBlock *find_by_seg(uint16_t seg) {
    for (int i = 0; i < s_blockCount; i++) {
        SegBlock *b = &s_blocks[i];
        if (seg >= b->seg && (uint32_t)(seg - b->seg) < b->paragraphs)
            return b;
    }
    return NULL;
}

static SegBlock *find_by_ptr(const void *p) {
    const uint8_t *q = p;
    for (int i = 0; i < s_blockCount; i++) {
        SegBlock *b = &s_blocks[i];
        if (q >= b->base && q <= b->base + b->size)
            return b;
    }
    return NULL;
}

static void add_block(uint16_t seg, void *base, uint32_t size) {
    if (s_blockCount == MAX_BLOCKS) {
        fprintf(stderr, "segment table full\n");
        abort();
    }
    SegBlock *b = &s_blocks[s_blockCount++];
    b->seg = seg;
    b->base = base;
    b->size = size;
    b->paragraphs = size ? (size + 15) / 16 : 1;
}

uint16_t bak_seg_register(void *base, uint32_t size) {
    uint32_t need = size ? (size + 15) / 16 : 1;
    /* First fit over the free gaps of the dynamic range. */
    uint32_t candidate = FIRST_DYNAMIC_SEG;
    for (;;) {
        int clash = 0;
        for (int i = 0; i < s_blockCount; i++) {
            SegBlock *b = &s_blocks[i];
            uint32_t lo = b->seg, hi = b->seg + b->paragraphs;
            if (candidate < hi && candidate + need > lo) {
                candidate = hi;
                clash = 1;
            }
        }
        if (!clash)
            break;
    }
    if (candidate + need - 1 > LAST_DYNAMIC_SEG) {
        fprintf(stderr, "out of emulated conventional memory (%u bytes requested)\n",
                (unsigned)size);
        abort();
    }
    add_block((uint16_t)candidate, base, size);
    return (uint16_t)candidate;
}

void bak_seg_register_at(uint16_t seg, void *base, uint32_t size) {
    bak_seg_unregister(seg);
    add_block(seg, base, size);
}

void bak_seg_unregister(uint16_t seg) {
    for (int i = 0; i < s_blockCount; i++) {
        if (s_blocks[i].seg == seg) {
            s_blocks[i] = s_blocks[--s_blockCount];
            return;
        }
    }
}

uint32_t bak_seg_extent(uint16_t seg) {
    SegBlock *b = seg != 0 ? find_by_seg(seg) : NULL;
    if (b == NULL)
        return 0;
    uint32_t skipped = (uint32_t)(seg - b->seg) * 16;
    return b->size > skipped ? b->size - skipped : 0;
}

void *bak_seg_ptr(uint16_t seg) {
    if (seg == 0)
        return NULL; /* MK_FP(0, n) is used for sentinels and null pointers */
    SegBlock *b = find_by_seg(seg);
    if (b == NULL) {
        char what[64];
        snprintf(what, sizeof what, "MK_FP on unmapped segment %04X", seg);
        bak_unimplemented(what, __FILE__, __LINE__);
    }
    return b->base + (uint32_t)(seg - b->seg) * 16;
}

static uint32_t offset_in_block(const void *p, SegBlock **out) {
    SegBlock *b = find_by_ptr(p);
    if (b == NULL) {
        char what[64];
        snprintf(what, sizeof what, "FP_SEG/FP_OFF of unregistered pointer %p", p);
        bak_unimplemented(what, __FILE__, __LINE__);
    }
    *out = b;
    return (uint32_t)((const uint8_t *)p - b->base);
}

uint16_t bak_ptr_seg(const void *p) {
    if ((uintptr_t)p < 0x10000)
        return 0;
    SegBlock *b;
    uint32_t off = offset_in_block(p, &b);
    return off < 0x10000 ? b->seg : (uint16_t)(b->seg + (off >> 4));
}

uint16_t bak_ptr_seg_norm(const void *p) {
    return (uint16_t)(bak_ptr_seg(p) + (bak_ptr_off(p) >> 4));
}

uint16_t bak_ptr_off(const void *p) {
    if ((uintptr_t)p < 0x10000)
        return (uint16_t)(uintptr_t)p;
    SegBlock *b;
    uint32_t off = offset_in_block(p, &b);
    return off < 0x10000 ? (uint16_t)off : (uint16_t)(off & 15);
}
