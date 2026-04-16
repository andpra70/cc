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

static int vprint_fd(int fd, const char *fmt, long a1, long a2, long a3, long a4) {
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

int eprintf(const char *fmt, long a1, long a2, long a3, long a4) {
  return vprint_fd(2, fmt, a1, a2, a3, a4);
}

int printf(fmt, a1, a2, a3, a4)
const char *fmt;
long a1;
long a2;
long a3;
long a4;
{
  return vprint_fd(1, fmt, a1, a2, a3, a4);
}
