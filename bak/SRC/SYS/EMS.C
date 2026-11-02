#include <dos.h>
#include "structs.h"
#include "defines.h"
#include "SRC/SYS/EMS.H"

/*
 * EMS, natively: each allocation is a block of plain memory of its own (bak_plain_alloc,
 * outside the segment table), and mapping one gives the block whole, with no page frame and no
 * copying. The original keeps its allocations as chains of 16 KB pages in the EMS memory
 * INT 67h gives it (every free page, taken at start-up) and maps four pages at a time into the
 * 64 KB page frame, so a caller maps an allocation again before each use, as they all do. The
 * allocations are numbered from 1 as its chains are (0: none), and its count of free pages is
 * kept as it keeps it: the start-up check reads it (BOOT.C).
 */
#define EMS_PAGE_SIZE 0x4000UL
#define EMS_TOTAL_PAGES 512 /* the 8 MB the emulated driver offered */
#define EMS_MAX_ALLOCS 299  /* numbers under 300, as the original's chains mostly were */

typedef struct EmsAlloc {
    unsigned char *block;
    s16 pages;
} EmsAlloc;

static EmsAlloc s_allocs[EMS_MAX_ALLOCS + 1];

unsigned short g_free_memory_kb; /* free 16 KB pages, whatever the name says */
s16 g_ems_present = 0;

s16 ems_init(void) {
    g_free_memory_kb = EMS_TOTAL_PAGES;
    g_ems_present = 1;
    return 1;
}

unsigned short ems_get_free_memory(void) {
    return g_free_memory_kb;
}

void ems_shutdown(void) {
    s16 i;

    if (g_ems_present) {
        for (i = 1; i <= EMS_MAX_ALLOCS; i++) {
            ems_free_pages(i);
        }
        g_ems_present = 0;
    }
}

s16 ems_alloc_pages(u32 size_bytes) {
    s16 pages;
    s16 id;

    if (g_ems_present == 0 || size_bytes == 0)
        return 0;

    pages = (s16)((s32)size_bytes >> 14);
    if (size_bytes & 0x3fff)
        pages++;
    if (pages <= 0 || pages > (s16)g_free_memory_kb)
        return 0;

    for (id = 1; id <= EMS_MAX_ALLOCS && s_allocs[id].block != NULL; id++)
        ;
    if (id > EMS_MAX_ALLOCS)
        return 0;
    if ((s_allocs[id].block = bak_plain_alloc((u32)pages * EMS_PAGE_SIZE)) == NULL)
        return 0;
    s_allocs[id].pages = pages;
    g_free_memory_kb -= pages;
    return id;
}

void ems_free_pages(s16 page_chain_1based) {
    EmsAlloc *a;

    if (g_ems_present == 0 || page_chain_1based <= 0 || page_chain_1based > EMS_MAX_ALLOCS)
        return;
    a = &s_allocs[page_chain_1based];
    if (a->block == NULL)
        return;
    bak_plain_free(a->block);
    a->block = NULL;
    g_free_memory_kb += a->pages;
}

/* The original maps the four pages from the one offset falls in and gives the place in the
 * frame: natively the place in the block. */
unsigned char far *ems_map_at_offset(s16 page_chain_1based, s32 offset) {
    if (g_ems_present == 0 || page_chain_1based <= 0 || page_chain_1based > EMS_MAX_ALLOCS ||
        s_allocs[page_chain_1based].block == NULL)
        return NULL;
    return s_allocs[page_chain_1based].block + offset;
}

unsigned char far *ems_map_resource_pages(s16 page_id) {
    return ems_map_at_offset(page_id, 0);
}
