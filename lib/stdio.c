#include <stdio.h>

static int write_all_fd(int fd, const char *buf, long n) {
  long off = 0;
  while (off < n) {
    long w = (long)write(fd, buf + off, (size_t)(n - off));
    if (w <= 0) return -1;
    off += w;
  }
  return (int)off;
}

int puts(const char *s) {
  long n = (long)strlen(s);
  if (write_all_fd(1, s, n) < 0) return -1;
  if (write_all_fd(1, "\n", 1) < 0) return -1;
  return (int)(n + 1);
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

static int write_u64_buf(char *dst, size_t cap, size_t *ioff, unsigned long v) {
  char tmp[32];
  int n = 0;
  if (v == 0) tmp[n++] = '0';
  else {
    while (v && n < (int)sizeof(tmp)) {
      unsigned long q = v / 10;
      unsigned long rem = v - q * 10;
      tmp[n++] = (char)('0' + rem);
      v = q;
    }
  }
  while (n > 0) {
    n--;
    if (*ioff + 1 < cap) dst[*ioff] = tmp[n];
    (*ioff)++;
  }
  return 0;
}

static int write_i64_buf(char *dst, size_t cap, size_t *ioff, long v) {
  unsigned long uv;
  if (v < 0) {
    if (*ioff + 1 < cap) dst[*ioff] = '-';
    (*ioff)++;
    uv = (unsigned long)(-v);
  } else uv = (unsigned long)v;
  return write_u64_buf(dst, cap, ioff, uv);
}

static int write_str_buf(char *dst, size_t cap, size_t *ioff, const char *s, int precision) {
  size_t i = 0;
  if (!s) s = "(null)";
  if (precision >= 0) {
    while (s[i] && (int)i < precision) {
      if (*ioff + 1 < cap) dst[*ioff] = s[i];
      (*ioff)++;
      i++;
    }
    return 0;
  }
  while (s[i]) {
    if (*ioff + 1 < cap) dst[*ioff] = s[i];
    (*ioff)++;
    i++;
  }
  return 0;
}

static int vprint_fd4(int fd, const char *fmt, long a1, long a2, long a3, long a4) {
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

    {
      int precision = -1;
      char spec;
      long arg;
      if (*fmt == '.') {
        fmt++;
        precision = 0;
        while (isdigit((unsigned char)*fmt)) {
          precision = precision * 10 + (*fmt - '0');
          fmt++;
        }
      }
      while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') fmt++;

      spec = *fmt ? *fmt++ : 0;
      arg = 0;
      if (ai == 0) arg = a1;
      else if (ai == 1) arg = a2;
      else if (ai == 2) arg = a3;
      else if (ai == 3) arg = a4;
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

      if (write_all_fd(fd, "%", 1) < 0) return -1;
      out++;
      if (spec) {
        if (write_all_fd(fd, &spec, 1) < 0) return -1;
        out++;
      }
    }
  }
  return out;
}

typedef __builtin_va_list cc_va_list;

static int vprint_fd_va(int fd, const char *fmt, cc_va_list ap) {
  int out = 0;
  while (*fmt) {
    int precision = -1;
    char spec;
    if (*fmt != '%') {
      if (write_all_fd(fd, fmt, 1) < 0) return -1;
      out++;
      fmt++;
      continue;
    }
    fmt++;
    if (*fmt == '%') {
      if (write_all_fd(fd, "%", 1) < 0) return -1;
      out++;
      fmt++;
      continue;
    }
    if (*fmt == '.') {
      fmt++;
      precision = 0;
      while (isdigit((unsigned char)*fmt)) {
        precision = precision * 10 + (*fmt - '0');
        fmt++;
      }
    }
    while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') fmt++;
    spec = *fmt ? *fmt++ : 0;
    if (spec == 's') {
      const char *s = __builtin_va_arg(ap, const char *);
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
      long v = __builtin_va_arg(ap, long);
      int n = write_i64_fd(fd, v);
      if (n < 0) return -1;
      out += n;
      continue;
    }
    if (spec == 'u') {
      unsigned long v = __builtin_va_arg(ap, unsigned long);
      int n = write_u64_fd(fd, v);
      if (n < 0) return -1;
      out += n;
      continue;
    }
    if (spec == 'c') {
      int v = __builtin_va_arg(ap, int);
      char ch = (char)v;
      if (write_all_fd(fd, &ch, 1) < 0) return -1;
      out++;
      continue;
    }
    if (write_all_fd(fd, "%", 1) < 0) return -1;
    out++;
    if (spec) {
      if (write_all_fd(fd, &spec, 1) < 0) return -1;
      out++;
    }
  }
  return out;
}

static int vprint_buf_va(char *dst, size_t cap, const char *fmt, cc_va_list ap) {
  size_t off = 0;
  while (*fmt) {
    int precision = -1;
    char spec;
    if (*fmt != '%') {
      if (off + 1 < cap) dst[off] = *fmt;
      off++;
      fmt++;
      continue;
    }
    fmt++;
    if (*fmt == '%') {
      if (off + 1 < cap) dst[off] = '%';
      off++;
      fmt++;
      continue;
    }
    if (*fmt == '.') {
      fmt++;
      precision = 0;
      while (isdigit((unsigned char)*fmt)) {
        precision = precision * 10 + (*fmt - '0');
        fmt++;
      }
    }
    while (*fmt == 'l' || *fmt == 'z' || *fmt == 'h') fmt++;
    spec = *fmt ? *fmt++ : 0;
    if (spec == 's') {
      const char *s = __builtin_va_arg(ap, const char *);
      write_str_buf(dst, cap, &off, s, precision);
      continue;
    }
    if (spec == 'd' || spec == 'i') {
      long v = __builtin_va_arg(ap, long);
      write_i64_buf(dst, cap, &off, v);
      continue;
    }
    if (spec == 'u') {
      unsigned long v = __builtin_va_arg(ap, unsigned long);
      write_u64_buf(dst, cap, &off, v);
      continue;
    }
    if (spec == 'c') {
      int v = __builtin_va_arg(ap, int);
      if (off + 1 < cap) dst[off] = (char)v;
      off++;
      continue;
    }
    if (off + 1 < cap) dst[off] = '%';
    off++;
    if (spec) {
      if (off + 1 < cap) dst[off] = spec;
      off++;
    }
  }
  if (cap > 0) {
    size_t at = off < (cap - 1) ? off : (cap - 1);
    dst[at] = 0;
  }
  return (int)off;
}

int printf(const char *fmt, ...) {
  cc_va_list ap;
  int n;
  __builtin_va_start(ap, fmt);
  n = vprint_fd_va(1, fmt, ap);
  __builtin_va_end(ap);
  return n;
}

int fprintf(FILE *stream, const char *fmt, ...) {
  cc_va_list ap;
  int n;
  int fd = 1;
  if (stream == stderr) fd = 2;
  __builtin_va_start(ap, fmt);
  n = vprint_fd_va(fd, fmt, ap);
  __builtin_va_end(ap);
  return n;
}

int snprintf(char *dst, size_t n, const char *fmt, ...) {
  cc_va_list ap;
  int out;
  if (!dst || n == 0) return 0;
  __builtin_va_start(ap, fmt);
  out = vprint_buf_va(dst, n, fmt, ap);
  __builtin_va_end(ap);
  return out;
}

int eprintf(const char *fmt, long a1, long a2, long a3, long a4) {
  return vprint_fd4(2, fmt, a1, a2, a3, a4);
}

/* Kept disabled for self-host parser stability.
 * Old K&R-style definitions are not handled robustly yet by the frontend.
 */
