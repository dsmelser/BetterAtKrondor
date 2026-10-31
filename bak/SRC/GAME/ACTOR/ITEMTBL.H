#ifndef BAK_ITEMTBL_H
#define BAK_ITEMTBL_H

#include "structs.h"
#include "SRC/GAME/ACTOR/ACTOR.H"

extern ItemRecord far *g_pItemDefTable;
extern u32 *g_pItemPriceTable;

extern s16 itemtbl_slot_value_modifier(ItemSlot far *slot);
extern void itemtbl_free(void);
extern s16 itemtbl_load(void);
extern ItemRecord far *itemtbl_record_ptr(ItemSlot far *slot_ptr);
extern ItemRecord far *itemtbl_record_ptr_by_id(s16 id);
extern s32 *itemtbl_count(void);
extern s32 itemtbl_compute_value(ItemSlot far *slot);
extern s16 itemtbl_find_by_full_record(ItemRecord far *record);
extern s16 itemtbl_inv_count_by_kind(Actor far *inv, u16 kind);
extern s16 itemtbl_partySize_by_kind(u16 kind);
extern s16 itemtbl_inv_consume_one_by_kind(Actor far *inv, u16 kind);
extern s16 itemtbl_pty_consum_one_kind(u16 kind);

#endif
