#ifndef CC_FCNTL_H
#define CC_FCNTL_H

#include "sys/types.h"

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#define O_APPEND 1024

int open(const char *pathname, int flags, mode_t mode);

#endif
