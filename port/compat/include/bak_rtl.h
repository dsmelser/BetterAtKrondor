/*
 * bak_rtl.h - the parts of the Borland C++ 3.1 runtime library the game uses
 * that a host libc lacks or implements differently.
 *
 * Each function is provided as bakrtl_<name> and mapped with a function-like
 * macro, so nothing collides with the host's own open/read/itoa/... and only
 * calls are rewritten. File functions resolve DOS paths (backslashes, any case,
 * drive letters) against the game-data and user directories.
 *
 * Included by bak_native.h before structure packing starts, and by port code
 * directly, so every translation unit sees the same layout of struct ffblk.
 */
#ifndef BAK_RTL_H
#define BAK_RTL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "bak_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- dos.h -------------------------------------------------------------- */

typedef void (*BakIsrFn)(void);

void bakrtl_geninterrupt(int intno);
BakIsrFn bakrtl_getvect(int intno);
void bakrtl_setvect(int intno, BakIsrFn isr);

#define geninterrupt(n) bakrtl_geninterrupt(n)
#define getvect(n) bakrtl_getvect(n)
#define setvect(n, isr) bakrtl_setvect((n), (BakIsrFn)(isr))

/* ---- conio.h: port I/O ---------------------------------------------------- */

#define inp(port) bak_port_in8((uint16_t)(port))
#define outp(port, v) bak_port_out8((uint16_t)(port), (uint8_t)(v))
#define outpw(port, v) bak_port_out16((uint16_t)(port), (uint16_t)(v))

/* ---- io.h: handle-based files, Borland O_* flag values ----------------------- */

#define BAKRTL_O_RDONLY 0x0001
#define BAKRTL_O_WRONLY 0x0002
#define BAKRTL_O_RDWR 0x0004
#define BAKRTL_O_CREAT 0x0100
#define BAKRTL_O_TRUNC 0x0200
#define BAKRTL_O_EXCL 0x0400
#define BAKRTL_O_APPEND 0x0800
#define BAKRTL_O_TEXT 0x4000
#define BAKRTL_O_BINARY 0x8000

int bakrtl_open(const char *path, int access, ...);
int bakrtl_close(int fd);
int bakrtl_read(int fd, void *buf, unsigned len);
int bakrtl_write(int fd, const void *buf, unsigned len);

#define open(...) bakrtl_open(__VA_ARGS__)
#define close(fd) bakrtl_close(fd)
#define read(fd, buf, len) bakrtl_read((fd), (buf), (unsigned)(len))
#define write(fd, buf, len) bakrtl_write((fd), (buf), (unsigned)(len))

/* ---- dir.h ----------------------------------------------------------------- */

struct ffblk {
    char ff_reserved[21];
    char ff_attrib;
    uint16_t ff_ftime;
    uint16_t ff_fdate;
    int32_t ff_fsize;
    char ff_name[13];
};

int bakrtl_findfirst(const char *pattern, struct ffblk *ff, int attrib);
int bakrtl_findnext(struct ffblk *ff);
int bakrtl_mkdir(const char *path);
int bakrtl_rmdir(const char *path);
int bakrtl_chdir(const char *path);

#define findfirst(p, ff, attrib) bakrtl_findfirst((p), (ff), (attrib))
#define findnext(ff) bakrtl_findnext(ff)
#define mkdir(p) bakrtl_mkdir(p)
#define rmdir(p) bakrtl_rmdir(p)
#define chdir(p) bakrtl_chdir(p)

/* ---- stdio.h: path-translating variants ---------------------------------------- */

FILE *bakrtl_fopen(const char *path, const char *mode);
int bakrtl_remove(const char *path);

#define fopen(p, m) bakrtl_fopen((p), (m))
#define remove(p) bakrtl_remove(p)

/* ---- stdlib.h / string.h / mem.h extensions ---------------------------------------- */

char *bakrtl_itoa(int value, char *buf, int radix);
char *bakrtl_ltoa(int32_t value, char *buf, int radix);
int bakrtl_stricmp(const char *a, const char *b);
int bakrtl_strnicmp(const char *a, const char *b, size_t n);
uint32_t bakrtl_lrotl(uint32_t value, int count);
void bakrtl_movedata(unsigned srcseg, unsigned srcoff, unsigned dstseg, unsigned dstoff,
                     size_t n);

#define itoa(v, buf, radix) bakrtl_itoa((v), (buf), (radix))
#define ltoa(v, buf, radix) bakrtl_ltoa((v), (buf), (radix))
#define stricmp(a, b) bakrtl_stricmp((a), (b))
#define strnicmp(a, b, n) bakrtl_strnicmp((a), (b), (n))
#define _lrotl(v, n) bakrtl_lrotl((v), (n))
#define _fmemcpy(d, s, n) memcpy((d), (s), (n))
#define _fmemcmp(a, b, n) memcmp((a), (b), (n))
#define _fstrcpy(d, s) strcpy((d), (s))
#define _fstrcat(d, s) strcat((d), (s))
#define movedata(ss, so, ds, do_, n) bakrtl_movedata((ss), (so), (ds), (do_), (n))

/* alloc.h: the heap is the host's; report it intact (_HEAPOK). */
#define heapcheck() 2

/*
 * ctype.h internals. Decompiled code indexes Borland's classification table
 * directly - ((unsigned char *)_ctype)[c + 1] & _IS_DIG - so provide it with
 * Borland's bit values (entry 0 is EOF).
 */
#define _IS_SP 1
#define _IS_DIG 2
#define _IS_UPP 4
#define _IS_LOW 8
#define _IS_HEX 16
#define _IS_CTL 32
#define _IS_PUN 64
extern unsigned char bakrtl_ctype[257];
#define _ctype bakrtl_ctype

#ifdef __cplusplus
}
#endif

#endif
