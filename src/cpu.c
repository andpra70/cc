#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <math.h>

typedef long i64;

enum {
  OP_NOP = 0,
  OP_PUSH_CONST,
  OP_PUSH_STR,
  OP_LOAD_LOCAL,
  OP_STORE_LOCAL,
  OP_LOAD_GLOBAL,
  OP_STORE_GLOBAL,
  OP_ADDR_LOCAL,
  OP_ADDR_GLOBAL,
  OP_LOAD_INDIRECT,
  OP_STORE_INDIRECT,
  OP_LOAD_INDIRECT1,
  OP_LOAD_INDIRECT2,
  OP_LOAD_INDIRECT4,
  OP_LOAD_INDIRECT8,
  OP_STORE_INDIRECT1,
  OP_STORE_INDIRECT2,
  OP_STORE_INDIRECT4,
  OP_STORE_INDIRECT8,
  OP_CALL,
  OP_DROP,
  OP_RET,
  OP_BR,
  OP_BR_FALSE,
  OP_UN_NOT,
  OP_UN_NEG,
  OP_UN_BNOT,
  OP_BIN_ADD,
  OP_BIN_SUB,
  OP_BIN_MUL,
  OP_BIN_DIV,
  OP_CMP_EQ,
  OP_CMP_NE,
  OP_CMP_LT,
  OP_CMP_LE,
  OP_CMP_GT,
  OP_CMP_GE,
  OP_BIN_LAND,
  OP_BIN_LOR,
  OP_BIN_BAND,
  OP_BIN_BOR
};

typedef struct {
  char *name;
  i64 val;
} KV;

typedef struct {
  KV *v;
  int n;
  int cap;
} Map;

typedef struct {
  int op;
  i64 imm;
  char *name;
  int argc;
  int target;
  int off;
} Instr;

typedef struct {
  int off;
  char *name;
} OffName;

typedef struct {
  char *name;
  int pc;
} Label;

typedef struct {
  char *name;
  Instr *code;
  int ncode;
  int ccode;
  Label *labels;
  int nlabels;
  int clabels;
  OffName *offs;
  int noffs;
  int coffs;
  char **param_names;
  int nparams;
  int frame_size;
} Function;

typedef struct {
  Function *fns;
  int nfns;
  int cfns;
  Map globals;
} Program;

typedef struct {
  Function *fn;
  int pc;
  Map locals;
  unsigned char *mem;
  int mem_size;
  unsigned char *base;
  i64 *stack;
  int sp;
  int cap;
} Frame;

typedef struct {
  char *name;
  int maxargc;
} CallStat;

static void *xmalloc(size_t n) {
  void *p = malloc(n ? n : 1);
  if (!p) {
    fprintf(stderr, "cpu: out of memory\n");
    exit(2);
  }
  return p;
}

static char *xstrdup(const char *s) {
  size_t n = strlen(s);
  char *p = (char *)xmalloc(n + 1);
  memcpy(p, s, n + 1);
  return p;
}

static char *trim(char *s) {
  while (*s && isspace((unsigned char)*s)) s++;
  if (!*s) return s;
  char *e = s + strlen(s) - 1;
  while (e >= s && isspace((unsigned char)*e)) {
    *e = 0;
    e--;
  }
  return s;
}

static i64 *map_ref(Map *m, const char *name, int create) {
  int i;
  for (i = 0; i < m->n; i++) {
    if (!strcmp(m->v[i].name, name)) return &m->v[i].val;
  }
  if (!create) return NULL;
  if (m->n == m->cap) {
    int ncap = m->cap ? m->cap * 2 : 64;
    m->v = (KV *)realloc(m->v, (size_t)ncap * sizeof(KV));
    if (!m->v) {
      fprintf(stderr, "cpu: out of memory\n");
      exit(2);
    }
    m->cap = ncap;
  }
  m->v[m->n].name = xstrdup(name);
  m->v[m->n].val = 0;
  m->n++;
  return &m->v[m->n - 1].val;
}

