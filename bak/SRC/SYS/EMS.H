#ifndef BAK_EMS_H
#define BAK_EMS_H

#include "structs.h"

extern s16 g_ems_present;
extern u16 g_ems_total_pages;
extern u16 g_ems_free_pages;
extern u16 g_ems_frame_segment;
extern char s_ems_driver_signature[8];
extern u16 g_free_memory_kb;
extern u16 g_wEmsFreeListHead;
extern s16 *g_pEmsPageNextTbl;
extern s16 g_anEmsPageMap[8];

extern s16 ems_init(void);
extern u16 ems_get_free_memory(void);
extern void ems_shutdown(void);
extern s16 ems_alloc_pages(u32 size_bytes);
extern void ems_free_pages(s16 page_chain_1based);
extern unsigned char far *ems_map_at_offset(s16 page_chain_1based, s32 offset);
extern unsigned char far *ems_map_resource_pages(s16 page_id);

#endif
