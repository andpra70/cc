/*
 * Shared host ABI bridge.
 * Builtin calls are dispatched to host libc/kernel symbols at runtime.
 */

#include "../include/kernel_abi.h"
#include <dlfcn.h>

extern ssize_t read(int fd, void *buf, size_t count);
extern ssize_t write(int fd, const void *buf, size_t count);
extern int open(const char *pathname, int flags, ...);
extern int close(int fd);
extern void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
extern int munmap(void *addr, size_t len);
extern off_t lseek(int fd, off_t offset, int whence);
extern void *malloc(size_t n);
extern void free(void *p);
extern void *calloc(size_t n, size_t sz);
extern void *realloc(void *p, size_t n);
extern size_t strlen(const char *s);
extern int strcmp(const char *a, const char *b);
extern void *memcpy(void *dst, const void *src, size_t n);
extern void *memset(void *dst, int c, size_t n);
extern char *strchr(const char *s, int c);
extern char *strrchr(const char *s, int c);
extern double strtod(const char *s, char **endptr);
extern void exit(int code);
extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
extern char *dlerror(void);

static void *abi_dlopen_handle = 0;

static long abi_call_ptr(void *fp, long *args, int argc) {
  long (*f0)(void);
  long (*f1)(long);
  long (*f2)(long, long);
  long (*f3)(long, long, long);
  long (*f4)(long, long, long, long);
  long (*f5)(long, long, long, long, long);
  long (*f6)(long, long, long, long, long, long);
  if (!fp) return (long)KERNEL_ABI_UNKNOWN;
  if (argc <= 0) { f0 = fp; return f0(); }
  if (argc == 1) { f1 = fp; return f1(args[0]); }
  if (argc == 2) { f2 = fp; return f2(args[0], args[1]); }
  if (argc == 3) { f3 = fp; return f3(args[0], args[1], args[2]); }
  if (argc == 4) { f4 = fp; return f4(args[0], args[1], args[2], args[3]); }
  if (argc == 5) { f5 = fp; return f5(args[0], args[1], args[2], args[3], args[4]); }
  if (argc == 6) { f6 = fp; return f6(args[0], args[1], args[2], args[3], args[4], args[5]); }
  return (long)KERNEL_ABI_UNKNOWN;
}

static void *abi_default_handle() {
  if (abi_dlopen_handle) return abi_dlopen_handle;
  abi_dlopen_handle = dlopen((const char *)0, RTLD_LAZY + RTLD_LOCAL);
  if (!abi_dlopen_handle) abi_dlopen_handle = dlopen("libc.so.6", RTLD_LAZY + RTLD_LOCAL);
  return abi_dlopen_handle;
}

static int abi_name_eq(const char *name, const char *base) {
  if (!name || !base) return 0;
  if (!strcmp(name, base)) return 1;
  if (!strcmp(name, "__kernel_abi_read") && !strcmp(base, "read")) return 1;
  if (!strcmp(name, "__kernel_abi_write") && !strcmp(base, "write")) return 1;
  if (!strcmp(name, "__kernel_abi_open") && !strcmp(base, "open")) return 1;
  if (!strcmp(name, "__kernel_abi_close") && !strcmp(base, "close")) return 1;
  if (!strcmp(name, "__kernel_abi_mmap") && !strcmp(base, "mmap")) return 1;
  if (!strcmp(name, "__kernel_abi_munmap") && !strcmp(base, "munmap")) return 1;
  if (!strcmp(name, "__kernel_abi_exit") && !strcmp(base, "exit")) return 1;
  if (!strcmp(name, "__kernel_abi_dlopen") && !strcmp(base, "dlopen")) return 1;
  if (!strcmp(name, "__kernel_abi_dlsym") && !strcmp(base, "dlsym")) return 1;
  if (!strcmp(name, "__kernel_abi_dlclose") && !strcmp(base, "dlclose")) return 1;
  if (!strcmp(name, "__kernel_abi_dlerror") && !strcmp(base, "dlerror")) return 1;
  return 0;
}

