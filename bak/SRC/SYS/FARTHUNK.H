#ifndef BAK_FARTHUNK_H
#define BAK_FARTHUNK_H

#include "structs.h"

extern s16 my_open2(char *filename, s16 oflag);
extern s16 my_open3(char *filename, s16 oflag, s16 pmode);
extern s16 my_close(s16 fd);
extern s16 my_read(s16 fd, void *buf, u16 count);
extern FILE *my_fopen(char *filename, char *mode);
extern s16 my_fseek(FILE *stream, s32 offset, s16 whence);
extern s32 my_ftell(FILE *stream);
extern s16 my_fread(void *ptr, s16 size, s16 count, FILE *stream);
extern s16 my_fwrite(void *ptr, s16 size, s16 count, FILE *stream);
extern s16 my_fputc(s16 c, FILE *stream);
extern void my_rewind(FILE *stream);
extern s16 fclose_far(FILE *stream);
extern void *my_malloc(u16 size);
extern void my_free(void *ptr);
extern char *my_strcat(char *dest, char *src);
extern char *my_strcpy(char *dest, char *src);
extern char *my_strchr(char *s, s16 c);
extern void *my_calloc(u16 nitems, u16 size);
extern void my_fgetc(FILE *f);

#endif
