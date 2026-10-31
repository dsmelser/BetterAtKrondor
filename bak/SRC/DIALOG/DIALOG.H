#ifndef BAK_DIALOG_H
#define BAK_DIALOG_H

#include "structs.h"

extern s16 g_nDialogChapterId;
extern u32 g_dwDialogInputCooldown;
extern unsigned char g_abTextSpeedScale[3];
extern s16 g_nTabWidth;
extern DialogStyleDescriptor g_dialog_style_table[7];
extern u16 g_bDialogWorldAnchorValid;
extern unsigned char g_dialog_running;
extern char g_speaker_kinds[6];
extern char g_speaker_names[6][32];
extern s16 g_nDialogAnchorX;
extern s16 g_nDialogAnchorY;

extern s16 dialog_play_record(u32 record_key, s16 modal_flag);
extern s16 dialog_show_by_key(u32 key, s16 interactive);
extern PDdxRecord dialog_load_record_by_key(u32 record_key, s16 use_offset);
extern s16 dialog_freemem_if_not_null(unsigned char far *ptr);
extern void dialog_screen_wipe_rect_or_full(DDXRecord far *param_1);
extern void dialog_animate_open(DDXRecord far *param_1, s16 param_2);
extern void dialog_viewport_clip_point(s16 x, s16 y);
extern s16 dialog_wait_for_acknowledge(u16 chars, u16 flags, s16 arg3, s16 arg4);
extern void dialog_cmbt_name_assign_kind(s16 slot, s16 kind, s16 aux, s16 name_token);
extern void dialog_input_wait_with_cooldown(s16 mode);
extern void dialog_draw_speech_bubble(char far *text, s16 y_offset);
extern void dialog_render_text_with_tokens(DDXRecord far *param_1, char far *param_2, s16 param_3,
                                           s16 param_4, s16 param_5, s16 param_6);
extern void dialog_frame_draw(DDXRecord far *param_1, s16 far *param_2);
extern void dialog_apply_style_state(DDXRecord far *param_1, void far *param_2);
extern u16 dialog_poll_arrow_or_button(void);
extern void dialog_input_wait_release(s16 animate_frames, s16 check_keyboard);
extern void dialog_combatant_name_table_init(void);
extern void dialog_cutscene_op_noop(unsigned char far *record, s16 a, u16 flags);

#endif