int kernel_abi_is_builtin(const char *name) {
  if (abi_name_eq(name, "read")) return 1;
  if (abi_name_eq(name, "write")) return 1;
  if (abi_name_eq(name, "open")) return 1;
  if (abi_name_eq(name, "close")) return 1;
  if (abi_name_eq(name, "mmap")) return 1;
  if (abi_name_eq(name, "munmap")) return 1;
  if (abi_name_eq(name, "malloc")) return 1;
  if (abi_name_eq(name, "free")) return 1;
  if (abi_name_eq(name, "calloc")) return 1;
  if (abi_name_eq(name, "realloc")) return 1;
  if (abi_name_eq(name, "strlen")) return 1;
  if (abi_name_eq(name, "strcmp")) return 1;
  if (abi_name_eq(name, "memcpy")) return 1;
  if (abi_name_eq(name, "memset")) return 1;
  if (abi_name_eq(name, "strchr")) return 1;
  if (abi_name_eq(name, "strrchr")) return 1;
  if (abi_name_eq(name, "isspace")) return 1;
  if (abi_name_eq(name, "isdigit")) return 1;
  if (abi_name_eq(name, "isalpha")) return 1;
  if (abi_name_eq(name, "isalnum")) return 1;
  if (abi_name_eq(name, "strtod")) return 1;
  if (abi_name_eq(name, "printf")) return 1;
  if (abi_name_eq(name, "eprintf")) return 1;
  if (abi_name_eq(name, "exit")) return 1;
  if (abi_name_eq(name, "dlopen")) return 1;
  if (abi_name_eq(name, "dlsym")) return 1;
  if (abi_name_eq(name, "dlclose")) return 1;
  if (abi_name_eq(name, "dlerror")) return 1;
  return 0;
}

const char *kernel_abi_symbol(const char *name) {
  if (!name) return name;
  if (abi_name_eq(name, "read")) return "__kernel_abi_read";
  if (abi_name_eq(name, "write")) return "__kernel_abi_write";
  if (abi_name_eq(name, "open")) return "__kernel_abi_open";
  if (abi_name_eq(name, "close")) return "__kernel_abi_close";
  if (abi_name_eq(name, "mmap")) return "__kernel_abi_mmap";
  if (abi_name_eq(name, "munmap")) return "__kernel_abi_munmap";
  if (abi_name_eq(name, "exit")) return "__kernel_abi_exit";
  if (abi_name_eq(name, "dlopen")) return "__kernel_abi_dlopen";
  if (abi_name_eq(name, "dlsym")) return "__kernel_abi_dlsym";
  if (abi_name_eq(name, "dlclose")) return "__kernel_abi_dlclose";
  if (abi_name_eq(name, "dlerror")) return "__kernel_abi_dlerror";
  return name;
}

