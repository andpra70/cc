#include "config.h"
#include "ast.c"
#include "g_asm.c"
#include "g_interpreter.c"
#include "g_elf.c"
#include "g_llvm.c"

#define MODE_ELF 0
#define MODE_ASM 1
#define MODE_LLVM 2
#define MODE_INTERP 3

#define ELF_OUT_EXEC 0
#define ELF_OUT_OBJ 1
#define ELF_OUT_AR 2

char *read_file_source(const char *path) {
  int fd = open(path, O_RDONLY, 0);
  size_t cap = CC_CFG_IO_BUFFER_INIT;
  size_t len = 0;
  char *buf;
  if (fd < 0) return NULL;

  buf = malloc(cap + 1);
  if (!buf) {
    close(fd);
    return NULL;
  }

  while (1) {
    long n;
    if (len == cap) {
      char *nb;
      cap *= 2;
      nb = realloc(buf, cap + 1);
      if (!nb) {
        free(buf);
        close(fd);
        return NULL;
      }
      buf = nb;
    }
    n = read(fd, buf + len, cap - len);
    if (n < 0) {
      free(buf);
      close(fd);
      return NULL;
    }
    if (n == 0) break;
    len += (size_t)n;
  }

  close(fd);
  buf[len] = 0;
  return buf;
}

char *derive_output_path(const char *input, const char *ext) {
  const char *slash = NULL;
  const char *dot = NULL;
  const char *p = input;
  size_t base_len;
  size_t ext_len = strlen(ext);
  char *out;

  while (*p) {
    if (*p == '/') slash = p;
    if (*p == '.') dot = p;
    p++;
  }

  if (dot && (!slash || dot > slash)) base_len = (size_t)(dot - input);
  else base_len = strlen(input);

  out = malloc(base_len + ext_len + 1);
  if (!out) return NULL;
  memcpy(out, input, base_len);
  memcpy(out + base_len, ext, ext_len);
  out[base_len + ext_len] = 0;
  return out;
}

char *derive_output_name(const char *input, const char *ext) {
  const char *slash = NULL;
  const char *dot = NULL;
  const char *p = input;
  const char *base;
  size_t base_len;
  size_t ext_len = strlen(ext);
  char *out;

  while (*p) {
    if (*p == '/') slash = p;
    if (*p == '.') dot = p;
    p++;
  }

  base = slash ? slash + 1 : input;
  if (dot && dot > base) base_len = (size_t)(dot - base);
  else base_len = strlen(base);

  out = malloc(base_len + ext_len + 1);
  if (!out) return NULL;
  memcpy(out, base, base_len);
  memcpy(out + base_len, ext, ext_len);
  out[base_len + ext_len] = 0;
  return out;
}

char *join_output_dir_file(const char *dir, const char *file) {
  size_t dlen = strlen(dir);
  size_t flen = strlen(file);
  int need_slash = 1;
  char *out;
  if (dlen > 0 && dir[dlen - 1] == '/') need_slash = 0;
  out = malloc(dlen + (size_t)need_slash + flen + 1);
  if (!out) return NULL;
  memcpy(out, dir, dlen);
  if (need_slash) out[dlen++] = '/';
  memcpy(out + dlen, file, flen);
  out[dlen + flen] = 0;
  return out;
}

int redirect_stdout_to_path(const char *path) {
  int fd;
  close(1);
  fd = open(path, O_WRONLY + O_CREAT + O_TRUNC, 493);
  if (fd < 0) return -1;
  if (fd != 1) {
    close(fd);
    return -1;
  }
  return 0;
}

int str_has_suffix(const char *s, const char *suffix) {
  size_t n = strlen(s);
  size_t m = strlen(suffix);
  size_t i = 0;
  if (n < m) return 0;
  while (i < m) {
    if (s[n - m + i] != suffix[i]) return 0;
    i++;
  }
  return 1;
}

