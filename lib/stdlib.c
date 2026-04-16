#include "../include/stdlib.h"
#include "../include/ctype.h"
#include "../include/string.h"
#include "../include/stdint.h"
#include "../include/fcntl.h"
#include "../include/unistd.h"
#include "../include/sys/mman.h"

#define HEAP_SIZE (1024 * 1024 * 64)

typedef struct Block {
  size_t sz;
  int free;
  struct Block *next;
} Block;

static Block *heap_list;
static void *heap_base;
static size_t heap_used;

static int heap_init(void) {
  if (heap_base) return 1;
  heap_base = mmap((void *)0, HEAP_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if ((long)heap_base <= 0) return 0;
  heap_used = 0;
  heap_list = (Block *)0;
  return 1;
}

static void *heap_alloc(size_t n) {
  void *p;
  if (!heap_init()) return (void *)0;
  if (heap_used + n > HEAP_SIZE) return (void *)0;
  p = (void *)((unsigned char *)heap_base + heap_used);
  heap_used += n;
  return p;
}

void *malloc(size_t n) {
  Block *b;
  size_t need;
  if (!n) n = 1;
  b = heap_list;
  while (b) {
    if (b->free && b->sz >= n) {
      b->free = 0;
      return (void *)(b + 1);
    }
    b = b->next;
  }
  need = sizeof(Block) + n;
  b = (Block *)heap_alloc(need);
  if (!b) return (void *)0;
  b->sz = n;
  b->free = 0;
  b->next = heap_list;
  heap_list = b;
  return (void *)(b + 1);
}

void free(void *ptr) {
  Block *b;
  if (!ptr) return;
  b = ((Block *)ptr) - 1;
  b->free = 1;
}

void *calloc(size_t n, size_t sz) {
  size_t total = n * sz;
  void *p = malloc(total);
  if (!p) return p;
  memset(p, 0, total);
  return p;
}

void *realloc(void *ptr, size_t n) {
  void *np;
  Block *b;
  if (!ptr) return malloc(n);
  if (!n) {
    free(ptr);
    return (void *)0;
  }
  b = ((Block *)ptr) - 1;
  if (b->sz >= n) return ptr;
  np = malloc(n);
  if (!np) return (void *)0;
  memcpy(np, ptr, b->sz);
  free(ptr);
  return np;
}

int atoi(const char *s) {
  long sign = 1;
  long v = 0;
  while (isspace((unsigned char)*s)) s++;
  if (*s == '-') { sign = -1; s++; }
  else if (*s == '+') s++;
  while (isdigit((unsigned char)*s)) {
    v = v * 10 + (*s - '0');
    s++;
  }
  return (int)(sign * v);
}

long strtol(const char *s, char **endptr, int base) {
  long sign = 1;
  long v = 0;
  int d;
  if (base != 10) {
    if (endptr) *endptr = (char *)s;
    return 0;
  }
  while (isspace((unsigned char)*s)) s++;
  if (*s == '-') { sign = -1; s++; }
  else if (*s == '+') s++;
  while (*s) {
    d = *s - '0';
    if (d < 0 || d > 9) break;
    v = v * 10 + d;
    s++;
  }
  if (endptr) *endptr = (char *)s;
  return sign * v;
}

double strtod(const char *s, char **endptr) {
  long sign = 1;
  long ip = 0;
  const char *p = s;
  if (*p == '-') { sign = -1; p++; }
  else if (*p == '+') p++;
  while (isdigit((unsigned char)*p)) {
    ip = ip * 10 + (*p - '0');
    p++;
  }
  if (*p == '.') {
    p++;
    while (isdigit((unsigned char)*p)) p++;
  }
  if (endptr) *endptr = (char *)p;
  return (double)(sign * ip);
}

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
  size_t i, j, k;
  unsigned char *a = (unsigned char *)base;
  unsigned char *tmp;
  if (!base || !cmp || size == 0 || n < 2) return;
  tmp = (unsigned char *)malloc(size);
  if (!tmp) return;
  for (i = 1; i < n; i++) {
    memcpy(tmp, a + i * size, size);
    j = i;
    while (j > 0 && cmp(a + (j - 1) * size, tmp) > 0) {
      for (k = 0; k < size; k++) a[j * size + k] = a[(j - 1) * size + k];
      j--;
    }
    for (k = 0; k < size; k++) a[j * size + k] = tmp[k];
  }
  free(tmp);
}
