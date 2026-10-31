#ifndef BAK_SPELLFX_H
#define BAK_SPELLFX_H

#include "structs.h"

extern u16 g_aPalEventBitMask[9];
extern unsigned char g_event_caption_glyph_table[9];
extern ImageRecord **g_pCastBmp;

extern void spellfx_cast_and_dispatch(void);
extern void spellfx_palette_fade_slot0(s16 caster_slot, s16 damage, s16 effect_param);
extern void spellfx_combat_play_hit_effect(s16 caster_slot, s16 damage, s16 effect_param);
extern void spellfx_overworld_pal_fade_slot2(s16 caster_slot, s16 damage, s16 effect_param);
extern void spellfx_combat_damage_flash_sfx(s16 param_1, s16 param_2, s16 param_3);
extern void spellfx_palette_event_with_sound(s16 param_1, s16 param_2, s16 param_3);
extern void spellfx_show_msg_pal_fade(s16 param_1, s16 param_2, s16 param_3);
extern void spellfx_save_then_cmap_recs(s16 target, s16 damage);
extern void spellfx_combat_roll_outcome(s16 param_1, s16 param_2);
extern void spellfx_save_cmap_scene_events(s16 target, s16 damage);
extern TimerEventEntry *spellfx_pal_event_timer_upsert(u16 param_1, s32 param_2);
extern void spellfx_pal_event_mask_upd(TimerEventEntry *entry);
extern s16 spellfx_event_mask_test_bit(s16 param_1);
extern s16 spellfx_actor_has_ration(WorldObject far *entry);
extern s16 spellfx_actor_has_special_item(WorldObject far *entry);
extern void spellfx_run_request_cmap_dialog(s16 request_type);
extern void spellfx_cast_bmp_load(void);
extern void spellfx_cast_bmp_free(void);
extern void spellfx_draw_events_caption(void);
extern void spellfx_party_member_take_damage(s16 roster_slot, s16 damage);

#endif
