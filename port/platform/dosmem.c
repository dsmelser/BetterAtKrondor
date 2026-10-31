/*
 * DOS conventional memory (INT 21h AH=48h/49h) and LIM EMS (INT 67h) for the game.
 *
 * DOS blocks are host allocations registered in the segment table, so the far
 * pointers the game builds from them (seg:0000, segment arithmetic) resolve.
 *
 * EMS: a 64 KB page frame at segment E000 with four 16 KB physical pages. Mapping
 * a logical page writes back the page previously in that physical slot and copies
 * the new one in, which is exactly what bank switching looks like to the program.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bak_port.h"

/* ---- DOS memory ------------------------------------------------------------------ */

/* What the original saw as conventional memory; it only gates the boot check. */
#define DOS_FREE_BYTES 0xA0000u

void *bak_dos_alloc(uint32_t size, int zeroFill) {
    uint32_t rounded = (size + 15u) & ~15u;
    void *p = zeroFill ? calloc(1, rounded ? rounded : 16) : malloc(rounded ? rounded : 16);
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

/* ---- EMS ------------------------------------------------------------------------- */

#define EMS_FRAME_SEG 0xE000
#define EMS_PAGE_SIZE 0x4000u
/* 8 MB, a well-equipped 1993 machine; the game takes every free page at boot. */
#define EMS_TOTAL_PAGES 512
#define EMS_HANDLE 1

static uint8_t *s_frame;
static uint8_t *s_pages;
static int s_allocated;
static int s_mapped[4] = {-1, -1, -1, -1};

static void ems_ensure_frame(void) {
    if (s_frame == NULL) {
        s_frame = calloc(4, EMS_PAGE_SIZE);
        bak_seg_register_at(EMS_FRAME_SEG, s_frame, 4 * EMS_PAGE_SIZE);
    }
}

uint16_t bak_ems_frame_segment(void) {
    ems_ensure_frame();
    return EMS_FRAME_SEG;
}

uint16_t bak_ems_unallocated_pages(void) {
    return s_allocated ? 0 : EMS_TOTAL_PAGES;
}

uint16_t bak_ems_allocate(uint16_t pages) {
    if (s_allocated || pages == 0 || pages > EMS_TOTAL_PAGES)
        return 0;
    ems_ensure_frame();
    s_pages = calloc(pages, EMS_PAGE_SIZE);
    if (s_pages == NULL)
        return 0;
    s_allocated = pages;
    return EMS_HANDLE;
}

void bak_ems_release(uint16_t handle) {
    (void)handle;
    free(s_pages);
    s_pages = NULL;
    s_allocated = 0;
    for (int i = 0; i < 4; i++)
        s_mapped[i] = -1;
}

void bak_ems_map_multiple(uint16_t handle, const int16_t *pairs, int count) {
    (void)handle;
    for (int i = 0; i < count; i++) {
        int logical = (uint16_t)pairs[2 * i] == 0xFFFF ? -1 : pairs[2 * i];
        int physical = pairs[2 * i + 1];
        if (physical < 0 || physical > 3 || logical >= s_allocated) {
            fprintf(stderr, "ems: bad mapping logical %d -> physical %d\n", logical, physical);
            continue;
        }
        uint8_t *slot = s_frame + (uint32_t)physical * EMS_PAGE_SIZE;
        if (s_mapped[physical] >= 0)
            memcpy(s_pages + (uint32_t)s_mapped[physical] * EMS_PAGE_SIZE, slot, EMS_PAGE_SIZE);
        if (logical >= 0)
            memcpy(slot, s_pages + (uint32_t)logical * EMS_PAGE_SIZE, EMS_PAGE_SIZE);
        s_mapped[physical] = logical;
    }
}
