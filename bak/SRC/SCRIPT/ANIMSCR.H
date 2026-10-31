#ifndef BAK_ANIMSCR_H
#define BAK_ANIMSCR_H

#include "structs.h"

extern s16 g_nCurrentChannelSlot;
extern s16 g_current_animation_slot;
extern s16 g_aScriptResourceOpcodes[2];
extern s16 g_nScriptPendingJumpTarget;
extern u16 g_wScriptFrameHoldFlag;
extern s16 g_nCombatResolveStaticThreshold;
extern s16 g_nChapterSnapshotDeadStore;
extern Slot far *g_anim_slots[2];
extern u16 g_wScriptYieldPending;
extern u16 g_wAnimResSfxId;
extern ScriptAnimNode *g_pScriptAnimWalker;
extern ScriptAnimNode *g_pScriptAnimTarget;
extern s16 g_nAnimIterCursor;

extern s16 anim_script_subsystem_init(void);
extern void anim_script_close_all(void);
extern s16 anim_script_open(char *filename, s16 mode);
extern s16 anim_script_activate(s16 slot_plus_one);
extern void anim_script_close(s16 slot_plus_one);
extern s16 anim_script_tick(void);
extern void anim_script_rndr_all_objs(void);
extern s16 anim_script_channel_is_busy(s16 channel_id);
extern void anim_script_channel_start(s16 channel_id);
extern void anim_script_chan_request_pause(s16 channel_id);
extern void anim_script_channel_stop(s16 channel_id);
extern void anim_script_channel_pause_after(s16 channel_id);
extern void anim_script_start_all_channels(s16 channel_ref);
extern void anim_script_pause_all_channels(s16 script_object_id);
extern void anim_script_stop_all_channels(s16 script_object_id);
extern void anim_script_pause_all_chans(s16 script_object_id);
extern void anim_script_delete_all_channels(s16 script_object_id);
extern s16 anim_script_has_channel(s16 channel_id);
extern void anim_script_reset_all_objects(void);
extern ScriptAnimNode *anim_script_find_object(u16 wTag, u16 wIdB);
extern s16 far *anim_script_combat_resolve_loop(s16 far *script_pc);
extern s16 anim_script_find_channel(s16 channel_id);
extern u16 far *anim_script_channel_step(u16 far *script_pc);
extern u16 far *anim_script_dispatch_opcode(u16 far *script_pc);
extern s16 anim_script_iter_next_object(u16 *out_id);
extern void anim_script_iter_first_object(u16 *out_id);

#endif
