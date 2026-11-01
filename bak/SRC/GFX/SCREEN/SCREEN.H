#ifndef BAK_SCREEN_H
#define BAK_SCREEN_H

#include "structs.h"

extern short g_cursor_hidden;
extern short g_cursor_prev_shape;
extern ImageRecord **g_cursor_state;
extern unsigned char far *g_pPalQueuedForFlip;
extern short g_nCursorX;
extern short g_nCursorY;
extern ImageRecord **g_nCursorStateSaved;
extern unsigned char far *g_pSavedCursorBuf1;
extern unsigned char far *g_pSavedCursorBuf2;
extern CursorState g_cursor_prev_x[2];
extern unsigned char far *g_pActiveCursorBuf;
extern unsigned char far *g_pCursorScratchBuf1;
extern unsigned char far *g_pCursorScratchBuf2;

extern s16 screen_cursor_get_position(short *out_x, short *out_y);
extern s16 screen_cursor_set_position(s16 x, s16 y);
extern void screen_cursor_set_hidden_flag(s16 hidden);
extern short screen_cursor_get_x(void);
extern short screen_cursor_get_y(void);
extern ImageRecord **screen_cursor_state_get(void);
extern void screen_cur_set_shape_remember(s16 shape_id);
extern void screen_cursor_set_shape(s16 shape_id);
extern s16 screen_input_poll_confirm_cancel(void);
extern void screen_cursor_load_pack(s16 pack_id);
extern void screen_cursor_unload_pack(void);
extern void screen_cursor_state_push_pop(s16 restore);
extern void screen_cursor_hide(void);
extern void screen_cursor_draw(CursorState *cursor_rec, unsigned char far *buf);
extern void screen_cursor_restore_background(CursorState *cur, unsigned char far *pBgBuffer);
extern void screen_cur_refr_during_long_op(void);
extern void screen_frame_present(void);
extern void screen_frame_sync_buffers_rect(s16 row_start, s16 row_count);
extern void screen_clear_both_pages(void);
extern void screen_clear_back_buffer(void);
extern void screen_render_main_frame(char *menu_only);
extern void screen_cursor_show_busy(void);
extern void screen_cursor_restore_shape(void);
extern void screen_scene_reset_clip_cur(void);
extern void screen_wipe_horizontal(s16 x, s16 y, s16 w, s16 h);
extern void screen_frame_flip(void);

#endif
