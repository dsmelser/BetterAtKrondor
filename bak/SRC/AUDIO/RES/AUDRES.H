#ifndef BAK_AUDRES_H
#define BAK_AUDRES_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

extern unsigned char far *audres_load_chunk_by_mode(ResFile *file, u16 size_lo, u16 size_hi,
                                            u16 *out_size, u16 allocTag);
extern void audres_release_buffer_chain(AudFragNode far *head);
extern s16 audres_stream_find_tagged_record(s16 stream, unsigned char tag);
extern void far *audres_load_sorted_chain(s16 stream_id);
extern AudFragNode far *audres_sorted_list_insert(AudFragNode far *head, AudFragNode far *new_node);
extern s16 audres_resource_read_fragmented(s16 stream_id, AudFragNode far *fragment_list,
                                           unsigned char far *out_buf, s16 header_size, s16 tag);

#endif