static void fn_add_instr(Function *f, Instr in) {
  if (f->ncode == f->ccode) {
    int ncap = f->ccode ? f->ccode * 2 : 256;
    f->code = (Instr *)realloc(f->code, (size_t)ncap * sizeof(Instr));
    if (!f->code) {
      fprintf(stderr, "cpu: out of memory\n");
      exit(2);
    }
    f->ccode = ncap;
  }
  f->code[f->ncode++] = in;
}

static void fn_add_label(Function *f, const char *name, int pc) {
  if (f->nlabels == f->clabels) {
    int ncap = f->clabels ? f->clabels * 2 : 64;
    f->labels = (Label *)realloc(f->labels, (size_t)ncap * sizeof(Label));
    if (!f->labels) {
      fprintf(stderr, "cpu: out of memory\n");
      exit(2);
    }
    f->clabels = ncap;
  }
  f->labels[f->nlabels].name = xstrdup(name);
  f->labels[f->nlabels].pc = pc;
  f->nlabels++;
}

static void fn_note_off(Function *f, int off, const char *name) {
  int i;
  if (off < 0 || !name || !*name) return;
  for (i = 0; i < f->noffs; i++) {
    if (f->offs[i].off == off) return;
  }
  if (f->noffs == f->coffs) {
    int ncap = f->coffs ? f->coffs * 2 : 32;
    f->offs = (OffName *)realloc(f->offs, (size_t)ncap * sizeof(OffName));
    if (!f->offs) {
      fprintf(stderr, "cpu: out of memory\n");
      exit(2);
    }
    f->coffs = ncap;
  }
  f->offs[f->noffs].off = off;
  f->offs[f->noffs].name = xstrdup(name);
  f->noffs++;
}

static int fn_label_pc(Function *f, const char *name) {
  int i;
  for (i = 0; i < f->nlabels; i++) {
    if (!strcmp(f->labels[i].name, name)) return f->labels[i].pc;
  }
  return -1;
}

static Function *prog_add_fn(Program *p, const char *name) {
  if (p->nfns == p->cfns) {
    int ncap = p->cfns ? p->cfns * 2 : 64;
    p->fns = (Function *)realloc(p->fns, (size_t)ncap * sizeof(Function));
    if (!p->fns) {
      fprintf(stderr, "cpu: out of memory\n");
      exit(2);
    }
    memset(p->fns + p->cfns, 0, (size_t)(ncap - p->cfns) * sizeof(Function));
    p->cfns = ncap;
  }
  Function *f = &p->fns[p->nfns++];
  memset(f, 0, sizeof(*f));
  f->name = xstrdup(name);
  return f;
}

static Function *prog_find_fn(Program *p, const char *name) {
  int i;
  for (i = 0; i < p->nfns; i++) {
    if (!strcmp(p->fns[i].name, name)) return &p->fns[i];
  }
  return NULL;
}

static int cmp_off(const void *a, const void *b) {
  const OffName *x = (const OffName *)a;
  const OffName *y = (const OffName *)b;
  if (x->off < y->off) return -1;
  if (x->off > y->off) return 1;
  return 0;
}

static void stat_note(CallStat **arr, int *n, int *cap, const char *name, int argc) {
  int i;
  for (i = 0; i < *n; i++) {
    if (!strcmp((*arr)[i].name, name)) {
      if (argc > (*arr)[i].maxargc) (*arr)[i].maxargc = argc;
      return;
    }
  }
  if (*n == *cap) {
    int ncap = *cap ? *cap * 2 : 64;
    *arr = (CallStat *)realloc(*arr, (size_t)ncap * sizeof(CallStat));
    if (!*arr) {
      fprintf(stderr, "cpu: out of memory\n");
      exit(2);
    }
    *cap = ncap;
  }
  (*arr)[*n].name = xstrdup(name);
  (*arr)[*n].maxargc = argc;
  (*n)++;
}

static int stat_get(CallStat *arr, int n, const char *name) {
  int i;
  for (i = 0; i < n; i++) if (!strcmp(arr[i].name, name)) return arr[i].maxargc;
  return -1;
}

