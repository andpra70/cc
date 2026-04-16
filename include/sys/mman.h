#ifndef CC_SYS_MMAN_H
#define CC_SYS_MMAN_H

#include "stddef.h"
#include "sys/types.h"

#define PROT_READ 1
#define PROT_WRITE 2

#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 32

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int munmap(void *addr, size_t len);

#endif
