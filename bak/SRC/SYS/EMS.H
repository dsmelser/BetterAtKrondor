#ifndef BAK_EMS_H
#define BAK_EMS_H

#include "structs.h"

extern s16 g_ems_present;
extern unsigned short g_free_memory_kb;

extern s16 ems_init(void);
extern unsigned short ems_get_free_memory(void);
extern void ems_shutdown(void);
extern s16 ems_alloc_pages(u32 size_bytes);
extern void ems_free_pages(s16 page_chain_1based);
extern unsigned char far *ems_map_at_offset(s16 page_chain_1based, s32 offset);
extern unsigned char far *ems_map_resource_pages(s16 page_id);

#endif
