extern int mix_from_cc();
extern int mix_from_c99();
extern int mix_from_gcc();
extern long write(int fd, const void *buf, unsigned long count);

int main() {
  int sum = mix_from_cc() + mix_from_c99() + mix_from_gcc();
  if (sum == 31) {
    write(1, "OK\n", 3);
    return 0;
  }
  write(1, "KO\n", 3);
  return 1;
}
