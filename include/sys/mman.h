#include "stddef.h"

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 32

extern void *mmap(void *addr, size_t len, int prot, int flags, int fd, size_t off);
extern int munmap(void *addr, size_t len);