static int fn_off_by_name(Function *f, const char *name) {
  int i;
  for (i = 0; i < f->noffs; i++) {
    if (!strcmp(f->offs[i].name, name)) return f->offs[i].off;
  }
  return -1;
}

static char *unescape_str(const char *s) {
  size_t n = strlen(s);
  char *out = (char *)xmalloc(n + 1);
  size_t i = 0;
  size_t j = 0;
  while (i < n) {
    if (s[i] == '\\' && i + 1 < n) {
      i++;
      if (s[i] == 'n') out[j++] = '\n';
      else if (s[i] == 't') out[j++] = '\t';
      else if (s[i] == 'r') out[j++] = '\r';
      else if (s[i] == '0') out[j++] = '\0';
      else out[j++] = s[i];
      i++;
      continue;
    }
    out[j++] = s[i++];
  }
  out[j] = 0;
  return out;
}

static char *read_file(const char *path) {
  FILE *fp = fopen(path, "rb");
  long n;
  char *buf;
  if (!fp) return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  n = ftell(fp);
  if (n < 0) {
    fclose(fp);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }
  buf = (char *)xmalloc((size_t)n + 1);
  if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
    fclose(fp);
    free(buf);
    return NULL;
  }
  fclose(fp);
  buf[n] = 0;
  return buf;
}

