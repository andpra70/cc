#include "ast.c"
#include "g_asm.c"
#include "g_interpreter.c"
#include "g_elf.c"
#include "g_ast.c"

int compiler_entry(int argc, char **argv, int argi) {
  int asm_only = 0;
  int ast_only = 0;
  if (argi < argc && !strcmp(argv[argi], "-s")) {
    asm_only = 1;
    argi++;
  } else if (argi < argc && !strcmp(argv[argi], "-a")) {
    ast_only = 1;
    argi++;
  }
  if (argi < argc) {
    eprintf("Usage: %s [-s|-a] < stdin.c > out\n", (long)argv[0], 0, 0, 0, 0, 0);
    return 1;
  }
  if (asm_only) return emit_asm_only();
  if (ast_only) return emit_ast_only();
  return compile_to_elf();
}

int main(int argc, char **argv) {
  return compiler_entry(argc, argv, 1);
}
