#ifndef BAK_INVENTOR_H
#define BAK_INVENTOR_H

#include "structs.h"
#include "SRC/GAME/ACTOR/ACTOR.H"

extern ImageRecord **g_pInvSpriteLoAssetTable;
extern ImageRecord **g_pInvSpriteHiAssetTable;
extern ImageRecord **g_pInvLockAssetTable;
extern ImageRecord **g_pInvMiscAssetTable;
extern unsigned char far *g_pCursorSaveBufActive;
extern unsigned char far *g_pCursorSaveBufA;
extern unsigned char far *g_pCursorSaveBufB;
extern u32 g_dwDblClickDeadline;
extern s16 g_nInvSelItemAnchorX;
extern s16 g_nInvSelItemAnchorY;
extern s16 g_nInvSelItemIconDx;
extern s16 g_nInvSelItemIconDy;
extern s16 g_nCursorSaveRectX;
extern s16 g_nCursorSaveRectY;
extern u16 g_inventory_actor_kind;

extern void invui_inspect_image_cleanup(void);
extern void invui_inspect_images_load_once(s16 load_lock_images);
extern void invui_portrait_refresh_animated(s16 portrait_idx, s16 prev_portrait, s16 anim_step);
extern void invui_portr_panel_fill_pulsing(s16 state, s16 phase);
extern void invui_cur_spr_paint_ctrd(ImageRecord *sprite, s16 mode);
extern void invui_render_present_sync(MenuPage *page, Actor far *actor, s16 selected_slot,
                                      s16 highlight_slot);
extern s16 invui_handle_item_drag(MenuPage *page, Actor far *actor, s16 selected_slot,
                                  s16 *p_selected_slot);
extern void invui_draw_text_aligned_shadow(char far *text, s16 x, s16 y, s16 align, s16 fg_color,
                                           s16 shadow_color);
extern ImageRecord *invui_item_sprite_select(ItemSlot far *slot);
extern void invui_blit_spr_ckey_dflt_size(ImageRecord *sprite, s16 x, s16 y, s16 w, s16 h);
extern s16 invui_actor_inventory_kind(Actor far *actor, s16 flag);
extern void invui_status_icons_render(u16 status_bits, s16 x, s16 y);

#endif
