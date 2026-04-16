CC ?= cc
CFLAGS ?= -O0

cc: cc.c ast.c g_asm.c g_interpreter.c g_elf.c g_ast.c libc.c
	$(CC) $(CFLAGS) cc.c -o cc

clean:
	rm -f cc compiler tmp/cc-self1 tmp/cc-self2 tmp/cc-self3 tmp/smoke.out
