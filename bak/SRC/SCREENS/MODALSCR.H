#ifndef BAK_MODALSCR_H
#define BAK_MODALSCR_H

#include "structs.h"
#include "SRC/GAME/ACTOR/ACTOR.H"

extern s16 modalscreen_pending_scene_trans(void);
extern void modalscreen_teleport_dat_load(s16 n_index);
extern void modalscreen_inventory_request(u16 category_mask, s16 discount_pct);
extern void modalscreen_rest_until_time(Actor far *pActor);
extern s16 modalscreen_teleport_spell_run(s16 current_city, s32 cost_base, s16 cost_per_unit);
extern void modalscreen_req_inv_run(s16 kind_or_id, s16 tax, s16 qty_mult, s16 char_slot);

#endif
