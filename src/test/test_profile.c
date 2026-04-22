typedef unsigned long size_t;

extern long write(int fd, const void *buf, size_t count);
extern void *malloc(size_t n);
extern void *calloc(size_t n, size_t sz);
extern void *realloc(void *ptr, size_t n);
extern size_t dbg_heap_live_bytes(void);
extern size_t dbg_heap_peak_bytes(void);
extern size_t dbg_stack_peak_bytes(void);
extern void dbg_stack_reset(void);
extern long kernel_abi_call(const char *name, long *args, int argc);

void out_ch(char c) {
  write(1, &c, 1);
}

void out_str(char *s) {
  int i = 0;
  while (s[i]) {
    out_ch(s[i]);
    i = i + 1;
  }
}

void out_nl() {
  out_ch('\n');
}

void out_u(size_t v) {
  char buf[32];
  int i = 0;
  if (v == 0) {
    out_ch('0');
    return;
  }
  while (v > 0 && i < 31) {
    buf[i++] = (char)('0' + (v % 10));
    v = v / 10;
  }
  while (i > 0) out_ch(buf[--i]);
}

void report(char *name, int ok) {
  out_str(name);
  out_ch(':');
  out_ch(ok ? 'V' : 'X');
  out_nl();
}

int recurse_stack(int n) {
  char pad[256];
  long ka[1];
  int i = 0;
  while (i < 256) {
    pad[i] = (char)(i + n);
    i = i + 1;
  }
  ka[0] = 48;
  (void)kernel_abi_call("isdigit", ka, 1);
  if (n <= 0) return (int)pad[0];
  return recurse_stack(n - 1) + (int)pad[1];
}

int test_heap_profile() {
  size_t before = dbg_heap_live_bytes();
  size_t peak_before = dbg_heap_peak_bytes();
  void *a = malloc(64);
  void *b = calloc(4, 64);
  void *c = realloc(a, 512);
  size_t live = dbg_heap_live_bytes();
  size_t peak = dbg_heap_peak_bytes();
  return a != 0 && b != 0 && c != 0 && live > before && peak >= peak_before && peak >= live;
}

int test_stack_profile() {
  size_t peak;
  dbg_stack_reset();
  (void)recurse_stack(64);
  peak = dbg_stack_peak_bytes();
  return peak > 0;
}

int main() {
  int fails = 0;
  int ok;

  ok = test_heap_profile();
  report("heap_profile", ok);
  if (!ok) fails = fails + 1;

  ok = test_stack_profile();
  report("stack_profile", ok);
  if (!ok) fails = fails + 1;

  out_str("heap_live=");
  out_u(dbg_heap_live_bytes());
  out_nl();
  out_str("heap_peak=");
  out_u(dbg_heap_peak_bytes());
  out_nl();
  out_str("stack_peak=");
  out_u(dbg_stack_peak_bytes());
  out_nl();

  out_str("total_fail=");
  out_u((size_t)fails);
  out_nl();
  return fails;
}