static int parse_ir(Program *p, const char *path) {
  char *text = read_file(path);
  char *save = NULL;
  char *line;
  Function *cur = NULL;
  CallStat *calls = NULL;
  int ncalls = 0;
  int ccalls = 0;

  if (!text) {
    fprintf(stderr, "cpu: cannot read %s\n", path);
    return 1;
  }

  line = strtok_r(text, "\n", &save);
  while (line) {
    char *t = trim(line);
    Instr in;
    memset(&in, 0, sizeof(in));
    in.target = -1;
    in.off = -1;

    if (*t == 0 || *t == ';') {
      line = strtok_r(NULL, "\n", &save);
      continue;
    }

    if (!strncmp(t, "func @", 6)) {
      cur = prog_add_fn(p, t + 6);
      line = strtok_r(NULL, "\n", &save);
      continue;
    }
    if (!strcmp(t, "endfunc")) {
      cur = NULL;
      line = strtok_r(NULL, "\n", &save);
      continue;
    }

    if (!cur) {
      line = strtok_r(NULL, "\n", &save);
      continue;
    }

    {
      size_t tl = strlen(t);
      if (tl > 1 && t[0] == '.' && t[tl - 1] == ':') {
        t[tl - 1] = 0;
        fn_add_label(cur, t, cur->ncode);
        line = strtok_r(NULL, "\n", &save);
        continue;
      }
    }

    {
      char *offp = strstr(t, "; off=");
      if (offp) {
        in.off = atoi(offp + 6);
        *offp = 0;
        t = trim(t);
      } else {
        char *semi = strchr(t, ';');
        if (semi) {
          *semi = 0;
          t = trim(t);
        }
      }
    }

    if (!strncmp(t, "push.const ", 11)) {
      in.op = OP_PUSH_CONST;
      in.imm = strtol(t + 11, NULL, 10);
      fn_add_instr(cur, in);
    } else if (!strncmp(t, "push.str ", 9)) {
      char *q1 = strchr(t + 9, '"');
      char *q2 = q1 ? strrchr(q1 + 1, '"') : NULL;
      in.op = OP_PUSH_STR;
      if (!q1 || !q2 || q2 <= q1) {
        in.name = xstrdup("");
      } else {
        *q2 = 0;
        in.name = unescape_str(q1 + 1);
      }
      fn_add_instr(cur, in);
    } else if (!strncmp(t, "load.local @", 12)) {
      in.op = OP_LOAD_LOCAL;
      in.name = xstrdup(t + 12);
      fn_note_off(cur, in.off, in.name);
      fn_add_instr(cur, in);
    } else if (!strncmp(t, "store.local @", 13)) {
      in.op = OP_STORE_LOCAL;
      in.name = xstrdup(t + 13);
      fn_note_off(cur, in.off, in.name);
      fn_add_instr(cur, in);
    } else if (!strncmp(t, "load.global @", 13)) {
      in.op = OP_LOAD_GLOBAL;
      in.name = xstrdup(t + 13);
      fn_add_instr(cur, in);
    } else if (!strncmp(t, "store.global @", 14)) {
      in.op = OP_STORE_GLOBAL;
      in.name = xstrdup(t + 14);
      fn_add_instr(cur, in);
    } else if (!strncmp(t, "addr.local @", 12)) {
      in.op = OP_ADDR_LOCAL;
      in.name = xstrdup(t + 12);
      fn_note_off(cur, in.off, in.name);
      fn_add_instr(cur, in);
    } else if (!strncmp(t, "addr.global @", 13)) {
      in.op = OP_ADDR_GLOBAL;
      in.name = xstrdup(t + 13);
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "load.indirect")) {
      in.op = OP_LOAD_INDIRECT;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "load.indirect1")) {
      in.op = OP_LOAD_INDIRECT1;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "load.indirect2")) {
      in.op = OP_LOAD_INDIRECT2;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "load.indirect4")) {
      in.op = OP_LOAD_INDIRECT4;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "load.indirect8")) {
      in.op = OP_LOAD_INDIRECT8;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "store.indirect")) {
      in.op = OP_STORE_INDIRECT;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "store.indirect1")) {
      in.op = OP_STORE_INDIRECT1;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "store.indirect2")) {
      in.op = OP_STORE_INDIRECT2;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "store.indirect4")) {
      in.op = OP_STORE_INDIRECT4;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "store.indirect8")) {
      in.op = OP_STORE_INDIRECT8;
      fn_add_instr(cur, in);
    } else if (!strncmp(t, "call @", 6)) {
      char *sp = strrchr(t, ' ');
      in.op = OP_CALL;
      if (sp) {
        *sp = 0;
        in.argc = atoi(sp + 1);
      }
      in.name = xstrdup(t + 6);
      stat_note(&calls, &ncalls, &ccalls, in.name, in.argc);
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "drop")) {
      in.op = OP_DROP;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "ret")) {
      in.op = OP_RET;
      fn_add_instr(cur, in);
    } else if (!strncmp(t, "br.false ", 9)) {
      in.op = OP_BR_FALSE;
      in.name = xstrdup(t + 9);
      fn_add_instr(cur, in);
    } else if (!strncmp(t, "br ", 3)) {
      in.op = OP_BR;
      in.name = xstrdup(t + 3);
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "un.not")) {
      in.op = OP_UN_NOT;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "un.neg")) {
      in.op = OP_UN_NEG;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "un.bnot")) {
      in.op = OP_UN_BNOT;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "bin.add")) {
      in.op = OP_BIN_ADD;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "bin.sub")) {
      in.op = OP_BIN_SUB;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "bin.mul")) {
      in.op = OP_BIN_MUL;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "bin.div")) {
      in.op = OP_BIN_DIV;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "cmp.eq")) {
      in.op = OP_CMP_EQ;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "cmp.ne")) {
      in.op = OP_CMP_NE;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "cmp.lt")) {
      in.op = OP_CMP_LT;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "cmp.le")) {
      in.op = OP_CMP_LE;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "cmp.gt")) {
      in.op = OP_CMP_GT;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "cmp.ge")) {
      in.op = OP_CMP_GE;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "bin.land")) {
      in.op = OP_BIN_LAND;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "bin.lor")) {
      in.op = OP_BIN_LOR;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "bin.band")) {
      in.op = OP_BIN_BAND;
      fn_add_instr(cur, in);
    } else if (!strcmp(t, "bin.bor")) {
      in.op = OP_BIN_BOR;
      fn_add_instr(cur, in);
    } else {
      in.op = OP_NOP;
      fn_add_instr(cur, in);
    }

    line = strtok_r(NULL, "\n", &save);
  }

  {
    int i, j;
    for (i = 0; i < p->nfns; i++) {
      Function *f = &p->fns[i];
      for (j = 0; j < f->ncode; j++) {
        Instr *in = &f->code[j];
        if (in->op == OP_BR || in->op == OP_BR_FALSE) {
          int pc = fn_label_pc(f, in->name ? in->name : "");
          if (pc < 0 && in->name && !strncmp(in->name, ".Lcont", 6)) {
            char alt[128];
            snprintf(alt, sizeof(alt), ".Lcond%s", in->name + 6);
            pc = fn_label_pc(f, alt);
          }
          if (pc < 0) {
            fprintf(stderr, "cpu: unresolved label %s in %s\n", in->name ? in->name : "?", f->name);
            free(text);
            return 1;
          }
          in->target = pc;
        }
      }

      if (f->noffs > 1) qsort(f->offs, (size_t)f->noffs, sizeof(OffName), cmp_off);
      if (f->noffs > 0) {
        int max_off = f->offs[f->noffs - 1].off;
        if (max_off < 64) max_off = 64;
        f->frame_size = (max_off + 64 + 15) & ~15;
      } else {
        f->frame_size = 64;
      }
      {
        int want = stat_get(calls, ncalls, f->name);
        if (want < 0) want = 0;
        if (want > f->noffs) want = f->noffs;
        if (want > 0) {
          f->param_names = (char **)xmalloc((size_t)want * sizeof(char *));
          f->nparams = want;
          for (j = 0; j < want; j++) f->param_names[j] = f->offs[j].name;
        }
      }
    }
  }

  free(text);
  return 0;
}

