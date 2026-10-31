#ifndef BAK_MAINMENU_H
#define BAK_MAINMENU_H

#include "structs.h"

extern EnginePrefs *g_engine_prefs;
extern s16 g_nMenuReentering;
extern u16 g_wSaveSlotDirValid;
extern u16 g_wSaveFileValid;
extern u16 g_wMenuCursorPreset;
extern u16 g_wCurrentSaveSlotKey;
extern u16 g_wCurrentSaveFileKey;
extern s16 g_wBookmarkFmapX;
extern s16 g_wBookmarkFmapY;
extern u16 g_wBookmarkCompassIcon;
extern char g_szSaveSlotDirName_51cc[13];
extern unsigned char g_abSaveFileHeader[90];

extern s16 mainmenu_save_cfg_load_settings(void);
extern void mainmenu_save_cfg_free_settings(void);
extern s16 mainmenu_save_main_menu(s16 reentering);
extern s16 mainmenu_save_confirm_abort(void);
extern s16 mainmenu_save_load_game_dialog(void);
extern s16 mainmenu_save_save_game_dialog(void);
extern s16 mainmenu_save_prefs_menu_run(void);
extern s16 mainmenu_save_rmdir_recursive(char *dir_path);
extern s16 mainmenu_save_filename_validate(char *filename);
extern void mainmenu_save_show_ctrd_modal(void);
extern s16 mainmenu_save_games_dir_exists(void);
extern s16 mainmenu_save_games_dir_create(void);
extern s16 mainmenu_save_any_exists(void);
extern void mainmenu_save_bookmark(void);
extern void mainmenu_save_party_to_tmp(void);
extern s16 mainmenu_save_file_read_header(char *path, void *sig_block, s16 *ds_state_1,
                                          s16 *ds_state_2, s16 *ds_state_3, s16 *ds_state_4);
extern s16 mainmenu_save_scan_highest_slot(void);
extern s16 mainmenu_save_slot_is_at_cap(char *entry);
extern void mainmenu_save_slot_exists(void);
extern void mainmenu_save_request_load_game(void);
extern s16 mainmenu_save_enum_directories(ListWidget *list);
extern s16 mainmenu_save_parse_filename(char *filename, char *out_name, s16 *out_slot);
extern s16 mainmenu_save_enum_files_slot(ListWidget *list, char *slot_name, s16 slot_id);
extern s16 mainmenu_save_parse_read_title(char *title_buf, char *sig_block, s16 *out_save_id,
                                          char *slot_name, s16 slot_id, s16 *ds_state);
extern s16 mainmenu_savelist_insert_sorted(ListWidget *list, char *label, u16 key);
extern s16 mainmenu_savelist_nextfree_key(ListWidget *list);
extern s16 mainmenu_savelist_nextfree_nobm(ListWidget *list);
extern s16 mainmenu_savelist_find_by_text(ListWidget *list, char *text);
extern s16 mainmenu_savelist_find_by_name(ListWidget *list, char *name);

#endif
