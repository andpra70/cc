#include <dlfcn.h>

int main() {
  void *handle;
  int (*p_puts)(const char *);

  handle = dlopen((const char *)0, RTLD_LAZY);
  if (handle) {
    p_puts = (int (*)(const char *))dlsym(handle, "puts");
    if (p_puts) {
      p_puts("dyn:ok");
      (*p_puts)("dyn:deref-ok");
    }
    dlclose(handle);
  }
  return 0;
}
