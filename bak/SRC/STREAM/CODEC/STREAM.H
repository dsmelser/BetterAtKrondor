#ifndef BAK_STREAM_H
#define BAK_STREAM_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern s16 stream_open(s16 codec_kind, ResFile *file, char *mode, u32 size);
extern s16 stream_open_from_memory(s16 codec_id, unsigned char huge *pSrc, char *mode, u32 size);
extern s16 stream_close(s16 stream_id);
extern u16 stream_read(s16 handle, void far *dest, u16 count);
extern u16 stream_write(s16 stream_id, void far *pSrc, u16 count);
extern u32 stream_size(s16 stream_id);
extern s32 stream_seek(s16 handle, s32 position, s16 origin);
extern s16 stream_rewind(s16 stream_id);

#endif
