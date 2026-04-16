#ifndef CC_UNISTD_H
#define CC_UNISTD_H

#include "sys/types.h"

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
void exit(int code);

#endif
