#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "structs.h"
#include "SRC/SYS/FARTHUNK.H"

s16 my_open2(char *filename, s16 oflag) {
    return open(filename, oflag);
}

s16 my_open3(char *filename, s16 oflag, s16 pmode) {
    return open(filename, oflag, pmode);
}

s16 my_close(s16 fd) {
    return close(fd);
}

s16 my_read(s16 fd, void *buf, u16 count) {
    return read(fd, buf, count);
}

FILE *my_fopen(char *filename, char *mode) {
    return fopen(filename, mode);
}

s16 my_fseek(FILE *stream, s32 offset, s16 whence) {
    return fseek(stream, offset, whence);
}

s32 my_ftell(FILE *stream) {
    return ftell(stream);
}

s16 my_fread(void *ptr, s16 size, s16 count, FILE *stream) {
    return fread(ptr, size, count, stream);
}

s16 my_fwrite(void *ptr, s16 size, s16 count, FILE *stream) {
    return fwrite(ptr, size, count, stream);
}

s16 my_fputc(s16 c, FILE *stream) {
    return fputc(c, stream);
}

void my_rewind(FILE *stream) {
    rewind(stream);
}

s16 fclose_far(FILE *stream) {
    return fclose(stream);
}

void *my_malloc(u16 size) {
    return calloc(1, size);
}

void my_free(void *ptr) {
    free(ptr);
}

char *my_strcat(char *dest, char *src) {
    return strcat(dest, src);
}

char *my_strcpy(char *dest, char *src) {
    return strcpy(dest, src);
}

char *my_strchr(char *s, s16 c) {
    return strchr(s, c);
}

void *my_calloc(u16 nitems, u16 size) {
    return calloc(nitems, size);
}

void my_fgetc(FILE *f) {
    fgetc(f);
}
