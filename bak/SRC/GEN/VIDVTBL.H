#ifndef BAK_VIDVTBL_H
#define BAK_VIDVTBL_H

#include "structs.h"
#include "SRC/IO/RESOURCE.H"

typedef u32(far *AllocFarFn)(u32 size, u16 unused,
                                       u16 flags);
typedef s16(far *FreememFn)(u32 ptr);
typedef s16(far *ReadFarThunkFn)(s16 fd, void *buf, u16 count);
typedef s16(far *CloseFarThunkFn)(s16 fd);
typedef void *(far *MallocFarFn)(u16 size);
typedef void *(far *CallocFarThunkFn)(u16 nitems, u16 size);
typedef void(far *FreeThunkFn)(void *ptr);
typedef char *(far *StrcatFarFn)(char *dest, char *src);
typedef char *(far *StrcpyFarFn)(char *dest, char *src);
typedef char *(far *StrchrFarFn)(char *s, s16 c);
typedef void(far *GfxDefaultFn)(s16 arg0, s16 arg1, s16 arg2);

typedef s16(far *BakFcloseFn)(ResFile *file);
typedef s16(far *BakFgetcFn)(ResFile *file);
typedef ResFile *(far *BakFopenFn)(char *filename, char *mode);
typedef s16(far *BakFreadFn)(void *ptr, s16 size, s16 count, ResFile *file);
typedef s16(far *BakFseekFn)(ResFile *file, s16 offset_lo, s16 offset_hi, s16 whence);
typedef s16(far *BakFtellFn)(ResFile *file);
typedef s16(far *BakFwriteFn)(void *ptr, s16 size, s16 count, ResFile *file);
typedef s16(far *BakPutcFn)(s16 c, ResFile *file);
typedef void(far *BakRewindFn)(ResFile *file);

typedef struct VideoDriverImports {
    AllocFarFn far *alloc_far;
    FreememFn far *_freemem;
    ReadFarThunkFn far *_read_far_thunk;
    BakFseekFn far *res_fseek;
    BakFtellFn far *res_ftell;
    BakFwriteFn far *res_fwrite;
    BakPutcFn far *res_putc;
    BakRewindFn far *res_rewind;
    CloseFarThunkFn far *_close_far_thunk;
    BakFopenFn far *res_fopen;
    BakFreadFn far *res_fread;
    BakFcloseFn far *res_fclose;
    MallocFarFn far *malloc_far;
    CallocFarThunkFn far *calloc_far_thunk;
    BakFgetcFn far *res_fgetc;
    FreeThunkFn far *free_thunk;
    StrcatFarFn far *strcat_far;
    StrcpyFarFn far *strcpy_far;
    StrchrFarFn far *strchr_far;
} VideoDriverImports;

extern GfxDefaultFn far *g_gfx_default_fptr;
extern VideoDriverImports g_video_driver_imports;

#endif
