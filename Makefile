CC ?= cc
CFLAGS ?= -O0

cc: src/cc.c src/ast.c src/g_asm.c src/g_interpreter.c src/g_elf.c src/g_ast.c src/minilib.c
	$(CC) $(CFLAGS) src/cc.c -o cc

cpu: src/cpu.c src/minilib.c
	$(CC) $(CFLAGS) src/cpu.c -o cpu -lm

all: cc cpu

clean:
	rm -f *.elf2 *.elf *.llvm cc cpu compiler tmp/cc-self1 tmp/cc-self2 tmp/cc-self3 tmp/smoke.out

test: clean all src/test.c
	./cc src/test.c -v -a -o test.llvm
	./cpu test.llvm
	./cc src/test.c -o test.elf
	./test.elf

self: clean all src/cc.c
	./cc src/cc.c -o cc.elf
	./cc.elf src/cc.c -o cc.elf2
	./cc.elf2 src/test.c -o test.elf2
	./test.elf2
