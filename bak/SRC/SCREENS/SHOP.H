#ifndef BAK_SHOP_H
#define BAK_SHOP_H

#include "structs.h"
#include "SRC/GAME/ACTOR/ACTOR.H"

extern void shop_items_compute_actor_prices(Actor far *actor);
extern s16 shop_sell_item(Actor far *shop_inv, Actor far *actor, ItemSlot far *item_slot);
extern s16 shop_npc_transaction(Actor far *actor, Actor far *merchant, ItemSlot far *item);
extern s16 shop_haggle_attempt_purchase(Actor far *actor, Combatant *combatant,
                                        ItemSlot far *item, s16 mode);

#endif
