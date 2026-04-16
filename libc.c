/*
 * Minimal libc subset for self-hosted compiler runtime.
 * Implemented in plain C so the compiler can compile itself.
 */

#ifndef MINIC_LIBC_INCLUDED
#define MINIC_LIBC_INCLUDED

typedef unsigned long size_t;
typedef long ssize_t;
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

#define NULL ((void *)0)
#define INT32_MIN (-2147483647 - 1)
#define INT32_MAX 2147483647

/* Linux x86_64 constants */
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 32
#define AT_FDCWD -100
#define O_RDONLY 0

/* Syscall-backed functions (emitted as builtins by codegen). */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int openat(int dirfd, const char *pathname, int flags, int mode);
int close(int fd);
void *mmap(void *addr, size_t len, int prot, int flags, int fd, size_t off);
int munmap(void *addr, size_t len);
void exit(int code);
int printf(const char *fmt, ...);
void *malloc(size_t n);
void free(void *ptr);
void *calloc(size_t n, size_t sz);
void *realloc(void *ptr, size_t n);

size_t strlen(const char *s) {
  size_t n = 0;
  while (s && s[n]) n++;
  return n;
}

void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  size_t i = 0;
  while (i < n) { d[i] = s[i]; i++; }
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  size_t i = 0;
  while (i < n) { d[i] = (unsigned char)c; i++; }
  return dst;
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a - *b;
}

char *strchr(const char *s, int c) {
  unsigned char ch = (unsigned char)c;
  while (*s) {
    if ((unsigned char)*s == ch) return (char *)s;
    s++;
  }
  if (ch == 0) return (char *)s;
  return NULL;
}

char *strrchr(const char *s, int c) {
  const char *last = NULL;
  unsigned char ch = (unsigned char)c;
  while (*s) {
    if ((unsigned char)*s == ch) last = s;
    s++;
  }
  if (ch == 0) return (char *)s;
  return (char *)last;
}

int isspace(int c) {
  return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }

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

static int write_all_fd(int fd, const char *buf, long n) {
  long off = 0;
  while (off < n) {
    long w = (long)write(fd, buf + off, (size_t)(n - off));
    if (w <= 0) return -1;
    off += w;
  }
  return (int)off;
}

static int write_u64_rec(int fd, unsigned long v) {
  int n = 0;
  char ch;
  unsigned long q;
  unsigned long rem;
  if (v >= 10) {
    n = write_u64_rec(fd, v / 10);
    if (n < 0) return -1;
  }
  q = v / 10;
  rem = v - q * 10;
  ch = (char)('0' + rem);
  if (write_all_fd(fd, &ch, 1) < 0) return -1;
  return n + 1;
}

static int write_u64_fd(int fd, unsigned long v) {
  if (v == 0) return write_all_fd(fd, "0", 1);
  return write_u64_rec(fd, v);
}

static int write_i64_fd(int fd, long v) {
  int n = 0;
  unsigned long uv;
  if (v < 0) {
    if (write_all_fd(fd, "-", 1) < 0) return -1;
    n++;
    uv = (unsigned long)(-v);
  } else uv = (unsigned long)v;
  n += write_u64_fd(fd, uv);
  return n;
}

static int vprint_fd(int fd, const char *fmt, long a1, long a2, long a3, long a4, long a5, long a6) {
  int ai = 0;
  int out = 0;

  while (*fmt) {
    if (*fmt != '%') {
      if (write_all_fd(fd, fmt, 1) < 0) return -1;
      fmt++;
      out++;
      continue;
    }
    fmt++;
    if (*fmt == '%') {
      if (write_all_fd(fd, "%", 1) < 0) return -1;
      fmt++;
      out++;
      continue;
    }

    int precision = -1;
    if (*fmt == '.') {
      fmt++;
      precision = 0;
      while (isdigit((unsigned char)*fmt)) {
        precision = precision * 10 + (*fmt - '0');
        fmt++;
      }
    }
    while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') fmt++;

    char spec = *fmt ? *fmt++ : 0;
    long arg = 0;
    if (ai == 0) arg = a1;
    else if (ai == 1) arg = a2;
    else if (ai == 2) arg = a3;
    else if (ai == 3) arg = a4;
    else if (ai == 4) arg = a5;
    else if (ai == 5) arg = a6;
    ai++;
    if (spec == 's') {
      const char *s = (const char *)arg;
      long n = 0;
      if (!s) s = "(null)";
      if (precision >= 0) {
        while (s[n] && n < precision) n++;
      } else n = (long)strlen(s);
      if (write_all_fd(fd, s, n) < 0) return -1;
      out += (int)n;
      continue;
    }
    if (spec == 'd' || spec == 'i') {
      int n = write_i64_fd(fd, arg);
      if (n < 0) return -1;
      out += n;
      continue;
    }
    if (spec == 'u') {
      int n = write_u64_fd(fd, (unsigned long)arg);
      if (n < 0) return -1;
      out += n;
      continue;
    }
    if (spec == 'c') {
      char ch = (char)arg;
      if (write_all_fd(fd, &ch, 1) < 0) return -1;
      out++;
      continue;
    }

    /* Unknown format: print raw fallback. */
    if (write_all_fd(fd, "%", 1) < 0) return -1;
    out++;
    if (spec) {
      if (write_all_fd(fd, &spec, 1) < 0) return -1;
      out++;
    }
  }
  return out;
}

int eprintf(const char *fmt, long a1, long a2, long a3, long a4, long a5, long a6) {
  return vprint_fd(2, fmt, a1, a2, a3, a4, a5, a6);
}

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
  size_t *base;
  if (!ptr) return;
  base = ((size_t *)ptr) - 1;
  munmap((void *)base, base[0]);
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
  free(ptr);
  return np;
}

#endif
