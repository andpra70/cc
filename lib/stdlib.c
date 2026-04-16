void *malloc(size_t n) {
  size_t total;
  size_t *base;
  if (n == 0) n = 1;
  total = n + sizeof(size_t);
  if (total < 32) total = 32;
  base = (size_t *)mmap(NULL, total, PROT_READ + PROT_WRITE, MAP_PRIVATE + MAP_ANONYMOUS, -1, 0);
  if ((long)base < 0) return NULL;
  base[0] = total;
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
