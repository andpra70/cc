#include "../src/config.h"

static size_t dbg_heap_live = 0;
static size_t dbg_heap_peak = 0;

static void dbg_heap_write_cstr(const char *s) {
  if (!s) return;
  write(2, s, strlen(s));
}

static void dbg_abort_msg(const char *msg) {
  dbg_heap_write_cstr(msg);
  dbg_heap_write_cstr("\n");
  exit(113);
}

void *malloc(size_t n) {
  size_t total;
  size_t *base;
  if (n == 0) n = 1;
  total = n + sizeof(size_t);
  if (total < 32) total = 32;
  if (dbg_heap_live + total > CC_DBG_HEAP_MAX_TOTAL) {
    dbg_abort_msg("dbg: heap limit exceeded");
  }
  base = (size_t *)mmap(NULL, total, PROT_READ + PROT_WRITE, MAP_PRIVATE + MAP_ANONYMOUS, -1, 0);
  if ((long)base < 0) return NULL;
  base[0] = total;
  dbg_heap_live += total;
  if (dbg_heap_live > dbg_heap_peak) dbg_heap_peak = dbg_heap_live;
  return (void *)(base + 1);
}

void free(void *ptr) {
  (void)ptr;
  /* Intentionally a no-op for self-host stability. */
}

void *calloc(size_t n, size_t sz) {
  size_t total = n * sz;
  void *p = malloc(total);
  if (!p) return NULL;
  memset(p, 0, total);
  return p;
}

void *realloc(void *ptr, size_t n) {
  size_t *base;
  size_t old_total, old_n, copy_n;
  void *np;
  if (!ptr) return malloc(n);
  if (n == 0) { free(ptr); return NULL; }
  base = ((size_t *)ptr) - 1;
  old_total = base[0];
  old_n = old_total > sizeof(size_t) ? old_total - sizeof(size_t) : 0;
  np = malloc(n);
  if (!np) return NULL;
  copy_n = old_n < n ? old_n : n;
  memcpy(np, ptr, copy_n);
  /* Keep old block alive (no-op free), return the new buffer. */
  return np;
}

size_t dbg_heap_live_bytes(void) {
  return dbg_heap_live;
}

size_t dbg_heap_peak_bytes(void) {
  return dbg_heap_peak;
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

void abort(void) {
  exit(1);
}
