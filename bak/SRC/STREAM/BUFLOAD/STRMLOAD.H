#ifndef BAK_STRMLOAD_H
#define BAK_STRMLOAD_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern void far *stream_load_to_buffer(ResFile *file, u32 size, u32 *out_desc,
                                       unsigned short allocTag);

#endif
