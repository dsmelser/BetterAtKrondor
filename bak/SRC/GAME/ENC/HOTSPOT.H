#ifndef BAK_HOTSPOT_H
#define BAK_HOTSPOT_H

#include "structs.h"

extern u16 g_nHotspotCount;
extern u16 g_wHotspotIterIdx;
extern u16 g_wHotspotMatchIdx;
extern u16 g_wHotspotEventEnabled;
extern u16 g_bZoneEntryInProgress;
extern u16 g_wHotspotIterCursor;
extern char g_monst_filename_buf[12];
extern char g_aDefFileNames[12][13];
extern s16 g_aDefRecordSizes[12];
extern ZoneHotspot g_aZoneHotspots[10];
extern u16 g_awHotspotPendingFlags[10];

extern void hotspotevt_monst_load_tbl_cur_id(void);
extern void hotspotevt_flags_clear(void);
extern s16 hotspotevt_activate_at_player(void);
extern u16 hotspotevt_disp_pending_events(void);
extern s16 hotspotevt_dispatch_at_point(s16 target_type, unsigned char x, unsigned char y, s16 *out_flag);
extern s16 hotspotevt_play_sound_zone_entry(s16 index);
extern s16 hotspotevt_trap_gates_pass(s16 nIndex);
extern s16 hotspotevt_available(ZoneHotspot *pHotspot);
extern void hotspotevt_dialog_popup_run(ZoneHotspot *pHotspot, s16 *out_skipped,
                                        s16 *out_user_confirmed);
extern void hotspotevt_load_record0_town(ZoneHotspot *pHotspot);
extern void hotspotevt_skill_check_trigger(s16 isAuto, ZoneHotspot *pHotspot, s16 *out_a,
                                           s16 *out_b, s16 *out_c);
extern void hotspotevt_type1_encounter_run(ZoneHotspot *pHotspot, s16 *out_moved);
extern s16 hotspotevt_monst_load_speak(ZoneHotspot *pHotspot);
extern void hotspotevt_maybe_play_random_sfx(ZoneHotspot *pHotspot);
extern void hotspotevt_show_record_message(ZoneHotspot *pHotspot, s16 *out_skipped,
                                           s16 *out_confirmed);
extern void hotspotevt_action_enter_town(ZoneHotspot *evt);
extern void hotspotevt_trap_prefire_scout(s16 nReentry, ZoneHotspot *evt,
                                          s16 *pOut_ambush_just_armed, s16 *pOut_handled,
                                          s16 *pOut_should_fire);
extern void hotspotevt_trap_main_fire(ZoneHotspot *pHotspot, u16 *pOut_status);
extern void hotspotevt_action_try_enter_zone(ZoneHotspot *evt, s16 *pOut_handled,
                                             u16 *pOut_accepted);
extern void hotspotevt_action_enter_zone(ZoneHotspot *evt);
extern void hotspotevt_combat_play_sfx_voice(ZoneHotspot *evt);
extern void hotspotevt_chance_trigger(ZoneHotspot *evt);
extern void hotspotevt_dlg_run_msg_event(ZoneHotspot *evt, s16 *out_status);
extern ZoneHotspot *hotspotevt_find_at_player_tile(s16 mode);
extern ZoneHotspot *hotspotevt_find_at_point(s16 mode, unsigned char x, unsigned char y);
extern ZoneHotspot *hotspotevt_next_entry_19byte(s16 param_1);
extern void hotspotevt_done_reset_all(void);
extern void hotspotevt_done_set(void);
extern void hotspotevt_done_clear(void);
extern s16 hotspotevt_done_read(void);
extern void hotspotevt_scout_tried_clear_all(void);
extern void hotspotevt_scout_tried_set(void);
extern void hotspotevt_scout_tried_clear(void);
extern u16 hotspotevt_scout_tried_read(void);
extern void hotspotevt_scouted_clear_all(void);
extern void hotspotevt_scouted_set(void);
extern void hotspotevt_scouted_clear(void);
extern u16 hotspotevt_scouted_read(void);
extern void hotspotevt_enc_fought_set(u32 key);
extern void hotspotevt_enc_fought_clear(u32 key);
extern u16 hotspotevt_enc_fought_read(u32 key);
extern void hotspotevt_tile_rect_world_ctr(unsigned char *tile_rect, s32 *out_world_xy);
extern s16 hotspotevt_monst_dispatch_by_tag(s32 tag);
extern void hotspotevt_bak_indexed_rec_info(s16 record_id, char **out_filename,
                                            s16 *out_record_size);
extern char hotspotevt_bak_load_indexed_rec(s16 record_id, void *dest, s32 offset);

#endif
