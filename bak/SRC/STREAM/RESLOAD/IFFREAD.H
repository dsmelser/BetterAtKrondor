#ifndef BAK_IFFREAD_H
#define BAK_IFFREAD_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern IffResReader *near iff_reader_find(FileRef *key);
extern void near bakfile_reset_preserve_cursor(IffResReader *reader);
extern s16 near strncmp_eq(char *a, char *b, s16 n);
extern IffResReader *cache_slot_read(IffResReader *dst, FileRef *key);
extern s16 cache_slot_write(IffResReader *record);
extern ResFile *cached_file_open(char *filename);
extern s32 near chunk_seek_rollback(IffResReader *reader);
extern s32 chunk_seek(ResFile *handle, char *chunk_id, s16 mode);
extern u32 cached_file_chunk_size(ResFile *file);
extern s16 cached_file_close(ResFile *file);
extern s16 is_file_cached(FileRef *key);

#endif
