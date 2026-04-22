#ifndef CC_STDLIB_H
#define CC_STDLIB_H

#include "stddef.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void *malloc(size_t n);
void free(void *ptr);
void *calloc(size_t n, size_t sz);
void *realloc(void *ptr, size_t n);
size_t dbg_heap_live_bytes(void);
size_t dbg_heap_peak_bytes(void);
size_t dbg_stack_peak_bytes(void);
void dbg_stack_reset(void);

int atoi(const char *s);
long strtol(const char *s, char **endptr, int base);
double strtod(const char *s, char **endptr);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));

void abort(void);
void exit(int code);

#endif
