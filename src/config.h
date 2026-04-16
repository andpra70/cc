#ifndef CC_CONFIG_H
#define CC_CONFIG_H

/*
 * Memory / footprint tuning knobs.
 * Increase for larger inputs or deeper nesting.
 * Decrease to reduce RAM usage.
 */

/* Generic input/read buffers */
#define CC_CFG_IO_BUFFER_INIT 4096

/* Preprocessor include-expansion stack depth */
#define CC_CFG_PP_SRC_STACK_MAX 64

/* ELF codegen: initial binary buffer chunk */
#define CC_CFG_ELF_CODE_INIT 1024

/* ELF codegen: initial label/fixup table capacities */
#define CC_CFG_ELF_LABELS_INIT 64
#define CC_CFG_ELF_FIXUPS_INIT 64

/* ELF codegen: bytes reserved per compiled function metadata */
#define CC_CFG_ELF_FUNC_SLOT_BYTES 64

/* ELF codegen: max nested loops/switches for break/continue stacks */
#define CC_CFG_ELF_LOOP_NEST_MAX 128

/* ELF codegen: max number of unique string literals per compilation unit */
#define CC_CFG_ELF_STR_LIT_MAX 8192

/* Parser/codegen guard against pathological/cyclic call argument lists */
#define CC_CFG_CALL_ARG_GUARD_MAX 128

#endif
