#include <dos.h>
#include "structs.h"
#include "defines.h"
#include "SRC/SYS/EMS.H"
#include "SRC/SYS/EMSDET.H"

#ifdef BAK_NATIVE
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
#else
s16 ems_detect_driver(void) {
    asm {
        mov     ax, 3567h
        int     INT_DOS
        mov     di, 0ah
        mov     si, offset s_ems_driver_signature
        mov     cx, 8
        repe    cmpsb
    }
    L_check_zf : asm jnz L_no_ems;

    _AH = 0x40;
    geninterrupt(INT_EMS);
    if (_AH)
        goto L_no_ems;

    _AH = 0x41;
    geninterrupt(INT_EMS);
    if (_AH)
        goto L_no_ems;
    g_ems_free_pages = _BX;

    _AH = 0x46;
    geninterrupt(INT_EMS);
    if (_AH)
        goto L_no_ems;
    asm { cmp al, 32h; jc L_check_zf }

    _AH = 0x42;
    geninterrupt(INT_EMS);
    if (_AH)
        goto L_no_ems;
    g_ems_total_pages = _BX;
    if (_BX == 0)
        goto L_no_ems;

    _AH = 0x43;
    geninterrupt(INT_EMS);
    if (_AH)
        goto L_no_ems;
    g_ems_frame_segment = _DX;

    g_ems_present = _AX = 1;
    asm jmp short L_ret;
L_no_ems:
    g_ems_present = _AX = 0;
L_ret:
    return _AX;
}
#endif
