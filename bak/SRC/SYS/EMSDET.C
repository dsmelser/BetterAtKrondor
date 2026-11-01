#include <dos.h>
#include "structs.h"
#include "defines.h"
#include "SRC/SYS/EMS.H"
#include "SRC/SYS/EMSDET.H"

/*
 * The original's INT 67h sequence, against the emulated EMS (port/platform/dosmem.c).
 * Note what each global really receives: AH=41h returns the page-frame segment (kept in
 * g_ems_free_pages), AH=42h the unallocated page count (g_ems_total_pages), and AH=43h,
 * which allocates all of those pages, the handle (g_ems_frame_segment).
 */
s16 ems_detect_driver(void) {
    g_ems_free_pages = bak_ems_frame_segment();
    g_ems_total_pages = bak_ems_unallocated_pages();
    if (g_ems_total_pages == 0)
        return g_ems_present = 0;
    g_ems_frame_segment = bak_ems_allocate(g_ems_total_pages);
    if (g_ems_frame_segment == 0)
        return g_ems_present = 0;
    return g_ems_present = 1;
}