int compiler_entry(int argc, char **argv, int argi) {
  int mode = MODE_ELF;
  int elf_out_kind = ELF_OUT_EXEC;
  int verbose = 0;
  char *input_path = NULL;
  char *output_path = NULL;
  char *output_file = NULL;
  char *output_dir = NULL;
  char *source;
  int i;

  for (i = argi; i < argc; i++) {
    if (!strcmp(argv[i], "-s")) {
      mode = MODE_ASM;
      continue;
    }
    if (!strcmp(argv[i], "-c")) {
      elf_out_kind = ELF_OUT_OBJ;
      continue;
    }
    if (!strcmp(argv[i], "-ar")) {
      elf_out_kind = ELF_OUT_AR;
      continue;
    }
    if (!strcmp(argv[i], "-a")) {
      mode = MODE_LLVM;
      continue;
    }
    if (!strcmp(argv[i], "-l")) {
      i++;
      if (i >= argc) {
        eprintf("Error: missing argument after -l\n", 0, 0, 0, 0);
        return 1;
      }
      output_dir = argv[i];
      continue;
    }
    if (!strcmp(argv[i], "-i")) {
      mode = MODE_INTERP;
      continue;
    }
    if (!strcmp(argv[i], "-v")) {
      verbose = 1;
      continue;
    }
    if (!strcmp(argv[i], "-o")) {
      i++;
      if (i >= argc) {
        eprintf("Error: missing argument after -o\n", 0, 0, 0, 0);
        return 1;
      }
      output_file = argv[i];
      continue;
    }
    if (argv[i][0] == '-') {
      eprintf("Error: unknown option %s\n", (long)argv[i], 0, 0, 0);
      return 1;
    }
    if (input_path) {
      eprintf("Error: multiple input files not supported\n", 0, 0, 0, 0);
      return 1;
    }
    input_path = argv[i];
  }

  if (!input_path) {
    eprintf("Usage: %s [-s|-a|-i|-c|-ar] [-v] [-l out_dir] [-o out_file] input.c\n", (long)argv[0], 0, 0, 0);
    return 1;
  }
  if (mode != MODE_ELF) elf_out_kind = ELF_OUT_EXEC;
  if (mode == MODE_ELF && output_file) {
    if (str_has_suffix(output_file, ".o")) elf_out_kind = ELF_OUT_OBJ;
    else if (str_has_suffix(output_file, ".a")) elf_out_kind = ELF_OUT_AR;
  }
  parse_verbose = verbose;
  if (verbose) {
    eprintf("[v] mode=%d elf_out=%d input=%s out_file=%s\n", mode, elf_out_kind, (long)input_path,
            (long)(output_file ? output_file : "(auto)"));
    eprintf("[v] out_dir=%s\n", (long)(output_dir ? output_dir : "(cwd)"), 0, 0, 0);
  }

  source = read_file_source(input_path);
  if (verbose) eprintf("[v] read_file_source done ptr=%ld\n", (long)source, 0, 0, 0);
  if (!source) {
    eprintf("Error: cannot read input file: %s\n", (long)input_path, 0, 0, 0);
    return 1;
  }

  if (mode == MODE_INTERP) {
    if (verbose) eprintf("[v] run interpreter\n", 0, 0, 0, 0);
    return run_source_as_program(source);
  }

  if (!output_file) {
    if (output_dir) {
      if (mode == MODE_ASM) output_file = derive_output_name(input_path, ".asm");
      else if (mode == MODE_LLVM) output_file = derive_output_name(input_path, ".llvm");
      else if (elf_out_kind == ELF_OUT_OBJ) output_file = derive_output_name(input_path, ".o");
      else if (elf_out_kind == ELF_OUT_AR) output_file = derive_output_name(input_path, ".a");
      else output_file = derive_output_name(input_path, ".elf");
    } else {
      if (mode == MODE_ASM) output_path = derive_output_path(input_path, ".asm");
      else if (mode == MODE_LLVM) output_path = derive_output_path(input_path, ".llvm");
      else if (elf_out_kind == ELF_OUT_OBJ) output_path = derive_output_path(input_path, ".o");
      else if (elf_out_kind == ELF_OUT_AR) output_path = derive_output_path(input_path, ".a");
      else output_path = derive_output_path(input_path, ".elf");
    }
  }

  if (!output_path) {
    if (output_dir) output_path = join_output_dir_file(output_dir, output_file);
    else output_path = output_file;
  }

  if (!output_path) {
    eprintf("Error: out of memory creating output path\n", 0, 0, 0, 0);
    return 1;
  }

  if (mode == MODE_ASM) {
    if (verbose) eprintf("[v] emit asm -> %s\n", (long)output_path, 0, 0, 0);
    if (redirect_stdout_to_path(output_path) != 0) {
      eprintf("Error: cannot open output file: %s\n", (long)output_path, 0, 0, 0);
      return 1;
    }
    return emit_asm_from_source(source);
  }

  if (mode == MODE_LLVM) {
    if (verbose) eprintf("[v] emit llvm -> %s\n", (long)output_path, 0, 0, 0);
    if (redirect_stdout_to_path(output_path) != 0) {
      eprintf("Error: cannot open output file: %s\n", (long)output_path, 0, 0, 0);
      return 1;
    }
    return emit_ast_from_source(source);
  }

  if (elf_out_kind == ELF_OUT_OBJ) {
    if (verbose) eprintf("[v] emit elf relocatable object -> %s\n", (long)output_path, 0, 0, 0);
    return compile_to_object_source_path(source, output_path);
  }
  if (elf_out_kind == ELF_OUT_AR) {
    char *member_name = derive_output_name(input_path, ".o");
    int rc;
    if (!member_name) {
      eprintf("Error: out of memory creating archive member name\n", 0, 0, 0, 0);
      return 1;
    }
    if (verbose) eprintf("[v] emit posix archive -> %s (member=%s)\n", (long)output_path, (long)member_name, 0, 0);
    rc = compile_to_archive_source_path(source, output_path, member_name);
    free(member_name);
    return rc;
  }

  if (verbose) eprintf("[v] emit elf exec -> %s\n", (long)output_path, 0, 0, 0);
  return compile_to_elf_source_path(source, output_path);
}

int main(int argc, char **argv) {
  int rc = compiler_entry(argc, argv, 1);
  exit(rc);
  return rc;
}
