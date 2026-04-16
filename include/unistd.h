#include "sys/types.h"

extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int close(int fd);
extern long lseek(int fd, long offset, int whence);
extern void exit(int code);
