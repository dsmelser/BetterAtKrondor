/*
 * The Borland C++ 3.1 runtime functions the game uses that the host libc lacks or
 * implements differently (see bak_rtl.h).
 *
 * DOS paths: the game names files relative to its own directory, with backslashes,
 * in any case, sometimes behind a drive letter (the CD drive, the configured temp
 * drive). Reads look in the user directory first (saves, TEMP.GAM), then in the
 * game-data directory; writes always go to the user directory.
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#endif

#include "bak_rtl.h"
#include "../platform/platform.h"

/* ---- path resolution ---------------------------------------------------------- */

#define PATH_MAX_LEN 1024

/* The DOS path with its drive letter removed, separators turned to '/', leading
 * separators dropped: a path relative to the game directory. */
static void dos_relative(const char *dosPath, char *out, size_t outSize) {
    const char *p = dosPath;
    if (isalpha((unsigned char)p[0]) && p[1] == ':')
        p += 2;
    while (*p == '\\' || *p == '/')
        p++;
    size_t n = 0;
    for (; *p && n + 1 < outSize; p++)
        out[n++] = (*p == '\\') ? '/' : *p;
    out[n] = '\0';
}

#ifdef _WIN32
static int path_exists(const char *path) {
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

/* NTFS is case-insensitive: the joined path is the answer. */
static int resolve_in(const char *dir, const char *rel, char *out, size_t outSize) {
    snprintf(out, outSize, "%s/%s", dir, rel);
    return path_exists(out);
}
#else
/* Walk the relative path component by component, matching each case-insensitively. */
static int resolve_in(const char *dir, const char *rel, char *out, size_t outSize) {
    char cur[PATH_MAX_LEN];
    snprintf(cur, sizeof cur, "%s", dir);
    const char *p = rel;
    int found = 1;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        char part[256];
        if (len >= sizeof part)
            len = sizeof part - 1;
        memcpy(part, p, len);
        part[len] = '\0';
        const char *name = part;
        DIR *d = found ? opendir(cur) : NULL;
        if (d) {
            struct dirent *e;
            found = 0;
            while ((e = readdir(d)) != NULL) {
                if (strcasecmp(e->d_name, part) == 0) {
                    snprintf(part, sizeof part, "%s", e->d_name);
                    found = 1;
                    break;
                }
            }
            closedir(d);
        } else {
            found = 0;
        }
        size_t used = strlen(cur);
        snprintf(cur + used, sizeof cur - used, "/%s", name);
        p += len;
        while (*p == '/')
            p++;
    }
    snprintf(out, outSize, "%s", cur);
    return found;
}
#endif

/* Host path for reading: user directory if the file exists there, else game data. */
static void resolve_read(const char *dosPath, char *out, size_t outSize) {
    char rel[PATH_MAX_LEN];
    dos_relative(dosPath, rel, sizeof rel);
    if (resolve_in(platform_user_dir(), rel, out, outSize))
        return;
    resolve_in(platform_data_dir(), rel, out, outSize);
}

/* Host path for writing or creating: always the user directory. */
static void resolve_write(const char *dosPath, char *out, size_t outSize) {
    char rel[PATH_MAX_LEN];
    dos_relative(dosPath, rel, sizeof rel);
    resolve_in(platform_user_dir(), rel, out, outSize);
}

/* ---- stdio --------------------------------------------------------------------------- */

#undef fopen
#undef remove

FILE *bakrtl_fopen(const char *path, const char *mode) {
    char host[PATH_MAX_LEN];
    int writes = strpbrk(mode, "wa+") != NULL;
    if (writes)
        resolve_write(path, host, sizeof host);
    else
        resolve_read(path, host, sizeof host);
    /* Borland's default stream mode is text; the game's files are all binary data. */
    char m[8];
    snprintf(m, sizeof m, "%s%s", mode, strchr(mode, 'b') ? "" : "b");
    return fopen(host, m);
}

int bakrtl_remove(const char *path) {
    char host[PATH_MAX_LEN];
    resolve_write(path, host, sizeof host);
    return remove(host);
}

/* ---- io.h ------------------------------------------------------------------------------ */

int bakrtl_open(const char *path, int access, ...) {
    char host[PATH_MAX_LEN];
    int writes = access & (BAKRTL_O_WRONLY | BAKRTL_O_RDWR | BAKRTL_O_CREAT | BAKRTL_O_TRUNC);
    if (writes)
        resolve_write(path, host, sizeof host);
    else
        resolve_read(path, host, sizeof host);
    int flags = 0;
    if (access & BAKRTL_O_RDWR)
        flags |= O_RDWR;
    else if (access & BAKRTL_O_WRONLY)
        flags |= O_WRONLY;
    else
        flags |= O_RDONLY;
    if (access & BAKRTL_O_CREAT)
        flags |= O_CREAT;
    if (access & BAKRTL_O_TRUNC)
        flags |= O_TRUNC;
    if (access & BAKRTL_O_EXCL)
        flags |= O_EXCL;
    if (access & BAKRTL_O_APPEND)
        flags |= O_APPEND;
#ifdef _WIN32
    flags |= (access & BAKRTL_O_TEXT) ? O_TEXT : O_BINARY;
    return _open(host, flags, _S_IREAD | _S_IWRITE);
#else
    return open(host, flags, 0644);
#endif
}

