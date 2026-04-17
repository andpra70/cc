size_t strlen(const char *s) {
  size_t n = 0;
  while (s && s[n]) n++;
  return n;
}

void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  size_t i = 0;
  while (i < n) { d[i] = s[i]; i++; }
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  unsigned char *d = (unsigned char *)dst;
  size_t i = 0;
  while (i < n) { d[i] = (unsigned char)c; i++; }
  return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  size_t i = 0;
  while (i < n) {
    if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    i++;
  }
  return 0;
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a - *b;
}

int strncmp(const char *a, const char *b, size_t n) {
  size_t i = 0;
  while (i < n && a[i] && b[i]) {
    if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    i++;
  }
  if (i == n) return 0;
  return (unsigned char)a[i] - (unsigned char)b[i];
}

char *strchr(const char *s, int c) {
  unsigned char ch = (unsigned char)c;
  while (*s) {
    if ((unsigned char)*s == ch) return (char *)s;
    s++;
  }
  if (ch == 0) return (char *)s;
  return (char *)0;
}

char *strrchr(const char *s, int c) {
  const char *last = (const char *)0;
  unsigned char ch = (unsigned char)c;
  while (*s) {
    if ((unsigned char)*s == ch) last = s;
    s++;
  }
  if (ch == 0) return (char *)s;
  return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
  size_t i;
  size_t j;
  if (!*needle) return (char *)haystack;
  for (i = 0; haystack[i]; i++) {
    j = 0;
    while (needle[j] && haystack[i + j] == needle[j]) j++;
    if (!needle[j]) return (char *)(haystack + i);
  }
  return (char *)0;
}

static int is_delim_char(char ch, const char *delim) {
  while (*delim) {
    if (ch == *delim) return 1;
    delim++;
  }
  return 0;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
  char *tok;
  if (!saveptr || !delim) return (char *)0;
  if (!str) str = *saveptr;
  if (!str) return (char *)0;

  while (*str && is_delim_char(*str, delim)) str++;
  if (!*str) {
    *saveptr = str;
    return (char *)0;
  }

  tok = str;
  while (*str && !is_delim_char(*str, delim)) str++;
  if (*str) {
    *str = 0;
    str++;
  }
  *saveptr = str;
  return tok;
}
