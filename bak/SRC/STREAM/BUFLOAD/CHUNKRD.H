#ifndef BAK_CHUNKRD_H
#define BAK_CHUNKRD_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern s16 g_nChunk_session_owns_close;

extern u32 chunkread_fread_far_large(char far *dst, u32 len, ResFile *file);
extern s16 chunkread_session_close(ResFile *file);
extern ResFile *chunkread_session_open(FileRef *file, char *chunk_id);
extern void far *chunkread_read_all_far(FileRef *file, char *chunk_id);

#endif
