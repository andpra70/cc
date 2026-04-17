#ifndef CC_STDARG_H
#define CC_STDARG_H

typedef char *va_list;

/*
 * Minimal stdarg support handled directly by the compiler backend.
 * Usage is the usual C form:
 *   va_list ap;
 *   va_start(ap, last_named_param);
 *   x = va_arg(ap, int);
 *   va_end(ap);
 */
void va_start(va_list ap, long last_named_param);
long va_arg(va_list ap, int type_hint);
void va_end(va_list ap);
void va_copy(va_list dst, va_list src);

#endif
