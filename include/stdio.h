#include "stddef.h"
#include "sys/types.h"

typedef struct __mini_FILE FILE;

#define SEEK_SET 0
#define SEEK_END 2

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

extern int printf(const char *fmt, ...);
extern int fprintf(FILE *stream, const char *fmt, ...);
extern int snprintf(char *dst, size_t n, const char *fmt, ...);
extern FILE *fopen(const char *path, const char *mode);
extern int fclose(FILE *fp);
extern size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp);
extern size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp);
extern int fseek(FILE *fp, long off, int whence);
extern long ftell(FILE *fp);
