#include <mem.h>

#include "structs.h"
#include "SRC/IO/RESFAR.H"
#include "SRC/IO/RESOURCE.H"

#define CHUNK_SIZE 128

s32 res_fread_far(void huge *dest, s32 size, s32 count, ResFile *file) {
    s32 remaining;
    s16 n;
    char buf[CHUNK_SIZE];

    remaining = size * count;
    while (remaining > 0) {
        n = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        res_fread(buf, n, 1, file);
        _fmemcpy(dest, buf, n);
        *(unsigned char huge **)&dest += n;
        remaining -= n;
    }
    return count;
}

s32 res_fwrite_far(void huge *src, s32 size, s32 count, ResFile *file) {
    s32 remaining;
    char buf[CHUNK_SIZE];
    s16 n;

    remaining = size * count;
    while (remaining > 0) {
        n = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        _fmemcpy(buf, src, n);
        res_fwrite(buf, n, 1, file);
        *(unsigned char huge **)&src += n;
        remaining -= n;
    }
    return remaining;
}
