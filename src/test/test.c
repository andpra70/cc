typedef unsigned long size_t;

typedef struct Pair {
  int a;
  int b;
} Pair;

typedef union Num {
  int i;
  char c;
} Num;

extern long write(int fd, const void *buf, size_t count);
extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);

#define RTLD_LAZY 1

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

void report(char *name, int ok) {
  out_str(name);
  out_ch(':');
  if (ok) out_ch('V');
  else out_ch('X');
  out_nl();
}

int test_math() {
  int x = 5;
  int y = 3;
  int z = x * y + 10 - 4;
  return z == 21;
}

int test_logic() {
  int a = 1;
  int b = 0;
  int c = (a && !b) || (b && a);
  return c == 1;
}

int test_if_else() {
  int x = 0;
  if (3 > 2) x = 7;
  else x = 9;
  return x == 7;
}

int test_while() {
  int i = 0;
  int s = 0;
  while (i < 5) {
    s = s + i;
    i = i + 1;
  }
  return s == 10;
}

int test_for() {
  int i;
  int s = 0;
  for (i = 1; i <= 4; i = i + 1) {
    s = s + i;
  }
  return s == 10;
}

int test_switch() {
  int v = 3;
  int out = 0;
  switch (v) {
    case 1: out = 10; break;
    case 2: out = 20; break;
    case 3: out = 30; break;
    default: out = 40; break;
  }
  return out == 30;
}

int test_memory() {
  char src[4];
  char dst[4];
  int i = 0;
  src[0] = 'a';
  src[1] = 'b';
  src[2] = 'c';
  src[3] = 0;
  while (i < 4) {
    dst[i] = src[i];
    i = i + 1;
  }
  return dst[0] == 'a' && dst[1] == 'b' && dst[2] == 'c' && dst[3] == 0;
}

int test_pointer() {
  int a[4];
  int *p = a;
  p[0] = 11;
  p[1] = 12;
  p[2] = 13;
  p[3] = 14;
  return a[0] + a[1] + a[2] + a[3] == 50;
}

int test_struct_typedef() {
  Pair p;
  p.a = 4;
  p.b = 6;
  return p.a + p.b == 10;
}

int test_union() {
  Num n;
  n.i = 65;
  return n.c == 'A';
}

int sum6(int a, int b, int c, int d, int e, int f) {
  return a + b + c + d + e + f;
}

int test_calls() {
  return sum6(1, 2, 3, 4, 5, 6) == 21;
}

int fact(int n) {
  if (n <= 1) return 1;
  return n * fact(n - 1);
}

int test_recursion() {
  return fact(5) == 120;
}

int test_inc_dec() {
  int i = 1;
  int a = i++;
  int b = ++i;
  return a == 1 && b == 3 && i == 3;
}

int test_ternary() {
  int a = 9;
  int b = (a > 5) ? 2 : 3;
  return b == 2;
}

int test_bitand() {
  int a = 14;
  int b = 11;
  return (a & b) == 10;
}

int test_bitor() {
  int a = 12;
  int b = 3;
  return (a | b) == 15;
}

int test_mod() {
  int a = 29;
  int b = 6;
  return (a % b) == 5;
}

int test_mod_assign() {
  int a = 29;
  a %= 6;
  return a == 5;
}

int test_pow() {
  int a = 2 ** 5;
  int b = 3 ** 3;
  return a == 32 && b == 27;
}

int test_casts() {
  int *p;
  long v;
  int (*fp)(int);
  p = (int *)0;
  v = (long)p;
  p = (int *)v;
  fp = (int (*)(int))0;
  return p == 0 && v == 0 && fp == 0;
}

int test_dyn() {
  void *h;
  long (*pwrite)(int, void *, size_t);
  h = dlopen((char *)0, RTLD_LAZY);
  if (!h) return 1;
  pwrite = (long (*)(int, void *, size_t))dlsym(h, "write");
  if (!pwrite) { dlclose(h); return 1; }
  dlclose(h);
  return 1;
}

int main() {
  int fails = 0;
  int ok;
  int tens;
  int ones;

  ok = test_math(); report("math", ok); if (!ok) fails = fails + 1;
  ok = test_logic(); report("logic", ok); if (!ok) fails = fails + 1;
  ok = test_if_else(); report("if_else", ok); if (!ok) fails = fails + 1;
  ok = test_while(); report("while", ok); if (!ok) fails = fails + 1;
  ok = test_for(); report("for", ok); if (!ok) fails = fails + 1;
  ok = test_switch(); report("switch", ok); if (!ok) fails = fails + 1;
  ok = test_memory(); report("memory", ok); if (!ok) fails = fails + 1;
  ok = test_pointer(); report("pointer", ok); if (!ok) fails = fails + 1;
  ok = test_struct_typedef(); report("struct_typedef", ok); if (!ok) fails = fails + 1;
  ok = test_union(); report("union", ok); if (!ok) fails = fails + 1;
  ok = test_calls(); report("calls", ok); if (!ok) fails = fails + 1;
  ok = test_recursion(); report("recursion", ok); if (!ok) fails = fails + 1;
  ok = test_inc_dec(); report("inc_dec", ok); if (!ok) fails = fails + 1;
  ok = test_ternary(); report("ternary", ok); if (!ok) fails = fails + 1;
  ok = test_bitand(); report("bitand", ok); if (!ok) fails = fails + 1;
  ok = test_bitor(); report("bitor", ok); if (!ok) fails = fails + 1;
  ok = test_mod(); report("mod", ok); if (!ok) fails = fails + 1;
  ok = test_mod_assign(); report("mod_assign", ok); if (!ok) fails = fails + 1;
  ok = test_pow(); report("pow", ok); if (!ok) fails = fails + 1;
  ok = test_casts(); report("casts", ok); if (!ok) fails = fails + 1;
  ok = test_dyn(); report("dyn", ok); if (!ok) fails = fails + 1;

  out_str("total_fail:");
  tens = fails / 10;
  ones = fails - tens * 10;
  if (fails >= 10) out_ch('0' + tens);
  out_ch('0' + ones);
  out_nl();

  return fails;
}
