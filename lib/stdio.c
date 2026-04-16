#include "../include/stdio.h"
#include "../include/unistd.h"
#include "../include/string.h"

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