long kernel_abi_call(const char *name, long *args, int argc) {
  if (abi_name_eq(name, "read") && argc >= 3) return (long)read((int)args[0], (void *)args[1], (unsigned long)args[2]);
  if (abi_name_eq(name, "write") && argc >= 3) return (long)write((int)args[0], (const void *)args[1], (unsigned long)args[2]);
  if (abi_name_eq(name, "open") && argc >= 3) return (long)open((const char *)args[0], (int)args[1], (int)args[2]);
  if (abi_name_eq(name, "close") && argc >= 1) return (long)close((int)args[0]);
  if (abi_name_eq(name, "mmap") && argc >= 6) return (long)mmap((void *)args[0], (unsigned long)args[1], (int)args[2], (int)args[3], (int)args[4], (long)args[5]);
  if (abi_name_eq(name, "munmap") && argc >= 2) return (long)munmap((void *)args[0], (unsigned long)args[1]);
  if (abi_name_eq(name, "malloc") && argc >= 1) return (long)malloc((unsigned long)args[0]);
  if (abi_name_eq(name, "free") && argc >= 1) {
    free((void *)args[0]);
    return 0;
  }
  if (abi_name_eq(name, "calloc") && argc >= 2) return (long)calloc((unsigned long)args[0], (unsigned long)args[1]);
  if (abi_name_eq(name, "realloc") && argc >= 2) return (long)realloc((void *)args[0], (unsigned long)args[1]);
  if (abi_name_eq(name, "strlen") && argc >= 1) return (long)strlen((const char *)args[0]);
  if (abi_name_eq(name, "strcmp") && argc >= 2) return (long)strcmp((const char *)args[0], (const char *)args[1]);
  if (abi_name_eq(name, "memcpy") && argc >= 3) return (long)memcpy((void *)args[0], (const void *)args[1], (unsigned long)args[2]);
  if (abi_name_eq(name, "memset") && argc >= 3) return (long)memset((void *)args[0], (int)args[1], (unsigned long)args[2]);
  if (abi_name_eq(name, "strchr") && argc >= 2) return (long)strchr((const char *)args[0], (int)args[1]);
  if (abi_name_eq(name, "strrchr") && argc >= 2) return (long)strrchr((const char *)args[0], (int)args[1]);
  if (abi_name_eq(name, "isspace") && argc >= 1) return (long)isspace((int)args[0]);
  if (abi_name_eq(name, "isdigit") && argc >= 1) return (long)isdigit((int)args[0]);
  if (abi_name_eq(name, "isalpha") && argc >= 1) return (long)isalpha((int)args[0]);
  if (abi_name_eq(name, "isalnum") && argc >= 1) return (long)isalnum((int)args[0]);
  if (abi_name_eq(name, "strtod") && argc >= 1) {
    char *endp = 0;
    double d = strtod((const char *)args[0], argc >= 2 ? &endp : 0);
    if (argc >= 2 && args[1]) *(long *)args[1] = (long)endp;
    return (long)d;
  }
  if (abi_name_eq(name, "printf") && argc >= 1) {
    const char *fmt = (const char *)args[0];
#ifdef KERNEL_ABI_HOST_LIBC
    if (argc == 1) return (long)printf("%s", fmt);
    if (argc == 2) return (long)printf(fmt, args[1]);
    if (argc == 3) return (long)printf(fmt, args[1], args[2]);
    if (argc == 4) return (long)printf(fmt, args[1], args[2], args[3]);
    if (argc == 5) return (long)printf(fmt, args[1], args[2], args[3], args[4]);
    return (long)printf(fmt, args[1], args[2], args[3], args[4], args[5]);
#else
    return (long)KERNEL_ABI_UNKNOWN;
#endif
  }
  if (abi_name_eq(name, "eprintf") && argc >= 1) {
    (void)args;
#ifdef KERNEL_ABI_HOST_LIBC
    const char *fmt = (const char *)args[0];
    if (argc == 1) return (long)fprintf(stderr, "%s", fmt);
    if (argc == 2) return (long)fprintf(stderr, fmt, args[1]);
    if (argc == 3) return (long)fprintf(stderr, fmt, args[1], args[2]);
    if (argc == 4) return (long)fprintf(stderr, fmt, args[1], args[2], args[3]);
    if (argc == 5) return (long)fprintf(stderr, fmt, args[1], args[2], args[3], args[4]);
    return (long)fprintf(stderr, fmt, args[1], args[2], args[3], args[4], args[5]);
#else
    return (long)KERNEL_ABI_UNKNOWN;
#endif
  }
  if (abi_name_eq(name, "dlopen") && argc >= 1) {
    int flags = (argc >= 2) ? (int)args[1] : (RTLD_LAZY + RTLD_LOCAL);
    return (long)dlopen((const char *)args[0], flags);
  }
  if (abi_name_eq(name, "dlsym") && argc >= 2) {
    return (long)dlsym((void *)args[0], (const char *)args[1]);
  }
  if (abi_name_eq(name, "dlclose") && argc >= 1) {
    return (long)dlclose((void *)args[0]);
  }
  if (abi_name_eq(name, "dlerror")) {
    return (long)dlerror();
  }
  if (abi_name_eq(name, "exit") && argc >= 1) {
    exit((int)args[0]);
  }
  {
    void *h = abi_default_handle();
    void *fp = 0;
    if (h && name) fp = dlsym(h, name);
    if (fp) return abi_call_ptr(fp, args, argc);
  }
  return (long)KERNEL_ABI_UNKNOWN;
}

long kernel_abi_call_ptr(long fp, long *args, int argc) {
  return abi_call_ptr((void *)fp, args, argc);
}

long __kernel_abi_read(int fd, void *buf, size_t count) {
  return read(fd, buf, count);
}

long __kernel_abi_write(int fd, const void *buf, size_t count) {
  return write(fd, buf, count);
}

int __kernel_abi_open(const char *pathname, int flags, mode_t mode) {
  return open(pathname, flags, mode);
}

int __kernel_abi_close(int fd) {
  return close(fd);
}

void *__kernel_abi_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
  return mmap(addr, len, prot, flags, fd, off);
}

int __kernel_abi_munmap(void *addr, size_t len) {
  return munmap(addr, len);
}

void __kernel_abi_exit(int code) {
  exit(code);
}

void *__kernel_abi_dlopen(const char *filename, int flags) {
  return dlopen(filename, flags);
}

void *__kernel_abi_dlsym(void *handle, const char *symbol) {
  return dlsym(handle, symbol);
}

int __kernel_abi_dlclose(void *handle) {
  return dlclose(handle);
}

char *__kernel_abi_dlerror(void) {
  return dlerror();
}
