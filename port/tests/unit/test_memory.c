/* Plain memory: the blocks with no segment that hold the images' pixels and what the original
 * kept in EMS (port/platform/dosmem.c, bak/SRC/SYS/EMS.C). */
#include <stdint.h>
#include <string.h>

#include "unit.h"

#include "bak_port.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/SYS/EMS.H"
#include "SRC/STREAM/RESLOAD/IMGLOAD.H"
#include "bak_game_end.h"

/* A plain block has no segment, but FP_OFF of a pointer into one is its offset there, as for a
 * DOS block: under 64 KB as is, past it what a normalized pointer keeps. */
void unit_memory_plain_blocks_have_offsets(void) {
    unsigned char *p = bak_plain_alloc(0x12345);
    UNIT_CHECK(p != NULL);
    UNIT_CHECK(p[0] == 0 && p[0x12344] == 0); /* zeroed */
    UNIT_CHECK(FP_OFF(p) == 0);
    UNIT_CHECK(FP_OFF(p + 0x1234) == 0x1234);
    UNIT_CHECK(FP_OFF(p + 0x10017) == 7);
    bak_plain_free(p);
    bak_plain_free(NULL);
}

/* An image placed in a plain block is where a far pointer into a DOS block would put it. */
void unit_memory_images_sit_in_plain_blocks(void) {
    unsigned char *block = bak_plain_alloc(0x18000);
    ImageRecord rec;
    imgload_set_plain(&rec, block, 0x18000, 0x1234);
    UNIT_CHECK(rec.pImageBase == block && rec.wImageOff == 0x1234);
    UNIT_CHECK(rec.nImageExtent == 0x18000);
    imgload_set_plain(&rec, block, 0x17FF9, 0x10019); /* past 64 KB: normalized */
    UNIT_CHECK(rec.pImageBase == block + 0x10010 && rec.wImageOff == 9);
    UNIT_CHECK(rec.nImageExtent == 0x18000 - 0x10010);
    bak_plain_free(block);
}

/* EMS: allocations of whole 16 KB pages, counted as the original counts them, each a plain
 * block that mapping gives whole; numbers from 1, and 0 for none. */
void unit_memory_ems_is_plain_blocks(void) {
    UNIT_CHECK(ems_init() == 1);
    unsigned short pages = ems_get_free_memory();
    UNIT_CHECK(pages == 512);
    s16 a = ems_alloc_pages(65000);  /* 4 pages */
    s16 b = ems_alloc_pages(0x4001); /* 2 */
    UNIT_CHECK(a > 0 && b > 0 && a != b && a < 300 && b < 300);
    UNIT_CHECK(ems_get_free_memory() == pages - 6);
    unsigned char *pa = ems_map_resource_pages(a);
    unsigned char *pb = ems_map_resource_pages(b);
    UNIT_CHECK(pa != NULL && pb != NULL && pa != pb);
    memset(pa, 0xAB, 65000);
    pb[0] = 0xCD;
    UNIT_CHECK(ems_map_resource_pages(a)[64999] == 0xAB); /* still there, mapped or not */
    UNIT_CHECK(ems_map_at_offset(a, 0x5000) == pa + 0x5000);
    UNIT_CHECK(FP_OFF(ems_map_at_offset(b, 0x10)) == 0x10);
    UNIT_CHECK(ems_alloc_pages(0) == 0 && ems_map_resource_pages(0) == NULL);
    UNIT_CHECK(ems_alloc_pages(513UL * 0x4000) == 0); /* more than is free */
    ems_free_pages(a);
    UNIT_CHECK(ems_get_free_memory() == pages - 2);
    UNIT_CHECK(ems_map_resource_pages(a) == NULL);
    ems_shutdown();
    UNIT_CHECK(ems_alloc_pages(16) == 0); /* none after the shutdown */
}
