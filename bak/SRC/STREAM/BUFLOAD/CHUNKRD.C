#include <dos.h>
#include "structs.h"
#include "SRC/STREAM/BUFLOAD/CHUNKRD.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/SYS/FARTHUNK.H"
#include "SRC/SYS/DOSMEM.H"
#include "SRC/STREAM/RESLOAD/IFFREAD.H"
#include "SRC/SYS/PANICF.H"

/**
 * @brief Chunk-read session's file-close-ownership flag.
 *
 * -1 = idle (no active session); 0 = borrowed an already-open handle (leave it
 * open); 1 = the session opened the file itself and owns the close.
 */
s16 g_nChunk_session_owns_close = -1;

u32 chunkread_fread_far_large(char far *dst, u32 len, ResFile *file) {
    char far *p;
    char *scratch;
    u32 remaining;
    u16 chunks_per_window;
    u16 window_left;
    char stackbuf[256];
    s16 n;
    register s16 size;

    remaining = len;

    size = 0x4000;
    while (size != 0 && (scratch = my_malloc(size)) == 0) {
        if (size > 0x800)
            size >>= 1;
        else
            size -= 0x100;
    }
    if (size == 0) {
        scratch = stackbuf;
        size = 0x100;
    }

    if (len & 0xffff0000L)
        chunks_per_window = (u16)(0x10000L / (s32)size);
    else
        chunks_per_window = 0;

    window_left = chunks_per_window;
    p = dst;
    while (len != 0) {
        n = (s32)len < (s32)size ? (s16)len : size;
        n = res_fread(scratch, 1, n, file);
        if (n == 0)
            break;
        memcpy(p, scratch, n);
        p += n;
        len -= n;
        if (chunks_per_window != 0 && --window_left == 0) {
            *(char huge **)&dst += 0x10000L;
            window_left = chunks_per_window;
            p = dst;
        }
    }

    if (scratch != 0)
        my_free(scratch);
    return remaining - len;
}

s16 far chunkread_session_close(ResFile *file) {

    g_nChunk_session_owns_close != -1 || panic(NULL);
    if (g_nChunk_session_owns_close != 0)
        cached_file_close(file);
    g_nChunk_session_owns_close = -1;
    return 0; /* the original returned whatever AX held; no caller reads it */
}

ResFile *far chunkread_session_open(FileRef *file, char *chunk_id) {
    (g_nChunk_session_owns_close == -1) || panic(NULL);
    if (is_file_cached(file) == 0) {
        if ((file = cached_file_open(file)) == 0)
            return 0;
        g_nChunk_session_owns_close = 1;
    } else {
        g_nChunk_session_owns_close = 0;
    }
    if (chunk_seek(file, chunk_id, 0) == -1L) {
        chunkread_session_close(file);
        return 0;
    }
    return file;
}

void far *chunkread_read_all_far(FileRef *file, char *chunk_id) {
    void far *buf;
    u32 size;

    buf = (void far *)0;
    if ((file = chunkread_session_open(file, chunk_id)) != 0) {
        size = cached_file_chunk_size(file);
        buf = alloc_far(size, 0);
        if (buf != 0) {
            (FP_OFF(buf) == 0) || panic(NULL);
            chunkread_fread_far_large(buf, size, file);
        }
        chunkread_session_close(file);
    }
    return buf;
}
