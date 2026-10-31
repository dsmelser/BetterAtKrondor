/* IO/DOSRW.ASM: dos_read and dos_write (port/asm/dosrw.c) on a DOS model, the twin of
 * test_dosrw.py's: one handle on a file in memory, and a call that fails or comes up short
 * when the test says so. Every call is logged. dosrw.host runs them on a real file instead,
 * through the runtime's handle functions. */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/IO/DOSRW.H"
#include "bak_game_end.h"

extern s32 (*g_dosrwTransfer)(unsigned char function, s16 handle, unsigned char *p, u16 count);

#define FILE_MAX 0x40000

static struct {
    uint8_t data[FILE_MAX]; /* zero past size */
    uint32_t size, pos;
    uint16_t handle;
    int32_t calls, failCall, shortCall;
    uint16_t shortBy;
    const unsigned char *buf;
    uint32_t bufSize;
    DtOut *log;
    int overrun;
} s_dos;

static s32 model(unsigned char function, s16 handle, unsigned char *p, u16 count) {
    int32_t k = s_dos.calls++;
    dt_put_u8(s_dos.log, function);
    dt_put_u16(s_dos.log, (uint16_t)handle);
    dt_put_u16(s_dos.log, count);
    dt_put_u32(s_dos.log, (uint32_t)(p - s_dos.buf));
    if ((uint16_t)handle != s_dos.handle || k == s_dos.failCall)
        return -1;
    uint32_t n = count;
    if (k == s_dos.shortCall)
        n = n > s_dos.shortBy ? n - s_dos.shortBy : 0;
    if (function == 0x3f) {
        uint32_t left = s_dos.size > s_dos.pos ? s_dos.size - s_dos.pos : 0;
        if (n > left)
            n = left;
    } else if (count == 0) {
        if (s_dos.size > s_dos.pos)
            memset(s_dos.data + s_dos.pos, 0, s_dos.size - s_dos.pos);
        s_dos.size = s_dos.pos;
        return 0;
    }
    if ((uint32_t)(p - s_dos.buf) + n > s_dos.bufSize || s_dos.pos + n > FILE_MAX) {
        s_dos.overrun = 1;
        return -1;
    }
    if (function == 0x3f) {
        memcpy(p, s_dos.data + s_dos.pos, n);
    } else {
        memcpy(s_dos.data + s_dos.pos, p, n);
        if (s_dos.pos + n > s_dos.size)
            s_dos.size = s_dos.pos + n;
    }
    s_dos.pos += n;
    return (s32)n;
}

/* dosrw.run <u8 function: 3Fh read, 40h write> <u16 handle> <u32 count>
 *           <u16 the file's handle> <u32 position> <i32 failing call> <i32 short call>
 *           <u16 short by> <u32 file size> <file> <u32 buffer size> <buffer>
 *   -> u32 result, u32 position, u32 file size, file, buffer, then each call:
 *      u8 function, u16 handle, u16 count, u32 offset in the buffer */
static void cmd_run(DtIn *in, DtOut *out) {
    uint8_t function = dt_u8(in);
    uint16_t handle = dt_u16(in);
    uint32_t count = dt_u32(in);
    s_dos.handle = dt_u16(in);
    s_dos.pos = dt_u32(in);
    s_dos.failCall = (int32_t)dt_u32(in);
    s_dos.shortCall = (int32_t)dt_u32(in);
    s_dos.shortBy = dt_u16(in);
    s_dos.size = dt_u32(in);
    if (s_dos.size > FILE_MAX)
        dt_fail("file too large for the model");
    memset(s_dos.data, 0, sizeof s_dos.data);
    memcpy(s_dos.data, dt_bytes(in, s_dos.size), s_dos.size);
    s_dos.bufSize = dt_u32(in);
    unsigned char *buf = malloc(s_dos.bufSize ? s_dos.bufSize : 1);
    memcpy(buf, dt_bytes(in, s_dos.bufSize), s_dos.bufSize);
    s_dos.buf = buf;
    s_dos.calls = 0;
    s_dos.overrun = 0;

    DtOut log = {0};
    s_dos.log = &log;
    s32 (*saved)(unsigned char, s16, unsigned char *, u16) = g_dosrwTransfer;
    g_dosrwTransfer = model;
    s32 result = function == 0x3f ? dos_read(buf, (s32)count, (s16)handle)
                                  : dos_write(buf, (s32)count, (s16)handle);
    g_dosrwTransfer = saved;
    if (s_dos.overrun) {
        free(log.data);
        free(buf);
        dt_fail("a transfer ran past the test's buffer or file");
    }

    dt_put_u32(out, (uint32_t)result);
    dt_put_u32(out, s_dos.pos);
    dt_put_u32(out, s_dos.size);
    dt_put_bytes(out, s_dos.data, s_dos.size);
    dt_put_bytes(out, buf, s_dos.bufSize);
    dt_put_bytes(out, log.data, log.size);
    free(log.data);
    free(buf);
}

static int create(const char *path) {
#ifdef _WIN32
    return _open(path, _O_RDWR | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    return open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
#endif
}

static long seek(int fd, long pos, int whence) {
#ifdef _WIN32
    return _lseek(fd, pos, whence);
#else
    return (long)lseek(fd, pos, whence);
#endif
}

/* dosrw.host <u16 length> <path> then steps on a new file there, each one of
 *   'W' <u32 count> <count bytes>   dos_write of the bytes
 *   'R' <u32 count>                 dos_read into a zeroed buffer
 *   'S' <u32 position>              the handle moved there
 * -> each step's u32 result (the new position for 'S'; then the buffer for 'R'), then
 *    u32 position, u32 size and the file */
static void cmd_host(DtIn *in, DtOut *out) {
    char path[1024];
    uint16_t len = dt_u16(in);
    if (len >= sizeof path)
        dt_fail("path too long");
    memcpy(path, dt_bytes(in, len), len);
    path[len] = '\0';
    int fd = create(path);
    if (fd < 0)
        dt_fail("cannot create %s", path);
    while (dt_remaining(in) != 0) {
        uint8_t kind = dt_u8(in);
        uint32_t n = dt_u32(in);
        if (kind == 'S') {
            dt_put_u32(out, (uint32_t)seek(fd, (long)n, SEEK_SET));
            continue;
        }
        unsigned char *buf = calloc(n ? n : 1, 1);
        if (kind == 'W') {
            memcpy(buf, dt_bytes(in, n), n);
            dt_put_u32(out, (uint32_t)dos_write(buf, (s32)n, (s16)fd));
        } else {
            dt_put_u32(out, (uint32_t)dos_read(buf, (s32)n, (s16)fd));
            dt_put_bytes(out, buf, n);
        }
        free(buf);
    }
    uint32_t pos = (uint32_t)bakrtl_tell(fd);
    uint32_t size = (uint32_t)seek(fd, 0, SEEK_END);
    seek(fd, 0, SEEK_SET);
    unsigned char *file = malloc(size ? size : 1);
    uint32_t got = (uint32_t)bakrtl_read(fd, file, size);
    bakrtl_close(fd);
    dt_put_u32(out, pos);
    dt_put_u32(out, size);
    dt_put_bytes(out, file, got);
    free(file);
}

const DtEntry dt_dosrw_entries[] = {
    DT_COMMAND("dosrw.run", cmd_run),
    DT_COMMAND("dosrw.host", cmd_host),
    DT_END,
};
