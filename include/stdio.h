#ifndef CC_STDIO_H
#define CC_STDIO_H

#include "stddef.h"
#include "sys/types.h"

typedef struct __mini_FILE FILE;

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *stream, const char *fmt, ...);
int snprintf(char *dst, size_t n, const char *fmt, ...);
int puts(const char *s);
int eprintf(const char *fmt, long a1, long a2, long a3, long a4);

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *fp);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp);
int fseek(FILE *fp, long off, int whence);
long ftell(FILE *fp);

#endif
