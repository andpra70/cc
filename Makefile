CC ?= tcc
CPPFLAGS ?= -Iinclude
CFLAGS ?= -O0
C99 ?= c99
GCC ?= gcc
RANLIB ?= ranlib
TEST_DIR ?= src/test

c99: src/c99.c src/ast.c src/g_asm.c src/g_interpreter.c src/g_elf.c src/g_llvm.c src/minilib.c
	$(CC) $(CPPFLAGS) $(CFLAGS) src/c99.c -o c99

cpu: src/cpu.c src/minilib.c
	$(CC) $(CPPFLAGS) $(CFLAGS) src/cpu.c -o cpu -lm

all: c99 cpu

clean:
	rm -f *.elf2 *.elf *.llvm c99 cpu compiler test_profile.host tmp/cc-self1 tmp/cc-self2 tmp/cc-self3 tmp/smoke.out

test: clean all $(TEST_DIR)/test.c test-mixed-objects test-profile
	./c99 $(TEST_DIR)/test.c -v -a -o test.llvm
	./cpu test.llvm
	./c99 $(TEST_DIR)/test.c -o test.elf
	./test.elf

test-mixed-objects: all $(TEST_DIR)/test_mix_cc.c $(TEST_DIR)/test_mix_c99.c $(TEST_DIR)/test_mix_gcc.c $(TEST_DIR)/test_mix_main.c
	./c99 $(TEST_DIR)/test_mix_cc.c -c -o /tmp/test_mix_cc.o
	./c99 $(TEST_DIR)/test_mix_cc.c -ar -o /tmp/libtest_mix_cc.a
	$(RANLIB) /tmp/libtest_mix_cc.a
	$(C99) -c $(TEST_DIR)/test_mix_c99.c -o /tmp/test_mix_c99.o
	$(GCC) -c $(TEST_DIR)/test_mix_gcc.c -o /tmp/test_mix_gcc.o
	$(GCC) -c $(TEST_DIR)/test_mix_main.c -o /tmp/test_mix_main_gcc.o
	$(C99) -c $(TEST_DIR)/test_mix_main.c -o /tmp/test_mix_main_c99.o
	$(GCC) /tmp/test_mix_main_gcc.o /tmp/test_mix_cc.o /tmp/test_mix_c99.o /tmp/test_mix_gcc.o -o /tmp/test_mix_link_gcc_o
	$(C99) /tmp/test_mix_main_c99.o /tmp/test_mix_cc.o /tmp/test_mix_c99.o /tmp/test_mix_gcc.o -o /tmp/test_mix_link_c99_o
	$(GCC) /tmp/test_mix_main_gcc.o /tmp/test_mix_c99.o /tmp/test_mix_gcc.o /tmp/libtest_mix_cc.a -o /tmp/test_mix_link_gcc_a
	$(C99) /tmp/test_mix_main_c99.o /tmp/test_mix_c99.o /tmp/test_mix_gcc.o /tmp/libtest_mix_cc.a -o /tmp/test_mix_link_c99_a
	/tmp/test_mix_link_gcc_o
	/tmp/test_mix_link_c99_o
	/tmp/test_mix_link_gcc_a
	/tmp/test_mix_link_c99_a

test-profile: $(TEST_DIR)/test_profile.c lib/minilib.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_DIR)/test_profile.c lib/minilib.c -o test_profile.host -ldl -lm
	./test_profile.host

self: clean all src/c99.c
	./c99 src/c99.c -o c99.elf
	./c99.elf src/c99.c -o c99.elf2
	./c99.elf2 $(TEST_DIR)/test.c -o test.elf2
	./test.elf2

cpu2: clean all src/c99.c
	./c99 src/c99.c -o c99.elf
	./c99.elf src/c99.c -o c99.elf2
	./c99.elf2 $(TEST_DIR)/test.c -o test.elf2
	./test.elf2

elfs: test
	readelf -d test.elf | grep NEEDED
	nm -D test.elf 

dyn: clean all src/dyn.c
	$(CC) $(CPPFLAGS) $(CFLAGS) src/dyn.c -o dyn
	./dyn
	./c99 src/dyn.c -o dyn.elf

push:
	git add .
	git commit -m "update"
	git push
	
