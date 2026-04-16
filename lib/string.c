#include "../include/string.h"

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

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a - *b;
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
