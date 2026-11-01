#ifndef BAK_ANIMSCR_H
#define BAK_ANIMSCR_H

#include "structs.h"

extern short g_nCurrentChannelSlot;
extern short g_current_animation_slot;
extern short g_aScriptResourceOpcodes[2];
extern short g_nScriptPendingJumpTarget;
extern unsigned short g_wScriptFrameHoldFlag;
extern short g_nCombatResolveStaticThreshold;
extern short g_nChapterSnapshotDeadStore;
extern Slot far *g_anim_slots[2];
extern u16 g_wAnimCondResult;
extern u16 g_wAnimSceneChapter;
extern unsigned short g_wScriptYieldPending;
extern unsigned short g_wAnimResSfxId;
extern ScriptAnimNode *g_pScriptAnimWalker;
extern ScriptAnimNode *g_pScriptAnimTarget;
extern short g_nAnimIterCursor;

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
extern ScriptAnimNode *anim_script_find_object(unsigned short wTag, unsigned short wIdB);
extern s16 far *anim_script_combat_resolve_loop(s16 far *script_pc);
extern s16 anim_script_find_channel(s16 channel_id);
extern unsigned short far *anim_script_channel_step(unsigned short far *script_pc);
extern unsigned short far *anim_script_dispatch_opcode(unsigned short far *script_pc);
extern s16 anim_script_iter_next_object(u16 *out_id);
extern void anim_script_iter_first_object(u16 *out_id);

#endif
