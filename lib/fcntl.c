#include "../include/fcntl.h"

int open(const char *pathname, int flags, int mode) {
  return openat(AT_FDCWD, pathname, flags, mode);
}