static void fr_push(Frame *fr, i64 v) {
  if (fr->sp == fr->cap) {
    int ncap = fr->cap ? fr->cap * 2 : 256;
    fr->stack = (i64 *)realloc(fr->stack, (size_t)ncap * sizeof(i64));
    if (!fr->stack) {
      fprintf(stderr, "cpu: out of memory\n");
      exit(2);
    }
    fr->cap = ncap;
  }
  fr->stack[fr->sp++] = v;
}

static i64 fr_pop(Frame *fr) {
  if (fr->sp <= 0) return 0;
  return fr->stack[--fr->sp];
}

static i64 call_builtin(const char *name, i64 *args, int argc) {
  if (!strcmp(name, "read") && argc >= 3) return (i64)read((int)args[0], (void *)(intptr_t)args[1], (size_t)args[2]);
  if (!strcmp(name, "write") && argc >= 3) return (i64)write((int)args[0], (const void *)(intptr_t)args[1], (size_t)args[2]);
  if (!strcmp(name, "open") && argc >= 3) return (i64)open((const char *)(intptr_t)args[0], (int)args[1], (int)args[2]);
  if (!strcmp(name, "open") && argc >= 3) return (i64)open((const char *)(intptr_t)args[0], (int)args[1], (mode_t)args[2]);
  if (!strcmp(name, "close") && argc >= 1) return (i64)close((int)args[0]);
  if (!strcmp(name, "mmap") && argc >= 6) return (i64)(intptr_t)mmap((void *)(intptr_t)args[0], (size_t)args[1], (int)args[2], (int)args[3], (int)args[4], (off_t)args[5]);
  if (!strcmp(name, "munmap") && argc >= 2) return (i64)munmap((void *)(intptr_t)args[0], (size_t)args[1]);
  if (!strcmp(name, "malloc") && argc >= 1) return (i64)(intptr_t)malloc((size_t)args[0]);
  if (!strcmp(name, "free") && argc >= 1) {
    free((void *)(intptr_t)args[0]);
    return 0;
  }
  if (!strcmp(name, "calloc") && argc >= 2) return (i64)(intptr_t)calloc((size_t)args[0], (size_t)args[1]);
  if (!strcmp(name, "realloc") && argc >= 2) return (i64)(intptr_t)realloc((void *)(intptr_t)args[0], (size_t)args[1]);
  if (!strcmp(name, "strlen") && argc >= 1) return (i64)strlen((const char *)(intptr_t)args[0]);
  if (!strcmp(name, "strcmp") && argc >= 2) return (i64)strcmp((const char *)(intptr_t)args[0], (const char *)(intptr_t)args[1]);
  if (!strcmp(name, "memcpy") && argc >= 3) return (i64)(intptr_t)memcpy((void *)(intptr_t)args[0], (const void *)(intptr_t)args[1], (size_t)args[2]);
  if (!strcmp(name, "memset") && argc >= 3) return (i64)(intptr_t)memset((void *)(intptr_t)args[0], (int)args[1], (size_t)args[2]);
  if (!strcmp(name, "strchr") && argc >= 2) return (i64)(intptr_t)strchr((const char *)(intptr_t)args[0], (int)args[1]);
  if (!strcmp(name, "strrchr") && argc >= 2) return (i64)(intptr_t)strrchr((const char *)(intptr_t)args[0], (int)args[1]);
  if (!strcmp(name, "isspace") && argc >= 1) return (i64)isspace((int)args[0]);
  if (!strcmp(name, "isdigit") && argc >= 1) return (i64)isdigit((int)args[0]);
  if (!strcmp(name, "isalpha") && argc >= 1) return (i64)isalpha((int)args[0]);
  if (!strcmp(name, "isalnum") && argc >= 1) return (i64)isalnum((int)args[0]);
  if (!strcmp(name, "strtod") && argc >= 1) {
    char *endp = NULL;
    double d = strtod((const char *)(intptr_t)args[0], argc >= 2 ? &endp : NULL);
    if (argc >= 2) {
      i64 *slot = (i64 *)(intptr_t)args[1];
      if (slot) *slot = (i64)(intptr_t)endp;
    }
    return (i64)d;
  }
  if (!strcmp(name, "printf") && argc >= 1) {
    const char *fmt = (const char *)(intptr_t)args[0];
    if (argc == 1) return (i64)printf("%s", fmt);
    if (argc == 2) return (i64)printf(fmt, args[1]);
    if (argc == 3) return (i64)printf(fmt, args[1], args[2]);
    if (argc == 4) return (i64)printf(fmt, args[1], args[2], args[3]);
    if (argc == 5) return (i64)printf(fmt, args[1], args[2], args[3], args[4]);
    return (i64)printf(fmt, args[1], args[2], args[3], args[4], args[5]);
  }
  if (!strcmp(name, "eprintf") && argc >= 1) {
    const char *fmt = (const char *)(intptr_t)args[0];
    if (argc == 1) return (i64)fprintf(stderr, "%s", fmt);
    if (argc == 2) return (i64)fprintf(stderr, fmt, args[1]);
    if (argc == 3) return (i64)fprintf(stderr, fmt, args[1], args[2]);
    if (argc == 4) return (i64)fprintf(stderr, fmt, args[1], args[2], args[3]);
    if (argc == 5) return (i64)fprintf(stderr, fmt, args[1], args[2], args[3], args[4]);
    return (i64)fprintf(stderr, fmt, args[1], args[2], args[3], args[4], args[5]);
  }
  if (!strcmp(name, "exit") && argc >= 1) {
    exit((int)args[0]);
  }
  return (i64)0x7fffffff00000000LL;
}

