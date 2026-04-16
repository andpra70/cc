#include "stddef.h"

void *malloc(size_t n);
void free(void *ptr);
void *calloc(size_t n, size_t sz);
void *realloc(void *ptr, size_t n);
int atoi(const char *s);
long strtol(const char *s, char **endptr, int base);
double strtod(const char *s, char **endptr);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
extern void exit(int code);