int bakrtl_close(int fd) {
#ifdef _WIN32
    return _close(fd);
#else
    return close(fd);
#endif
}

int bakrtl_read(int fd, void *buf, unsigned len) {
#ifdef _WIN32
    return _read(fd, buf, len);
#else
    return (int)read(fd, buf, len);
#endif
}

int bakrtl_write(int fd, const void *buf, unsigned len) {
#ifdef _WIN32
    return _write(fd, buf, len);
#else
    return (int)write(fd, buf, len);
#endif
}

/* ---- dir.h -------------------------------------------------------------------------------- */

#define FA_DIREC 0x10
#define FA_ARCH 0x20

typedef struct FindState {
    int used;
    char dir[PATH_MAX_LEN];
    char pattern[16];
    int attrib;
    char **names;
    int count;
    int pos;
} FindState;

static FindState s_finds[8];

/* DOS 8.3 wildcard match: '?' matches one character, '*' the rest of the name or extension. */
static int dos_match_part(const char *pat, const char *s) {
    for (; *pat; pat++) {
        if (*pat == '*')
            return 1;
        if (*pat == '?') {
            if (*s)
                s++;
            continue;
        }
        if (toupper((unsigned char)*pat) != toupper((unsigned char)*s))
            return 0;
        s++;
    }
    return *s == '\0';
}

static int dos_match(const char *pattern, const char *name) {
    char pb[16], pe[8], nb[16], ne[8];
    const char *pdot = strrchr(pattern, '.'), *ndot = strrchr(name, '.');
    snprintf(pb, sizeof pb, "%.*s", (int)(pdot ? pdot - pattern : (long)strlen(pattern)), pattern);
    snprintf(pe, sizeof pe, "%s", pdot ? pdot + 1 : "");
    snprintf(nb, sizeof nb, "%.*s", (int)(ndot ? ndot - name : (long)strlen(name)), name);
    snprintf(ne, sizeof ne, "%s", ndot ? ndot + 1 : "");
    if (strcmp(pattern, "*.*") == 0)
        return 1;
    return dos_match_part(pb, nb) && dos_match_part(pe, ne);
}

static void free_find(FindState *f) {
    for (int i = 0; i < f->count; i++)
        free(f->names[i]);
    free(f->names);
    memset(f, 0, sizeof *f);
}

static int fill_ffblk(FindState *f, struct ffblk *ff) {
    while (f->pos < f->count) {
        const char *name = f->names[f->pos++];
        char full[PATH_MAX_LEN * 2];
        snprintf(full, sizeof full, "%s/%s", f->dir, name);
        struct stat st;
        if (stat(full, &st) != 0)
            continue;
        int isDir = (st.st_mode & S_IFMT) == S_IFDIR;
        if (isDir && !(f->attrib & FA_DIREC))
            continue;
        memset(ff->ff_name, 0, sizeof ff->ff_name);
        for (int i = 0; name[i] && i < 12; i++)
            ff->ff_name[i] = (char)toupper((unsigned char)name[i]);
        ff->ff_attrib = (char)(isDir ? FA_DIREC : FA_ARCH);
        ff->ff_fsize = (int32_t)st.st_size;
        ff->ff_ftime = 0;
        ff->ff_fdate = 0;
        return 0;
    }
    return -1;
}

static void add_name(FindState *f, const char *name) {
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return;
    if (strlen(name) > 12 || !dos_match(f->pattern, name))
        return;
    f->names = realloc(f->names, sizeof *f->names * (size_t)(f->count + 1));
    f->names[f->count++] = strdup(name);
}

