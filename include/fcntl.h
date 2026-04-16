#define AT_FDCWD -100
#define O_RDONLY 0
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512

extern int openat(int dirfd, const char *pathname, int flags, int mode);
int open(const char *pathname, int flags, int mode);
