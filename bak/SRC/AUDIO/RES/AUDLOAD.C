#include "structs.h"
#include "SRC/AUDIO/RES/AUDRESIN.H"
#include "SRC/AUDIO/RES/AUDLOAD.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/IO/RESFAR.H"
#include "SRC/AUDIO/RES/POOL.H"
#include "SRC/STREAM/RESLOAD/RELBUF.H"
#include "SRC/AUDIO/RES/AUDRES.H"
#include "SRC/STREAM/BUFLOAD/STRMLOAD.H"

s16 music_chunk_load_and_link(ResFile *file, char mode) {
    s32 size;
    u16 out_size[2];
    AudioListNode far *node;
    u16 scratch;
    s16 allocTag;

    res_fread(&size, 4, 1, file);
    res_fread(&scratch, 2, 1, file);

    if ((node = pool_acquire_buffer(sizeof(AudioListNode), 3)) == 0)
        return 0;

    node->wId = scratch;
    res_fread(&scratch, 1, 1, file);
    *(u16 far *)&node->bPriority = (unsigned char)scratch;
    res_fread(&scratch, 1, 1, file);
    node->wFlags = (unsigned char)scratch;

    allocTag = (node->wFlags & 1) ? 4 : 7;

    size = size - 4;
    node->pData = 0;

    if (mode == 'c') {
        if ((node->pData = pool_acquire_buffer(size, allocTag)) == 0 ||
            res_fread_far(node->pData, size, 1, file) != 1) {
            release_buffer(node, 3);
            return 0;
        }
    } else if (g_nAudioStreamCodec != 0) {
        if ((node->pData = audres_load_chunk_by_mode(file, (u16)size,
                                                     (u16)((u32)size >> 16),
                                                     (u16 *)out_size, allocTag)) == 0) {
            release_buffer(node, 3);
            return 0;
        }
    } else {
        if ((node->pData =
                 stream_load_to_buffer(file, size, (u32 *)out_size, allocTag)) == 0) {
            release_buffer(node, 3);
            return 0;
        }
    }

    node->pNext = g_pActiveAudioListHead;
    node->wSortKey = out_size[0];
    g_pActiveAudioListHead = node;
    return 1;
}
