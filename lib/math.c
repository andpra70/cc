double fabs(double x) { return x < 0.0 ? -x : x; }

double pow(double x, double y) {
  long i;
  double r = 1.0;
  long n = (long)y;
  if ((double)n != y) return 0.0;
  if (n < 0) {
    if (x == 0.0) return 0.0;
    n = -n;
    for (i = 0; i < n; i++) r *= x;
    return 1.0 / r;
  }
  for (i = 0; i < n; i++) r *= x;
  return r;
}
