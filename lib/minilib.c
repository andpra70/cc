/*
 * Minimal runtime for self-hosted compiler.
 * Aggregates per-domain modules from lib/*.c.
 */

#include <stddef.h>
#include <sys/types.h>
#include <stdint.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "../lib/ctype.c"
#include "../lib/string.c"
#include "../lib/stdlib.c"
#include "../lib/fcntl.c"
#include "../lib/stdio.c"
#include "../lib/math.c"
