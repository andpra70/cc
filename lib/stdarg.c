#include "../include/stdarg.h"

/*
 * Minimal fallback runtime for stdarg.
 * Real variadic handling is implemented by compiler builtins in codegen.
 */

void va_start(va_list ap, long last_named_param) {
  (void)ap;
  (void)last_named_param;
}

long va_arg(va_list ap, int type_hint) {
  (void)type_hint;
  if (!ap) return 0;
  return *(long *)ap;
}

void va_end(va_list ap) {
  (void)ap;
}

void va_copy(va_list dst, va_list src) {
  (void)dst;
  (void)src;
}