static i64 run_function(Program *p, Function *fn, i64 *args, int argc) {
  Frame fr;
  memset(&fr, 0, sizeof(fr));
  fr.fn = fn;
  fr.mem_size = fn->frame_size > 0 ? fn->frame_size : 64;
  fr.mem = (unsigned char *)calloc(1, (size_t)fr.mem_size);
  if (!fr.mem) {
    fprintf(stderr, "cpu: out of memory\n");
    exit(2);
  }
  fr.base = fr.mem + fr.mem_size;

  {
    int i;
    int n = fn->nparams < argc ? fn->nparams : argc;
    for (i = 0; i < n; i++) {
      int off = fn_off_by_name(fn, fn->param_names[i]);
      if (off > 0 && off <= fr.mem_size - (int)sizeof(i64)) {
        memcpy(fr.base - off, &args[i], sizeof(i64));
      } else {
        i64 *slot = map_ref(&fr.locals, fn->param_names[i], 1);
        *slot = args[i];
      }
    }
  }

  while (fr.pc >= 0 && fr.pc < fn->ncode) {
    Instr *in = &fn->code[fr.pc++];
    i64 a, b, v;
    i64 *slot;
    i64 call_args[16];
    int i;

    switch (in->op) {
      case OP_NOP:
        break;
      case OP_PUSH_CONST:
        fr_push(&fr, in->imm);
        break;
      case OP_PUSH_STR:
        fr_push(&fr, (i64)(intptr_t)in->name);
        break;
      case OP_LOAD_LOCAL:
        if (in->off > 0 && in->off <= fr.mem_size - (int)sizeof(i64)) {
          i64 lv = 0;
          memcpy(&lv, fr.base - in->off, sizeof(i64));
          fr_push(&fr, lv);
        } else {
          slot = map_ref(&fr.locals, in->name, 1);
          fr_push(&fr, *slot);
        }
        break;
      case OP_STORE_LOCAL:
        v = fr_pop(&fr);
        if (in->off > 0 && in->off <= fr.mem_size - (int)sizeof(i64)) {
          memcpy(fr.base - in->off, &v, sizeof(i64));
        } else {
          slot = map_ref(&fr.locals, in->name, 1);
          *slot = v;
        }
        fr_push(&fr, v);
        break;
      case OP_LOAD_GLOBAL:
        slot = map_ref(&p->globals, in->name, 1);
        fr_push(&fr, *slot);
        break;
      case OP_STORE_GLOBAL:
        v = fr_pop(&fr);
        slot = map_ref(&p->globals, in->name, 1);
        *slot = v;
        fr_push(&fr, v);
        break;
      case OP_ADDR_LOCAL:
        if (in->off > 0 && in->off <= fr.mem_size) {
          fr_push(&fr, (i64)(intptr_t)(fr.base - in->off));
        } else {
          slot = map_ref(&fr.locals, in->name, 1);
          fr_push(&fr, (i64)(intptr_t)slot);
        }
        break;
      case OP_ADDR_GLOBAL:
        slot = map_ref(&p->globals, in->name, 1);
        fr_push(&fr, (i64)(intptr_t)slot);
        break;
      case OP_LOAD_INDIRECT:
      case OP_LOAD_INDIRECT8:
        a = fr_pop(&fr);
        if (!a) fr_push(&fr, 0);
        else fr_push(&fr, *(i64 *)(intptr_t)a);
        break;
      case OP_LOAD_INDIRECT1:
        a = fr_pop(&fr);
        if (!a) fr_push(&fr, 0);
        else fr_push(&fr, (unsigned char)(*(unsigned char *)(intptr_t)a));
        break;
      case OP_LOAD_INDIRECT2:
        a = fr_pop(&fr);
        if (!a) fr_push(&fr, 0);
        else fr_push(&fr, (unsigned short)(*(unsigned short *)(intptr_t)a));
        break;
      case OP_LOAD_INDIRECT4:
        a = fr_pop(&fr);
        if (!a) fr_push(&fr, 0);
        else fr_push(&fr, (unsigned int)(*(unsigned int *)(intptr_t)a));
        break;
      case OP_STORE_INDIRECT:
      case OP_STORE_INDIRECT8:
        v = fr_pop(&fr);
        a = fr_pop(&fr);
        if (a) *(i64 *)(intptr_t)a = v;
        fr_push(&fr, v);
        break;
      case OP_STORE_INDIRECT1:
        v = fr_pop(&fr);
        a = fr_pop(&fr);
        if (a) *(unsigned char *)(intptr_t)a = (unsigned char)v;
        fr_push(&fr, v);
        break;
      case OP_STORE_INDIRECT2:
        v = fr_pop(&fr);
        a = fr_pop(&fr);
        if (a) *(unsigned short *)(intptr_t)a = (unsigned short)v;
        fr_push(&fr, v);
        break;
      case OP_STORE_INDIRECT4:
        v = fr_pop(&fr);
        a = fr_pop(&fr);
        if (a) *(unsigned int *)(intptr_t)a = (unsigned int)v;
        fr_push(&fr, v);
        break;
      case OP_CALL: {
        Function *callee;
        i64 ret;
        int n = in->argc;
        if (n > 16) n = 16;
        for (i = n - 1; i >= 0; i--) call_args[i] = fr_pop(&fr);
        ret = call_builtin(in->name ? in->name : "", call_args, n);
        if (ret != (i64)0x7fffffff00000000LL) {
          fr_push(&fr, ret);
          break;
        }
        callee = prog_find_fn(p, in->name ? in->name : "");
        if (!callee) {
          fprintf(stderr, "cpu: unknown function %s\n", in->name ? in->name : "?");
          fr_push(&fr, 0);
          break;
        }
        ret = run_function(p, callee, call_args, n);
        fr_push(&fr, ret);
        break;
      }
      case OP_DROP:
        (void)fr_pop(&fr);
        break;
      case OP_RET:
        return fr_pop(&fr);
      case OP_BR:
        fr.pc = in->target;
        break;
      case OP_BR_FALSE:
        v = fr_pop(&fr);
        if (!v) fr.pc = in->target;
        break;
      case OP_UN_NOT:
        v = fr_pop(&fr);
        fr_push(&fr, !v);
        break;
      case OP_UN_NEG:
        v = fr_pop(&fr);
        fr_push(&fr, -v);
        break;
      case OP_UN_BNOT:
        v = fr_pop(&fr);
        fr_push(&fr, ~v);
        break;
      case OP_BIN_ADD:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, a + b);
        break;
      case OP_BIN_SUB:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, a - b);
        break;
      case OP_BIN_MUL:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, a * b);
        break;
      case OP_BIN_DIV:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, b ? a / b : 0);
        break;
      case OP_CMP_EQ:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, a == b);
        break;
      case OP_CMP_NE:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, a != b);
        break;
      case OP_CMP_LT:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, a < b);
        break;
      case OP_CMP_LE:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, a <= b);
        break;
      case OP_CMP_GT:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, a > b);
        break;
      case OP_CMP_GE:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, a >= b);
        break;
      case OP_BIN_LAND:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, (!!a) && (!!b));
        break;
      case OP_BIN_LOR:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, (!!a) || (!!b));
        break;
      case OP_BIN_BAND:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, a & b);
        break;
      case OP_BIN_BOR:
        b = fr_pop(&fr); a = fr_pop(&fr); fr_push(&fr, a | b);
        break;
      default:
        break;
    }
  }

  return fr.sp > 0 ? fr_pop(&fr) : 0;
}

int main(int argc, char **argv) {
  Program p;
  Function *main_fn;
  i64 args[2] = {0, 0};
  int rc;

  memset(&p, 0, sizeof(p));

  if (argc < 2) {
    fprintf(stderr, "Usage: %s input.llvm [program-args...]\n", argv[0]);
    return 2;
  }

  if (parse_ir(&p, argv[1]) != 0) return 1;

  main_fn = prog_find_fn(&p, "main");
  if (!main_fn) {
    fprintf(stderr, "cpu: main not found\n");
    return 1;
  }

  if (main_fn->nparams >= 2 && !strcmp(main_fn->param_names[0], "argc") && !strcmp(main_fn->param_names[1], "argv")) {
    args[0] = (i64)(argc - 2);
    args[1] = (i64)(intptr_t)&argv[2];
    rc = (int)run_function(&p, main_fn, args, 2);
  } else {
    rc = (int)run_function(&p, main_fn, NULL, 0);
  }

  return rc & 255;
}
