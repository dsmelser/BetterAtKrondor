#ifndef BAK_SAVEGAME_H
#define BAK_SAVEGAME_H

#include "structs.h"

extern char *g_pChapDatFilename;

extern s16 savegame_write(char *filename);
extern s16 savegame_read(char *filename);
extern void savegame_chapter_start_dispatch(s16 chapter);

#endif