int bakrtl_findfirst(const char *pattern, struct ffblk *ff, int attrib) {
    char rel[PATH_MAX_LEN], dirRel[PATH_MAX_LEN];
    dos_relative(pattern, rel, sizeof rel);
    const char *slash = strrchr(rel, '/');
    const char *pat = slash ? slash + 1 : rel;
    snprintf(dirRel, sizeof dirRel, "%.*s", (int)(slash ? slash - rel : 0), rel);

    int slot = -1;
    for (int i = 0; i < (int)(sizeof s_finds / sizeof s_finds[0]); i++) {
        if (!s_finds[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        errno = EMFILE;
        return -1;
    }
    FindState *f = &s_finds[slot];
    f->used = 1;
    f->attrib = attrib;
    snprintf(f->pattern, sizeof f->pattern, "%s", pat);
    resolve_read(dirRel[0] ? dirRel : ".", f->dir, sizeof f->dir);

#ifdef _WIN32
    char glob[PATH_MAX_LEN * 2];
    snprintf(glob, sizeof glob, "%s/*", f->dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do
            add_name(f, fd.cFileName);
        while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(f->dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL)
            add_name(f, e->d_name);
        closedir(d);
    }
#endif
    memset(ff->ff_reserved, 0, sizeof ff->ff_reserved);
    ff->ff_reserved[0] = (char)(slot + 1);
    if (fill_ffblk(f, ff) != 0) {
        free_find(f);
        ff->ff_reserved[0] = 0;
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int bakrtl_findnext(struct ffblk *ff) {
    int slot = ff->ff_reserved[0] - 1;
    if (slot < 0 || slot >= (int)(sizeof s_finds / sizeof s_finds[0]) || !s_finds[slot].used)
        return -1;
    FindState *f = &s_finds[slot];
    if (fill_ffblk(f, ff) != 0) {
        free_find(f);
        ff->ff_reserved[0] = 0;
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int bakrtl_mkdir(const char *path) {
    char host[PATH_MAX_LEN];
    resolve_write(path, host, sizeof host);
#ifdef _WIN32
    return _mkdir(host);
#else
    return mkdir(host, 0755);
#endif
}

int bakrtl_rmdir(const char *path) {
    char host[PATH_MAX_LEN];
    resolve_write(path, host, sizeof host);
#ifdef _WIN32
    return _rmdir(host);
#else
    return rmdir(host);
#endif
}

int bakrtl_chdir(const char *path) {
    /* The game only changes into its own directory tree; paths stay relative to it. */
    (void)path;
    return 0;
}

/* ---- dos.h ------------------------------------------------------------------------------- */

void bakrtl_geninterrupt(int intno) {
    char what[48];
    snprintf(what, sizeof what, "software interrupt %02Xh", intno);
    bak_unimplemented(what, __FILE__, __LINE__);
}

static BakIsrFn s_vectors[256];

BakIsrFn bakrtl_getvect(int intno) {
    return s_vectors[intno & 0xFF];
}

void bakrtl_setvect(int intno, BakIsrFn isr) {
    /* Hooked vectors are recorded but never raised: the platform layer owns the
     * timer, keyboard and critical-error paths natively. */
    s_vectors[intno & 0xFF] = isr;
}

/* ---- ctype.h ------------------------------------------------------------------------------- */

/* Borland's classification table, indexed by c + 1 (entry 0 is EOF); bits as in bak_rtl.h. */
unsigned char bakrtl_ctype[257] = {
     0, 32, 32, 32, 32, 32, 32, 32, 32, 32, 33, 33, 33, 33, 33, 32,
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
    32,  1, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 64, 64, 64, 64, 64,
    64, 64, 20, 20, 20, 20, 20, 20,  4,  4,  4,  4,  4,  4,  4,  4,
     4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4, 64, 64, 64, 64,
    64, 64, 24, 24, 24, 24, 24, 24,  8,  8,  8,  8,  8,  8,  8,  8,
     8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8, 64, 64, 64, 64,
    32,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
     0,
};

/* ---- stdlib.h / string.h / mem.h ----------------------------------------------------------- */

static char *utoa_radix(uint32_t v, char *buf, int radix) {
    char tmp[34];
    int n = 0;
    do {
        int d = (int)(v % (uint32_t)radix);
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= (uint32_t)radix;
    } while (v);
    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return buf;
}

char *bakrtl_itoa(int value, char *buf, int radix) {
    /* Borland's int is 16 bits: only base 10 is signed, other bases print the u16. */
    int16_t v = (int16_t)value;
    if (radix == 10 && v < 0) {
        buf[0] = '-';
        utoa_radix((uint32_t)(-(int32_t)v), buf + 1, 10);
        return buf;
    }
    return utoa_radix(radix == 10 ? (uint32_t)v : (uint16_t)v, buf, radix);
}

char *bakrtl_ltoa(int32_t value, char *buf, int radix) {
    if (radix == 10 && value < 0) {
        buf[0] = '-';
        utoa_radix((uint32_t)0 - (uint32_t)value, buf + 1, 10);
        return buf;
    }
    return utoa_radix((uint32_t)value, buf, radix);
}

int bakrtl_stricmp(const char *a, const char *b) {
    for (;; a++, b++) {
        int ca = toupper((unsigned char)*a), cb = toupper((unsigned char)*b);
        if (ca != cb || ca == 0)
            return ca - cb;
    }
}

int bakrtl_strnicmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        int ca = toupper((unsigned char)*a), cb = toupper((unsigned char)*b);
        if (ca != cb || ca == 0)
            return ca - cb;
    }
    return 0;
}

uint32_t bakrtl_lrotl(uint32_t value, int count) {
    count &= 31;
    return count ? (value << count) | (value >> (32 - count)) : value;
}

void bakrtl_movedata(unsigned srcseg, unsigned srcoff, unsigned dstseg, unsigned dstoff, size_t n) {
    memmove(MK_FP(dstseg, dstoff), MK_FP(srcseg, srcoff), n);
}
