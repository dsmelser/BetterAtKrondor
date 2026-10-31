/*
 * DOSRW.ASM in C: dos_read and dos_write move a 32-bit count of bytes through a DOS handle
 * in INT 21h calls (AH=3Fh, 40h) of at most 7FFFh bytes. Each call has to move all it asks
 * for: an error or a short transfer gives 0, success the count. The original folds the
 * buffer's offset into its segment before each call, so a transfer may run past 64 KB;
 * natively the buffer is linear.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/IO/DOSRW.H"
#include "bak_game_end.h"

#define DOS_READ 0x3f
#define DOS_WRITE 0x40
#define CHUNK 0x7fff

/* One INT 21h transfer, natively the runtime's raw read or write on the handle; a write of
 * 0 bytes ends the file at the handle's position, as DOS does. Returns AX, the bytes moved,
 * or -1 for an error (the carry). */
static s32 dos_transfer(unsigned char function, s16 handle, unsigned char *p, u16 count) {
    if (function == DOS_READ)
        return bakrtl_read(handle, p, count);
    if (count == 0)
        return bakrtl_chsize(handle, bakrtl_tell(handle)) == 0 ? 0 : -1;
    return bakrtl_write(handle, p, count);
}

/* The differential test puts its DOS model here. */
s32 (*g_dosrwTransfer)(unsigned char function, s16 handle, unsigned char *p,
                       u16 count) = dos_transfer;

/* The original takes the DOS function in ES (as AH). */
static s32 dos_rw_chunked(unsigned char *buf, s32 count, s16 handle, unsigned char function) {
    u32 left = (u32)count;
    for (;;) {
        u16 n = left > CHUNK ? CHUNK : (u16)left;
        if (g_dosrwTransfer(function, handle, buf, n) != n)
            return 0;
        if (left <= CHUNK)
            return count;
        buf += CHUNK;
        left -= CHUNK;
    }
}

s32 dos_read(unsigned char *buf, s32 count, s16 fd) {
    return dos_rw_chunked(buf, count, fd, DOS_READ);
}

s32 dos_write(unsigned char *buf, s32 count, s16 fd) {
    return dos_rw_chunked(buf, count, fd, DOS_WRITE);
}
