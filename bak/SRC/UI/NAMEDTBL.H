#ifndef BAK_NAMEDTBL_H
#define BAK_NAMEDTBL_H

#include "structs.h"

extern s16 *namedtbl_load(char *filename);
extern s16 namedtbl_free(s16 *table);
extern void namedtbl_labels_draw_all(s16 *table);
extern void namedtbl_label_draw(s16 *label);

#endif
