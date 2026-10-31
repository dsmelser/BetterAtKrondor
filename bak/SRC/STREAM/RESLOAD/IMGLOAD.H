#ifndef BAK_IMGLOAD_H
#define BAK_IMGLOAD_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern s16 iff_chunk_loader_2f1c(ResFile *file, unsigned short *count, NearPtr *out_handle);
extern NearPtr bak_load_image_record_chunked(FileRef *fname);
extern void free_container_pair(s16 *container);
extern void free_image_record(ImageRecord **record);
extern s16 null_terminated_count(ImageRecord **table);
extern void unpack_2bpp_to_byte_per_pixel(unsigned char huge *src, unsigned char huge *dst, u16 count);

#endif
