#ifndef BAK_CIPHER_H
#define BAK_CIPHER_H

#include "structs.h"

extern unsigned char far *g_pCipherTextEnd;
extern char far *g_pCipherTextBase;
extern char far *g_apCipherDialRows[10];
extern s16 *g_pCipherLetterSlots;
extern s16 g_nCipherRowCount;
extern s16 g_nCipherRowWidth;

extern s16 cipher_dial_puzzle_run(s16 nPuzzleId);
extern void cipher_puzzle_parse_table(unsigned char far *pTableData);
extern void cipher_puzzle_layout_letters(MenuPage *page);
extern s16 cipher_puzzle_is_solved(void);
extern void cipher_anim_bevel_text_dropin(MenuEntry *pEntry, s16 nCol);
extern void cipher_menu_draw_hotkey_labels(MenuPage *page);
extern void cipher_draw_bevelled_box(s16 x, s16 y, s16 w, s16 h);

#endif
