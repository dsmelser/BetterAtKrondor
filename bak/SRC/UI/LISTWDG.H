#ifndef BAK_LISTWDG_H
#define BAK_LISTWDG_H

#include "structs.h"

extern ListWidget *listwidget_attach(short x, short y, short width, short line_count,
                                     short capacity, MenuPage *parent, short child_idx);
extern ListWidget *listwidget_load(char *filename, MenuPage *parent, short child_idx);
extern s16 listwidget_destroy(ListWidget *list);
extern s16 listwidget_capacity(ListWidget *list);
extern s16 listwidget_count(ListWidget *list);
extern s16 listwidget_insert_item(ListWidget *list, s16 pos, char *text, unsigned short payload);
extern s16 listwidget_remove_item(ListWidget *list, s16 idx);
extern s16 listwidget_get_current_entry(ListWidget *list, char **out_label, unsigned short *out_value);
extern s16 listwidget_get_entry_at(ListWidget *list, s16 index, char **out_label,
                                   unsigned short *out_value);
extern s16 listwidget_last_index(ListWidget *list);
extern s16 listwidget_set_selection(ListWidget *list, s16 index);
extern s16 listwidget_scroll_up(ListWidget *list);
extern s16 listwidget_scroll_down(ListWidget *list);
extern s16 listwidget_get_selection(ListWidget *list);
extern s16 listwidget_handle_click(ListWidget *list, unsigned short *out_confirm);
extern s16 listwidget_hit_test(ListWidget *list);
extern s16 listwidget_ensure_visible(ListWidget *list);
extern void listwidget_draw(ListWidget *list);

#endif
