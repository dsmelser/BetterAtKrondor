#ifndef BAK_FONTLOAD_H
#define BAK_FONTLOAD_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern NearPtr g_font_chunk_tag_ptr;

extern s16 font_load(FileRef *filename);
extern void font_unload(s16 slot);

#endif
