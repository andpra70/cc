#ifndef CC_KERNEL_ABI_H
#define CC_KERNEL_ABI_H

#define KERNEL_ABI_UNKNOWN 0x7fffffff00000000L

int kernel_abi_is_builtin(const char *name);
const char *kernel_abi_symbol(const char *name);
long kernel_abi_call(const char *name, long *args, int argc);

#endif
