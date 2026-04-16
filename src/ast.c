/*
 * Tiny ANSI C Compiler in ANSI C
 * Features: Lexer, Parser (Recursive Descent), AST, CodeGen (x86_64), ELF Header
 */

#include "libc.c"
#include "config.h"

// --- Preprocessor ---
typedef struct Macro {
  char *name;
  char *body;
  struct Macro *next;
} Macro;

Macro *macros = NULL;
char *src_stack[CC_CFG_PP_SRC_STACK_MAX];
int src_ptr = 0;

void add_macro(char *name, char *body) {
  Macro *m = malloc(sizeof(Macro));
  m->name = name; m->body = body; m->next = macros; macros = m;
}

char *find_macro(char *name) {
  for (Macro *m = macros; m; m = m->next) if (!strcmp(m->name, name)) return m->body;
  return NULL;
}

// --- Lexer ---
enum {
  TK_NUM = 256, TK_FLOAT_LIT, TK_ID, TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_SWITCH, TK_CASE, TK_DEFAULT, TK_BREAK, TK_CONTINUE, TK_RETURN, 
  TK_INT, TK_VOID, TK_CHAR, TK_FLOAT,
  TK_EQ, TK_NE, TK_LE, TK_GE, TK_LOGIC_AND, TK_LOGIC_OR, TK_INC, TK_DEC, TK_ARROW, TK_STR, TK_SIZEOF,
  TK_ADD_ASSIGN, TK_SUB_ASSIGN, TK_MUL_ASSIGN, TK_DIV_ASSIGN, TK_AND_ASSIGN,
  TK_EOF
};

typedef struct {
  int type;
  long val;
  char *name;
} Token;

char *src;
char *src_base;
Token token;
int parse_verbose = 0;

char *tok_name(int t) {
  switch (t) {
    case TK_NUM: return (char *)"TK_NUM";
    case TK_FLOAT_LIT: return (char *)"TK_FLOAT_LIT";
    case TK_ID: return (char *)"TK_ID";
    case TK_IF: return (char *)"TK_IF";
    case TK_ELSE: return (char *)"TK_ELSE";
    case TK_WHILE: return (char *)"TK_WHILE";
    case TK_FOR: return (char *)"TK_FOR";
    case TK_SWITCH: return (char *)"TK_SWITCH";
    case TK_CASE: return (char *)"TK_CASE";
    case TK_DEFAULT: return (char *)"TK_DEFAULT";
    case TK_BREAK: return (char *)"TK_BREAK";
    case TK_CONTINUE: return (char *)"TK_CONTINUE";
    case TK_RETURN: return (char *)"TK_RETURN";
    case TK_INT: return (char *)"TK_INT";
    case TK_VOID: return (char *)"TK_VOID";
    case TK_CHAR: return (char *)"TK_CHAR";
    case TK_FLOAT: return (char *)"TK_FLOAT";
    case TK_EQ: return (char *)"TK_EQ";
    case TK_NE: return (char *)"TK_NE";
    case TK_LE: return (char *)"TK_LE";
    case TK_GE: return (char *)"TK_GE";
    case TK_LOGIC_AND: return (char *)"TK_LOGIC_AND";
    case TK_LOGIC_OR: return (char *)"TK_LOGIC_OR";
    case TK_INC: return (char *)"TK_INC";
    case TK_DEC: return (char *)"TK_DEC";
    case TK_ARROW: return (char *)"TK_ARROW";
    case TK_STR: return (char *)"TK_STR";
    case TK_SIZEOF: return (char *)"TK_SIZEOF";
    case TK_ADD_ASSIGN: return (char *)"TK_ADD_ASSIGN";
    case TK_SUB_ASSIGN: return (char *)"TK_SUB_ASSIGN";
    case TK_MUL_ASSIGN: return (char *)"TK_MUL_ASSIGN";
    case TK_DIV_ASSIGN: return (char *)"TK_DIV_ASSIGN";
    case TK_AND_ASSIGN: return (char *)"TK_AND_ASSIGN";
    case TK_EOF: return (char *)"TK_EOF";
    default: break;
  }
  return (char *)"TK_CHAR_OP";
}

void trace_token() {
  return;
  if (token.type == TK_ID || token.type == TK_STR) {
    eprintf("[v] tok=%s(%d) off=%ld name=\"%s\"\n", (long)tok_name(token.type), token.type,
            src_base ? (long)(src - src_base) : -1, (long)(token.name ? token.name : ""));
    return;
  }
  if (token.type == TK_NUM || token.type == TK_FLOAT_LIT) {
    eprintf("[v] tok=%s(%d) off=%ld val=%ld\n", (long)tok_name(token.type), token.type,
            src_base ? (long)(src - src_base) : -1, token.val);
    return;
  }
  if (token.type < 256) {
    eprintf("[v] tok='%c'(%d) off=%ld\n", token.type, token.type, src_base ? (long)(src - src_base) : -1, 0);
    return;
  }
  eprintf("[v] tok=%s(%d) off=%ld\n", (long)tok_name(token.type), token.type, src_base ? (long)(src - src_base) : -1, 0);
}

int lookup_named_constant(const char *name, int *out) {
  if (!strcmp(name, "TK_NUM")) { *out = TK_NUM; return 1; }
  if (!strcmp(name, "TK_FLOAT_LIT")) { *out = TK_FLOAT_LIT; return 1; }
  if (!strcmp(name, "TK_ID")) { *out = TK_ID; return 1; }
  if (!strcmp(name, "TK_IF")) { *out = TK_IF; return 1; }
  if (!strcmp(name, "TK_ELSE")) { *out = TK_ELSE; return 1; }
  if (!strcmp(name, "TK_WHILE")) { *out = TK_WHILE; return 1; }
  if (!strcmp(name, "TK_FOR")) { *out = TK_FOR; return 1; }
  if (!strcmp(name, "TK_SWITCH")) { *out = TK_SWITCH; return 1; }
  if (!strcmp(name, "TK_CASE")) { *out = TK_CASE; return 1; }
  if (!strcmp(name, "TK_DEFAULT")) { *out = TK_DEFAULT; return 1; }
  if (!strcmp(name, "TK_BREAK")) { *out = TK_BREAK; return 1; }
  if (!strcmp(name, "TK_CONTINUE")) { *out = TK_CONTINUE; return 1; }
  if (!strcmp(name, "TK_RETURN")) { *out = TK_RETURN; return 1; }
  if (!strcmp(name, "TK_INT")) { *out = TK_INT; return 1; }
  if (!strcmp(name, "TK_VOID")) { *out = TK_VOID; return 1; }
  if (!strcmp(name, "TK_CHAR")) { *out = TK_CHAR; return 1; }
  if (!strcmp(name, "TK_FLOAT")) { *out = TK_FLOAT; return 1; }
  if (!strcmp(name, "TK_EQ")) { *out = TK_EQ; return 1; }
  if (!strcmp(name, "TK_NE")) { *out = TK_NE; return 1; }
  if (!strcmp(name, "TK_LE")) { *out = TK_LE; return 1; }
  if (!strcmp(name, "TK_GE")) { *out = TK_GE; return 1; }
  if (!strcmp(name, "TK_LOGIC_AND")) { *out = TK_LOGIC_AND; return 1; }
  if (!strcmp(name, "TK_LOGIC_OR")) { *out = TK_LOGIC_OR; return 1; }
  if (!strcmp(name, "TK_INC")) { *out = TK_INC; return 1; }
  if (!strcmp(name, "TK_DEC")) { *out = TK_DEC; return 1; }
  if (!strcmp(name, "TK_ARROW")) { *out = TK_ARROW; return 1; }
  if (!strcmp(name, "TK_STR")) { *out = TK_STR; return 1; }
  if (!strcmp(name, "TK_SIZEOF")) { *out = TK_SIZEOF; return 1; }
  if (!strcmp(name, "TK_ADD_ASSIGN")) { *out = TK_ADD_ASSIGN; return 1; }
  if (!strcmp(name, "TK_SUB_ASSIGN")) { *out = TK_SUB_ASSIGN; return 1; }
  if (!strcmp(name, "TK_MUL_ASSIGN")) { *out = TK_MUL_ASSIGN; return 1; }
  if (!strcmp(name, "TK_DIV_ASSIGN")) { *out = TK_DIV_ASSIGN; return 1; }
  if (!strcmp(name, "TK_AND_ASSIGN")) { *out = TK_AND_ASSIGN; return 1; }
  if (!strcmp(name, "TK_EOF")) { *out = TK_EOF; return 1; }

  if (!strcmp(name, "ND_NUM")) { *out = 0; return 1; }
  if (!strcmp(name, "ND_ID")) { *out = 1; return 1; }
  if (!strcmp(name, "ND_ADD")) { *out = 2; return 1; }
  if (!strcmp(name, "ND_SUB")) { *out = 3; return 1; }
  if (!strcmp(name, "ND_MUL")) { *out = 4; return 1; }
  if (!strcmp(name, "ND_DIV")) { *out = 5; return 1; }
  if (!strcmp(name, "ND_EQ")) { *out = 6; return 1; }
  if (!strcmp(name, "ND_NE")) { *out = 7; return 1; }
  if (!strcmp(name, "ND_LT")) { *out = 8; return 1; }
  if (!strcmp(name, "ND_LE")) { *out = 9; return 1; }
  if (!strcmp(name, "ND_GT")) { *out = 10; return 1; }
  if (!strcmp(name, "ND_GE")) { *out = 11; return 1; }
  if (!strcmp(name, "ND_AND")) { *out = 12; return 1; }
  if (!strcmp(name, "ND_OR")) { *out = 13; return 1; }
  if (!strcmp(name, "ND_BITAND")) { *out = 14; return 1; }
  if (!strcmp(name, "ND_ASSIGN")) { *out = 15; return 1; }
  if (!strcmp(name, "ND_IF")) { *out = 16; return 1; }
  if (!strcmp(name, "ND_WHILE")) { *out = 17; return 1; }
  if (!strcmp(name, "ND_FOR")) { *out = 18; return 1; }
  if (!strcmp(name, "ND_SWITCH")) { *out = 19; return 1; }
  if (!strcmp(name, "ND_CASE")) { *out = 20; return 1; }
  if (!strcmp(name, "ND_DEFAULT")) { *out = 21; return 1; }
  if (!strcmp(name, "ND_BREAK")) { *out = 22; return 1; }
  if (!strcmp(name, "ND_CONTINUE")) { *out = 23; return 1; }
  if (!strcmp(name, "ND_RETURN")) { *out = 24; return 1; }
  if (!strcmp(name, "ND_BLOCK")) { *out = 25; return 1; }
  if (!strcmp(name, "ND_FUNC")) { *out = 26; return 1; }
  if (!strcmp(name, "ND_CALL")) { *out = 27; return 1; }
  if (!strcmp(name, "ND_VAR")) { *out = 28; return 1; }
  if (!strcmp(name, "ND_ADDR")) { *out = 29; return 1; }
  if (!strcmp(name, "ND_DEREF")) { *out = 30; return 1; }
  if (!strcmp(name, "ND_NOT")) { *out = 31; return 1; }
  if (!strcmp(name, "ND_NEG")) { *out = 32; return 1; }
  if (!strcmp(name, "ND_TERNARY")) { *out = 33; return 1; }
  if (!strcmp(name, "ND_PRE_INC")) { *out = 34; return 1; }
  if (!strcmp(name, "ND_PRE_DEC")) { *out = 35; return 1; }
  if (!strcmp(name, "ND_POST_INC")) { *out = 36; return 1; }
  if (!strcmp(name, "ND_POST_DEC")) { *out = 37; return 1; }
  if (!strcmp(name, "ND_BNOT")) { *out = 38; return 1; }
  if (!strcmp(name, "ND_COMMA")) { *out = 39; return 1; }
  if (!strcmp(name, "ND_BITOR")) { *out = 40; return 1; }
  return 0;
}

void push_src_state(char *resume_src) {
  if (src_ptr >= CC_CFG_PP_SRC_STACK_MAX) {
    eprintf("Error: preprocessor source stack overflow\n", 0, 0, 0, 0);
    exit(1);
  }
  src_stack[src_ptr++] = resume_src;
}

char *read_source_file_for_include(const char *path) {
  int fd = openat(AT_FDCWD, path, O_RDONLY, 0);
  char *fallback = NULL;
  char *fallback_src = NULL;
  size_t cap = CC_CFG_IO_BUFFER_INIT;
  size_t len = 0;
  char *buf;
  if (fd < 0) {
    size_t n = strlen(path);
    fallback = malloc(n + 9);
    if (!fallback) return NULL;
    memcpy(fallback, "include/", 8);
    memcpy(fallback + 8, path, n + 1);
    fd = openat(AT_FDCWD, fallback, O_RDONLY, 0);
    if (fd < 0) {
      fallback_src = malloc(n + 5);
      if (!fallback_src) {
        free(fallback);
        return NULL;
      }
      memcpy(fallback_src, "src/", 4);
      memcpy(fallback_src + 4, path, n + 1);
      fd = openat(AT_FDCWD, fallback_src, O_RDONLY, 0);
      if (fd < 0) {
        free(fallback);
        free(fallback_src);
        return NULL;
      }
    }
  }
  buf = malloc(cap + 1);
  if (!buf) {
    close(fd);
    if (fallback) free(fallback);
    if (fallback_src) free(fallback_src);
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
        if (fallback) free(fallback);
        if (fallback_src) free(fallback_src);
        return NULL;
      }
      buf = nb;
    }
    n = read(fd, buf + len, cap - len);
    if (n < 0) {
      free(buf);
      close(fd);
      if (fallback) free(fallback);
      if (fallback_src) free(fallback_src);
      return NULL;
    }
    if (n == 0) break;
    len += (size_t)n;
  }
  close(fd);
  if (fallback) free(fallback);
  if (fallback_src) free(fallback_src);
  buf[len] = 0;
  return buf;
}

typedef struct GlobalInfoDef {
  char *name;
  int offset;
  char *type_name;
  int ptr_level;
  int is_array;
  int bytes;
  struct GlobalInfoDef *next;
} GlobalInfoDef;

GlobalInfoDef *global_info_defs = NULL;
int global_info_initialized = 0;

void register_global_info(char *name, int offset, char *type_name, int ptr_level, int is_array, int bytes) {
  GlobalInfoDef *g = calloc(1, sizeof(GlobalInfoDef));
  g->name = name;
  g->offset = offset;
  g->type_name = type_name;
  g->ptr_level = ptr_level;
  g->is_array = is_array;
  g->bytes = bytes;
  g->next = global_info_defs;
  global_info_defs = g;
}

void ensure_global_info_registry() {
  if (global_info_initialized) return;
  global_info_initialized = 1;
  register_global_info((char *)"macros", 0, (char *)"Macro", 1, 0, 8);
  register_global_info((char *)"src_stack", 32, (char *)"char", 2, 1, 512);
  register_global_info((char *)"src_ptr", 544, (char *)"int", 0, 0, 8);
  register_global_info((char *)"src", 552, (char *)"char", 1, 0, 8);
  register_global_info((char *)"src_base", 560, (char *)"char", 1, 0, 8);
  register_global_info((char *)"token", 576, (char *)"Token", 0, 0, 24);
  register_global_info((char *)"parse_verbose", 600, (char *)"int", 0, 0, 8);
  register_global_info((char *)"global_info_defs", 608, (char *)"GlobalInfoDef", 1, 0, 8);
  register_global_info((char *)"global_info_initialized", 616, (char *)"int", 0, 0, 8);
  register_global_info((char *)"locals", 624, (char *)"Symbol", 1, 0, 8);
  register_global_info((char *)"local_stack_size", 632, (char *)"int", 0, 0, 8);
  register_global_info((char *)"user_types", 640, (char *)"UserTypeDef", 1, 0, 8);
  register_global_info((char *)"typedef_aliases", 648, (char *)"TypedefAlias", 1, 0, 8);
  register_global_info((char *)"anon_type_counter", 656, (char *)"int", 0, 0, 8);
}

int lookup_global_info(const char *name, int *offset, const char **type_name, int *ptr_level, int *is_array, int *bytes) {
  if (!name) return 0;
  ensure_global_info_registry();
  for (GlobalInfoDef *g = global_info_defs; g; g = g->next) {
    if (strcmp(g->name, name)) continue;
    if (offset) *offset = g->offset;
    if (type_name) *type_name = g->type_name;
    if (ptr_level) *ptr_level = g->ptr_level;
    if (is_array) *is_array = g->is_array;
    if (bytes) *bytes = g->bytes;
    return 1;
  }
  return 0;
}

int global_storage_size() {
  int max_end = 0;
  ensure_global_info_registry();
  for (GlobalInfoDef *g = global_info_defs; g; g = g->next) {
    int end = g->offset + g->bytes;
    if (end > max_end) max_end = end;
  }
  return max_end;
}

void next() {
  for (;;) {
    while (isspace(*src)) src++;
    if (src[0] == '/' && src[1] == '/') {
      src += 2;
      while (*src && *src != '\n') src++;
      continue;
    }
    if (src[0] == '/' && src[1] == '*') {
      src += 2;
      while (src[0] && !(src[0] == '*' && src[1] == '/')) src++;
      if (src[0]) src += 2;
      continue;
    }
    break;
  }
  if (!*src) {
    if (src_ptr > 0) { src = src_stack[--src_ptr]; next(); return; }
    token.type = TK_EOF;
    trace_token();
    return;
  }

  if (*src == '#') {
    src++;
    while (*src == ' ' || *src == '\t') src++;
    char buf[32]; int i = 0;
    while (isalpha(*src) && i < 31) buf[i++] = *src++;
    buf[i] = 0;
    if (!strcmp(buf, "define")) {
      while (isspace(*src)) src++;
      char *n_start = src; while (isalnum(*src) || *src == '_') src++;
      int n_len = src - n_start; char *name = malloc(n_len + 1);
      memcpy(name, n_start, n_len); name[n_len] = 0;
      while (isspace(*src)) src++;
      char *b_start = src; while (*src && *src != '\n') src++;
      int b_len = src - b_start; char *body = malloc(b_len + 1);
      memcpy(body, b_start, b_len); body[b_len] = 0;
      add_macro(name, body);
      next(); return;
    }
    if (!strcmp(buf, "include")) {
      char *path = NULL;
      char *inc = NULL;
      while (*src == ' ' || *src == '\t') src++;
      if (*src == '"' || *src == '<') {
        char term = (*src == '"') ? '"' : '>';
        char *start;
        int len;
        src++;
        start = src;
        while (*src && *src != term && *src != '\n') src++;
        len = src - start;
        path = malloc(len + 1);
        if (!path) {
          eprintf("Error: out of memory in #include\n", 0, 0, 0, 0);
          exit(1);
        }
        memcpy(path, start, len);
        path[len] = 0;
        if (*src == term) src++;
      }
      if (!path || !path[0]) {
        eprintf("Error: malformed #include directive\n", 0, 0, 0, 0);
        exit(1);
      }
      inc = read_source_file_for_include(path);
      if (!inc) {
        eprintf("Error: cannot open include file: %s\n", (long)path, 0, 0, 0);
        exit(1);
      }
      while (*src && *src != '\n') src++;
      if (*src == '\n') src++;
      push_src_state(src);
      src = inc;
      next();
      return;
    }
    while (*src && *src != '\n') src++;
    if (*src == '\n') src++;
    next();
    return;
  }

  if (isdigit(*src)) {
    if (src[0] == '0' && (src[1] == 'x' || src[1] == 'X')) {
      long v = 0;
      src += 2;
      while (1) {
        int c = *src;
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
        if (d < 0) break;
        v = v * 16 + d;
        src++;
      }
      token.type = TK_NUM;
      token.val = v;
      trace_token();
      return;
    }
    long v = 0;
    int is_float = 0;
    while (isdigit(*src)) {
      v = v * 10 + (*src - '0');
      src++;
    }
    if (*src == '.') {
      is_float = 1;
      src++;
      while (isdigit(*src)) src++;
    }
    token.val = v;
    token.type = is_float ? TK_FLOAT_LIT : TK_NUM;
    trace_token();
    return;
  }

  if (*src == '\'') {
    int v = 0;
    src++;
    if (*src == '\\') {
      src++;
      if (*src == 'n') { v = '\n'; src++; }
      else if (*src == 't') { v = '\t'; src++; }
      else if (*src == 'r') { v = '\r'; src++; }
      else if (*src == 'f') { v = '\f'; src++; }
      else if (*src == 'v') { v = '\v'; src++; }
      else if (*src == '0') { v = '\0'; src++; }
      else if (*src) { v = (unsigned char)*src; src++; }
    } else if (*src) {
      v = (unsigned char)*src;
      src++;
    }
    if (*src == '\'') src++;
    token.type = TK_NUM;
    token.val = v;
    trace_token();
    return;
  }

  if (isalpha(*src) || *src == '_') {
    char *start = src;
    while (isalnum(*src) || *src == '_') src++;
    int len = src - start;
    char *name = malloc(len + 1);
    memcpy(name, start, len);
    name[len] = 0;
    token.name = name;

    if (!strcmp(name, "if")) token.type = TK_IF;
    else if (!strcmp(name, "else")) token.type = TK_ELSE;
    else if (!strcmp(name, "while")) token.type = TK_WHILE;
    else if (!strcmp(name, "for")) token.type = TK_FOR;
    else if (!strcmp(name, "switch")) token.type = TK_SWITCH;
    else if (!strcmp(name, "case")) token.type = TK_CASE;
    else if (!strcmp(name, "default")) token.type = TK_DEFAULT;
    else if (!strcmp(name, "break")) token.type = TK_BREAK;
    else if (!strcmp(name, "continue")) token.type = TK_CONTINUE;
    else if (!strcmp(name, "return")) token.type = TK_RETURN;
    else if (!strcmp(name, "sizeof")) token.type = TK_SIZEOF;
    else if (!strcmp(name, "NULL")) { token.type = TK_NUM; token.val = 0; }
    else if (!strcmp(name, "SEEK_SET")) { token.type = TK_NUM; token.val = 0; }
    else if (!strcmp(name, "SEEK_END")) { token.type = TK_NUM; token.val = 2; }
    else if (!strcmp(name, "INT32_MIN")) { token.type = TK_NUM; token.val = -2147483647 - 1; }
    else if (!strcmp(name, "INT32_MAX")) { token.type = TK_NUM; token.val = 2147483647; }
    else if (!strcmp(name, "int")) token.type = TK_INT;
    else if (!strcmp(name, "char")) token.type = TK_CHAR;
    else if (!strcmp(name, "float")) token.type = TK_FLOAT;
    else if (!strcmp(name, "void")) token.type = TK_VOID;
    else {
      int cval = 0;
      if (lookup_named_constant(name, &cval)) {
        token.type = TK_NUM;
        token.val = cval;
        trace_token();
        return;
      }
      char *body = find_macro(name);
      if (body) {
        push_src_state(src);
        src = body;
        next();
        return;
      }
      token.type = TK_ID;
    }
    trace_token();
    return;
  }

  if (*src == '"') {
    src++;
    char *start = src;
    while (*src && *src != '"') {
      if (*src == '\\' && src[1]) src++;
      src++;
    }
    int len = src - start;
    char *s = malloc(len + 1);
    memcpy(s, start, len);
    s[len] = 0;
    if (*src == '"') src++;
    token.type = TK_STR;
    token.name = s;
    trace_token();
    return;
  }

  if (*src == '&' && src[1] == '&') { src += 2; token.type = TK_LOGIC_AND; trace_token(); return; }
  if (*src == '|' && src[1] == '|') { src += 2; token.type = TK_LOGIC_OR; trace_token(); return; }
  if (*src == '+' && src[1] == '=') { src += 2; token.type = TK_ADD_ASSIGN; trace_token(); return; }
  if (*src == '-' && src[1] == '=') { src += 2; token.type = TK_SUB_ASSIGN; trace_token(); return; }
  if (*src == '*' && src[1] == '=') { src += 2; token.type = TK_MUL_ASSIGN; trace_token(); return; }
  if (*src == '/' && src[1] == '=') { src += 2; token.type = TK_DIV_ASSIGN; trace_token(); return; }
  if (*src == '&' && src[1] == '=') { src += 2; token.type = TK_AND_ASSIGN; trace_token(); return; }
  if (*src == '+' && src[1] == '+') { src += 2; token.type = TK_INC; trace_token(); return; }
  if (*src == '-' && src[1] == '-') { src += 2; token.type = TK_DEC; trace_token(); return; }
  if (*src == '-' && src[1] == '>') { src += 2; token.type = TK_ARROW; trace_token(); return; }
  if (*src == '=' && src[1] == '=') { src += 2; token.type = TK_EQ; trace_token(); return; }
  if (*src == '!' && src[1] == '=') { src += 2; token.type = TK_NE; trace_token(); return; }
  if (*src == '<' && src[1] == '=') { src += 2; token.type = TK_LE; trace_token(); return; }
  if (*src == '>' && src[1] == '=') { src += 2; token.type = TK_GE; trace_token(); return; }

  token.type = *src++;
  trace_token();
}

void expect(int type) {
  if (token.type != type) {
    long off = src_base ? (long)(src - src_base) : -1;
    const char *ctx = src;
    eprintf("Error: Expected %d, got %d at offset %ld near \"%.40s\"\n", type, token.type, off, (long)(ctx ? ctx : ""));
    exit(1);
  }
  next();
}

// --- Symbol Table ---
typedef struct Symbol {
  char *name;
  int offset;
  int size;
  char *type_name;
  int ptr_level;
  int is_array;
  struct Symbol *next;
} Symbol;

Symbol *locals = NULL;
int local_stack_size = 0;

Symbol *find_local(char *name) {
  for (Symbol *s = locals; s; s = s->next) if (!strcmp(s->name, name)) return s;
  return NULL;
}

int type_size_from_name(const char *s);

Symbol *add_local_t(char *name, char *type_name, int ptr_level) {
  Symbol *s = malloc(sizeof(Symbol));
  int sz = (ptr_level > 0) ? 8 : type_size_from_name(type_name);
  if (sz < 1) sz = 8;
  sz = (sz + 7) & ~7;
  s->name = name;
  local_stack_size += sz;
  s->offset = local_stack_size;
  s->size = sz;
  s->type_name = type_name ? type_name : (char *)"int";
  s->ptr_level = ptr_level;
  s->is_array = 0;
  s->next = locals;
  locals = s;
  return s;
}

Symbol *add_local_array_t(char *name, char *type_name, int elem_ptr_level, int arr_len) {
  Symbol *s = malloc(sizeof(Symbol));
  int elem_sz = (elem_ptr_level > 0) ? 8 : type_size_from_name(type_name);
  int sz;
  if (arr_len < 1) arr_len = 1;
  if (elem_sz < 1) elem_sz = 8;
  sz = elem_sz * arr_len;
  sz = (sz + 7) & ~7;
  s->name = name;
  local_stack_size += sz;
  s->offset = local_stack_size;
  s->size = sz;
  s->type_name = type_name ? type_name : (char *)"int";
  s->ptr_level = elem_ptr_level + 1;
  s->is_array = 1;
  s->next = locals;
  locals = s;
  return s;
}

Symbol *add_local(char *name) {
  return add_local_t(name, (char *)"int", 0);
}

// --- AST ---
typedef enum {
  ND_NUM, ND_ID, ND_ADD, ND_SUB, ND_MUL, ND_DIV, ND_EQ, ND_NE, ND_LT, ND_LE, 
  ND_GT, ND_GE, ND_AND, ND_OR, ND_BITAND, ND_ASSIGN, ND_IF, ND_WHILE, ND_FOR, ND_SWITCH, ND_CASE, ND_DEFAULT, ND_BREAK, ND_CONTINUE, ND_RETURN, 
  ND_BLOCK, ND_FUNC, ND_CALL, ND_VAR, ND_ADDR, ND_DEREF, ND_NOT, ND_NEG, ND_TERNARY, ND_PRE_INC, ND_PRE_DEC, ND_POST_INC, ND_POST_DEC, ND_BNOT, ND_COMMA,
  ND_BITOR
} NodeKind;

typedef struct Node {
  NodeKind kind;
  struct Node *lhs, *rhs, *cond, *then, *els, *body, *init, *inc, *next, *args;
  int val;
  char *name;
  Symbol *sym;
  char *type_name;
  int ptr_level;
} Node;

Node *new_node(NodeKind kind) {
  long *raw = calloc(1, 120);
  if (!raw) return NULL;
  raw[0] = kind;
  return (Node *)raw;
}

Node *clone_node_shallow(Node *src_node) {
  if (!src_node) return NULL;
  Node *dst = calloc(1, 120);
  if (!dst) return NULL;
  memcpy(dst, src_node, 120);
  return dst;
}

// --- Parser ---
Node *expr();
Node *stmt();
Node *switch_stmt();
Node *assign();
int is_known_type_name(const char *s);
Node *bit_or();

int is_type_token(int t) {
  return t == TK_INT || t == TK_CHAR || t == TK_FLOAT || t == TK_VOID;
}

int is_type_qualifier_name(const char *s) {
  if (!s) return 0;
  return !strcmp(s, "const") || !strcmp(s, "volatile") ||
         !strcmp(s, "signed") || !strcmp(s, "unsigned") ||
         !strcmp(s, "long") || !strcmp(s, "short") ||
         !strcmp(s, "static") || !strcmp(s, "extern") ||
         !strcmp(s, "typedef") || !strcmp(s, "struct") ||
         !strcmp(s, "enum");
}

typedef struct TypeFieldDef {
  char *name;
  int offset;
  char *type_name;
  int ptr_level;
  int is_array;
  struct TypeFieldDef *next;
} TypeFieldDef;

typedef struct UserTypeDef {
  char *name;
  int size;
  int is_union;
  TypeFieldDef *fields;
  struct UserTypeDef *next;
} UserTypeDef;

typedef struct TypedefAlias {
  char *name;
  char *base_name;
  int ptr_level;
  struct TypedefAlias *next;
} TypedefAlias;

UserTypeDef *user_types = NULL;
TypedefAlias *typedef_aliases = NULL;
int anon_type_counter = 0;

char *dup_cstr(const char *s) {
  size_t n;
  char *p;
  if (!s) return NULL;
  n = strlen(s);
  p = malloc(n + 1);
  if (!p) return NULL;
  memcpy(p, s, n + 1);
  return p;
}

UserTypeDef *find_user_type(const char *name) {
  for (UserTypeDef *t = user_types; t; t = t->next) {
    if (!strcmp(t->name, name)) return t;
  }
  return NULL;
}

TypedefAlias *find_typedef_alias(const char *name) {
  for (TypedefAlias *a = typedef_aliases; a; a = a->next) {
    if (!strcmp(a->name, name)) return a;
  }
  return NULL;
}

void register_user_type(const char *name, int size, int is_union, TypeFieldDef *fields) {
  UserTypeDef *t = find_user_type(name);
  if (!t) {
    t = calloc(1, sizeof(UserTypeDef));
    t->name = dup_cstr(name);
    t->next = user_types;
    user_types = t;
  }
  t->size = size;
  t->is_union = is_union;
  t->fields = fields;
}

void register_typedef_alias(const char *name, const char *base_name, int ptr_level) {
  TypedefAlias *a = find_typedef_alias(name);
  if (!a) {
    a = calloc(1, sizeof(TypedefAlias));
    a->name = dup_cstr(name);
    a->next = typedef_aliases;
    typedef_aliases = a;
  }
  a->base_name = dup_cstr(base_name);
  a->ptr_level = ptr_level;
  if (parse_verbose) eprintf("[v] typedef alias %s -> %s (ptr=%d)\n", (long)name, (long)base_name, ptr_level, 0);
}

int resolve_typedef_alias(const char *name, const char **base_name, int *ptr_level) {
  int extra = 0;
  const char *cur = name;
  int depth = 0;
  while (depth < 16) {
    TypedefAlias *a = find_typedef_alias(cur);
    if (!a) break;
    extra += a->ptr_level;
    cur = a->base_name;
    depth++;
  }
  if (!cur || !strcmp(cur, name)) return 0;
  if (base_name) *base_name = cur;
  if (ptr_level) *ptr_level = extra;
  return 1;
}

int parse_type_base_for_decl(char **base_type_out) {
  int saw = 0;
  char *base = (char *)"int";
  while (1) {
    if (is_type_token(token.type)) {
      saw = 1;
      if (token.type == TK_CHAR) base = (char *)"char";
      else if (token.type == TK_INT) base = (char *)"int";
      else if (token.type == TK_FLOAT) base = (char *)"float";
      else if (token.type == TK_VOID) base = (char *)"void";
      next();
      continue;
    }
    if (token.type == TK_ID && token.name) {
      if (!strcmp(token.name, "long")) {
        saw = 1;
        base = (char *)"long";
        next();
        continue;
      }
      if (!strcmp(token.name, "short")) {
        saw = 1;
        base = (char *)"short";
        next();
        continue;
      }
      if (!strcmp(token.name, "struct") || !strcmp(token.name, "union") || !strcmp(token.name, "enum")) {
        saw = 1;
        next();
        if (token.type == TK_ID) {
          base = token.name;
          next();
        }
        continue;
      }
      if (is_type_qualifier_name(token.name)) {
        saw = 1;
        next();
        continue;
      }
      if (is_known_type_name(token.name)) {
        saw = 1;
        base = token.name;
        next();
        continue;
      }
    }
    break;
  }
  if (base_type_out) *base_type_out = base;
  return saw;
}

int parse_record_fields(int is_union, TypeFieldDef **out_fields, int *out_size) {
  int cur_off = 0;
  int max_align = 1;
  int max_size = 0;
  TypeFieldDef head = {0};
  TypeFieldDef *tail = &head;

  while (token.type != TK_EOF && token.type != '}') {
    char *base_type = (char *)"int";
    int saw_type = parse_type_base_for_decl(&base_type);
    if (!saw_type) {
      next();
      continue;
    }
    while (token.type != TK_EOF) {
      int pptr = 0;
      int arr_len = 1;
      int arr_dims = 0;
      char *fname = NULL;
      while (token.type == '*') { pptr++; next(); }
      if (token.type == TK_ID) {
        fname = token.name;
        next();
      }
      while (token.type == '[') {
        int n = 1;
        arr_dims++;
        next();
        if (token.type == TK_NUM || token.type == TK_FLOAT_LIT) {
          n = (int)token.val;
          next();
        } else if (token.type != ']') {
          (void)expr();
        }
        expect(']');
        if (n < 1) n = 1;
        arr_len *= n;
      }
      if (fname) {
        int elem_sz = (pptr > 0) ? 8 : type_size_from_name(base_type);
        int fsz = elem_sz;
        int align = elem_sz > 8 ? 8 : elem_sz;
        int fptr = pptr + (arr_dims > 0 ? 1 : 0);
        TypeFieldDef *f = calloc(1, sizeof(TypeFieldDef));
        if (arr_dims > 0) fsz = elem_sz * arr_len;
        if (align < 1) align = 1;
        if (!is_union) cur_off = (cur_off + align - 1) & ~(align - 1);
        f->name = fname;
        f->offset = is_union ? 0 : cur_off;
        f->type_name = base_type;
        f->ptr_level = fptr;
        f->is_array = arr_dims > 0;
        tail->next = f;
        tail = f;
        if (!is_union) cur_off += fsz;
        if (fsz > max_size) max_size = fsz;
        if (align > max_align) max_align = align;
      }
      if (token.type == ',') { next(); continue; }
      break;
    }
    if (token.type == ';') next();
    else while (token.type != TK_EOF && token.type != ';' && token.type != '}') next();
  }
  if (out_fields) *out_fields = head.next;
  if (out_size) {
    int sz = is_union ? max_size : cur_off;
    if (sz < 1) sz = 1;
    sz = (sz + max_align - 1) & ~(max_align - 1);
    *out_size = sz;
  }
  return 1;
}

char *make_anon_type_name() {
  char buf[48];
  int n = anon_type_counter++;
  int i = 0;
  char *out;
  char tmp[20];
  int ti = 0;
  const char *pfx = "__anon_type_";
  while (pfx[i]) { buf[i] = pfx[i]; i++; }
  if (n == 0) tmp[ti++] = '0';
  else {
    while (n > 0 && ti < 19) {
      int q = n / 10;
      int rem = n - q * 10;
      tmp[ti++] = (char)('0' + rem);
      n = q;
    }
    {
      int j = 0;
      while (j < ti / 2) {
        char t = tmp[j];
        tmp[j] = tmp[ti - j - 1];
        tmp[ti - j - 1] = t;
        j++;
      }
    }
  }
  {
    int j = 0;
    while (j < ti) {
      buf[i++] = tmp[j];
      j++;
    }
  }
  buf[i] = 0;
  out = malloc(i + 1);
  memcpy(out, buf, i + 1);
  return out;
}

int try_parse_typedef_or_record_decl() {
  char *saved_src = src;
  Token saved_tok = token;
  int is_typedef = 0;
  int is_union = 0;
  int is_enum = 0;
  char *tag_name = NULL;
  char *base_name = NULL;
  int saw_record = 0;

  if (token.type == TK_ID && token.name && !strcmp(token.name, "typedef")) {
    is_typedef = 1;
    next();
  }
  if (token.type == TK_ID && token.name &&
      (!strcmp(token.name, "struct") || !strcmp(token.name, "union") || !strcmp(token.name, "enum"))) {
    is_enum = !strcmp(token.name, "enum");
    is_union = !strcmp(token.name, "union");
    saw_record = 1;
    next();
    if (token.type == TK_ID) {
      tag_name = token.name;
      next();
    }
    if (token.type == '{') {
      TypeFieldDef *fields = NULL;
      int rsz = 0;
      next();
      if (is_enum) {
        while (token.type != TK_EOF && token.type != '}') next();
        rsz = 4;
      } else {
        parse_record_fields(is_union, &fields, &rsz);
      }
      expect('}');
      if (!is_enum) {
        if (!tag_name) tag_name = make_anon_type_name();
        register_user_type(tag_name, rsz, is_union, fields);
      }
    } else if (!is_typedef) {
      src = saved_src;
      token = saved_tok;
      return 0;
    }
    if (is_enum) base_name = (char *)"int";
    else base_name = tag_name;
  } else if (!is_typedef) {
    src = saved_src;
    token = saved_tok;
    return 0;
  }

  if (!base_name) {
    char *tbase = NULL;
    if (!parse_type_base_for_decl(&tbase)) {
      src = saved_src;
      token = saved_tok;
      return 0;
    }
    base_name = tbase;
  }

  if (is_typedef) {
    while (token.type != TK_EOF) {
      int pptr = 0;
      while (token.type == '*') { pptr++; next(); }
      if (token.type != TK_ID) break;
      char *alias = token.name;
      next();
      while (token.type == '[') {
        next();
        if (token.type != ']') {
          if (token.type == TK_NUM || token.type == TK_FLOAT_LIT) next();
          else (void)expr();
        }
        expect(']');
        pptr++;
      }
      register_typedef_alias(alias, base_name, pptr);
      if (token.type == ',') { next(); continue; }
      break;
    }
    if (token.type == ';') next();
    return 1;
  }

  while (token.type != TK_EOF && token.type != ';') next();
  if (token.type == ';') next();
  return saw_record;
}

int is_known_type_name(const char *s) {
  if (!s) return 0;
  if (!strcmp(s, "double")) return 1;
  if (find_typedef_alias(s)) return 1;
  if (find_user_type(s)) return 1;
  return 0;
}

int type_size_from_name(const char *s) {
  if (!s) return 8;
  if (!strcmp(s, "char")) return 1;
  if (!strcmp(s, "unsigned char")) return 1;
  if (!strcmp(s, "short")) return 2;
  if (!strcmp(s, "int")) return 4;
  if (!strcmp(s, "float")) return 4;
  if (!strcmp(s, "double")) return 8;
  if (!strcmp(s, "long")) return 8;
  if (!strcmp(s, "void")) return 8;
  {
    const char *base = NULL;
    int ptr = 0;
    if (resolve_typedef_alias(s, &base, &ptr)) {
      if (ptr > 0) return 8;
      return type_size_from_name(base);
    }
  }
  {
    UserTypeDef *t = find_user_type(s);
    if (t) return t->size > 0 ? t->size : 8;
  }
  return 8;
}

typedef struct {
  const char *stype;
  const char *field;
  int offset;
  const char *ftype;
  int fptr;
} FieldInfo;

int lookup_struct_field(const char *stype, const char *field, int *offset, const char **ftype, int *fptr) {
  const char *base = stype;
  int ptr = 0;
  UserTypeDef *t;
  if (!stype || !field) return 0;
  if (resolve_typedef_alias(stype, &base, &ptr) && ptr == 0) stype = base;
  t = find_user_type(stype);
  if (!t) return 0;
  for (TypeFieldDef *f = t->fields; f; f = f->next) {
    if (!strcmp(f->name, field)) {
      if (offset) *offset = f->offset;
      if (ftype) *ftype = f->type_name;
      if (fptr) *fptr = f->is_array ? -(f->ptr_level + 1) : f->ptr_level;
      return 1;
    }
  }
  return 0;
}

int pointee_size(const char *type_name, int ptr_level) {
  if (ptr_level <= 0) return type_size_from_name(type_name);
  if (ptr_level == 1) return type_size_from_name(type_name);
  return 8;
}

int is_byte_type_name(const char *t) {
  if (!t) return 0;
  return type_size_from_name(t) == 1;
}

int parse_type_name_size() {
  int size = 0;
  int saw_type = 0;
  int progress = 1;

  while (progress) {
    progress = 0;
    if (is_type_token(token.type)) {
      saw_type = 1;
      if (token.type == TK_CHAR) size = 1;
      else if (token.type == TK_INT) size = 4;
      else if (token.type == TK_FLOAT) size = 4;
      else if (token.type == TK_VOID) size = 8;
      next();
      progress = 1;
      continue;
    }

    if (token.type == TK_ID && token.name) {
      if (!strcmp(token.name, "struct") || !strcmp(token.name, "union") || !strcmp(token.name, "enum")) {
        next();
        if (token.type == TK_ID && token.name) {
          saw_type = 1;
          size = type_size_from_name(token.name);
          next();
        }
        progress = 1;
        continue;
      }
      if (!strcmp(token.name, "unsigned") || !strcmp(token.name, "signed") ||
          !strcmp(token.name, "const") || !strcmp(token.name, "volatile") ||
          !strcmp(token.name, "static") || !strcmp(token.name, "extern") ||
          !strcmp(token.name, "typedef")) {
        saw_type = 1;
        next();
        progress = 1;
        continue;
      }
      if (!strcmp(token.name, "long")) {
        saw_type = 1;
        size = 8;
        next();
        progress = 1;
        continue;
      }
      if (!strcmp(token.name, "short")) {
        saw_type = 1;
        size = 2;
        next();
        progress = 1;
        continue;
      }
      if (is_known_type_name(token.name)) {
        saw_type = 1;
        size = type_size_from_name(token.name);
        next();
        progress = 1;
        continue;
      }
    }
  }

  if (!saw_type) return -1;
  if (size == 0) size = 4;

  while (token.type == '*') {
    size = 8;
    next();
  }
  while (token.type == '[') {
    int n = 1;
    next();
    if (token.type == TK_NUM || token.type == TK_FLOAT_LIT) {
      n = (int)token.val;
      next();
    }
    expect(']');
    if (n < 0) n = 0;
    size *= n;
  }
  return size;
}

int is_decl_start_token() {
  if (is_type_token(token.type)) return 1;
  if (token.type == TK_ID && token.name) {
    if (is_type_qualifier_name(token.name)) return 1;
    if (is_known_type_name(token.name)) return 1;
  }
  return 0;
}

void skip_initializer_brace() {
  int depth = 0;
  if (token.type != '{') return;
  while (token.type != TK_EOF) {
    if (token.type == '{') depth++;
    else if (token.type == '}') {
      depth--;
      next();
      if (depth <= 0) return;
      continue;
    }
    next();
  }
}

Node *parse_decl_stmt(int consume_semi) {
  Node *blk = new_node(ND_BLOCK);
  Node head = {0};
  Node *cur = &head;
  int saw_type = 0;
  char *base_type = (char *)"int";
  int base_ptr = 0;

  while (is_decl_start_token()) {
    if (is_type_token(token.type)) {
      saw_type = 1;
      if (token.type == TK_CHAR) base_type = (char *)"char";
      else if (token.type == TK_INT) base_type = (char *)"int";
      else if (token.type == TK_FLOAT) base_type = (char *)"float";
      else if (token.type == TK_VOID) base_type = (char *)"void";
      next();
      continue;
    }
    if (token.type == TK_ID && token.name) {
      if (!strcmp(token.name, "struct") || !strcmp(token.name, "enum")) {
        saw_type = 1;
        next();
        if (token.type == TK_ID) {
          base_type = token.name;
          next();
        }
        continue;
      }
      if (!strcmp(token.name, "long")) {
        saw_type = 1;
        base_type = (char *)"long";
        next();
        continue;
      }
      if (!strcmp(token.name, "short")) {
        saw_type = 1;
        base_type = (char *)"short";
        next();
        continue;
      }
      if (!strcmp(token.name, "unsigned") || !strcmp(token.name, "signed") ||
          !strcmp(token.name, "const") || !strcmp(token.name, "volatile") ||
          !strcmp(token.name, "static") || !strcmp(token.name, "extern") ||
          !strcmp(token.name, "typedef")) {
        saw_type = 1;
        next();
        continue;
      }
      if (is_known_type_name(token.name)) {
        saw_type = 1;
        base_type = token.name;
        next();
        continue;
      }
    }
    break;
  }

  if (!saw_type) return blk;

  while (token.type != TK_EOF) {
    int local_ptr = 0;
    int is_array = 0;
    int arr_len = 1;
    char *vname;
    while (token.type == '*') { local_ptr++; next(); }
    if (token.type != TK_ID) break;
    vname = token.name;
    next();

    Node *var = new_node(ND_VAR);
    var->name = vname;

    while (token.type == '[') {
      int n = 1;
      is_array = 1;
      next();
      if (token.type != ']') {
        if (token.type == TK_NUM || token.type == TK_FLOAT_LIT) {
          n = (int)token.val;
          next();
        } else if (token.type == TK_ID) {
          next();
        } else {
          (void)expr();
        }
      }
      expect(']');
      if (n < 1) n = 1;
      arr_len *= n;
    }

    if (is_array) var->sym = add_local_array_t(var->name, base_type, base_ptr + local_ptr, arr_len);
    else var->sym = add_local_t(var->name, base_type, base_ptr + local_ptr);
    var->type_name = var->sym->type_name;
    var->ptr_level = var->sym->ptr_level;

    if (token.type == '=') {
      next();
      if (token.type == '{') skip_initializer_brace();
      else var->lhs = expr();
    }

    cur->next = var;
    cur = cur->next;

    if (token.type == ',') {
      next();
      continue;
    }
    break;
  }

  if (consume_semi) {
    if (token.type == ';') next();
    else {
      while (token.type != TK_EOF && token.type != ';') next();
      if (token.type == ';') next();
    }
  }

  blk->body = head.next;
  return blk;
}

Node *primary() {
  if (token.type == '(') { next(); Node *node = expr(); expect(')'); return node; }
  if (token.type == TK_NUM || token.type == TK_FLOAT_LIT) {
    Node *node = new_node(ND_NUM);
    node->val = (int)token.val;
    node->type_name = (char *)"int";
    node->ptr_level = 0;
    next();
    return node;
  }
  if (token.type == TK_ID) {
    Node *node = new_node(ND_ID);
    node->name = token.name;
    node->sym = find_local(node->name);
    if (node->sym) {
      node->type_name = node->sym->type_name;
      node->ptr_level = node->sym->ptr_level;
    } else {
      const char *gtype = "int";
      int gptr = 0;
      if (lookup_global_info(node->name, NULL, &gtype, &gptr, NULL, NULL)) {
        node->type_name = (char *)gtype;
        node->ptr_level = gptr;
      } else {
        node->type_name = (char *)"int";
        node->ptr_level = 0;
      }
    }
    next();
    if (token.type == '(') {
      next();
      node->kind = ND_CALL;
      node->type_name = (char *)"int";
      node->ptr_level = 0;
      Node head = {0}; Node *cur = &head;
      while (token.type != ')') {
        cur->next = assign(); cur = cur->next;
        if (token.type == ',') next();
      }
      expect(')');
      node->args = head.next;
    }
    return node;
  }
  if (token.type == TK_STR) {
    Node *node = new_node(ND_NUM);
    node->val = 0;
    node->name = token.name;
    node->type_name = (char *)"char";
    node->ptr_level = 1;
    next();
    return node;
  }
  long off = src_base ? (long)(src - src_base) : -1;
  const char *ctx = src;
  eprintf("Error: unsupported token in primary(): %d at offset %ld near \"%.40s\"\n", token.type, off, (long)(ctx ? ctx : ""), 0);
  exit(1);
}

Node *postfix() {
  Node *node = primary();
  for (;;) {
    if (token.type == '[') {
      Node *idx;
      Node *scaled;
      Node *addn;
      Node *deref;
      int esz;
      next();
      idx = expr();
      esz = pointee_size(node->type_name, node->ptr_level);
      scaled = idx;
      if (esz > 1) {
        Node *muln = new_node(ND_MUL);
        Node *cn = new_node(ND_NUM);
        cn->val = esz;
        cn->type_name = (char *)"int";
        cn->ptr_level = 0;
        muln->lhs = idx;
        muln->rhs = cn;
        muln->type_name = (char *)"int";
        muln->ptr_level = 0;
        scaled = muln;
      }
      addn = new_node(ND_ADD);
      addn->lhs = node;
      addn->rhs = scaled;
      addn->type_name = node->type_name;
      addn->ptr_level = node->ptr_level;
      expect(']');
      deref = new_node(ND_DEREF);
      deref->lhs = addn;
      deref->type_name = node->type_name;
      deref->ptr_level = node->ptr_level > 0 ? node->ptr_level - 1 : 0;
      node = deref;
      continue;
    }
    if (token.type == '.' || token.type == TK_ARROW) {
      int is_arrow = (token.type == TK_ARROW);
      int off = 0;
      const char *ftype = "int";
      int fptr = 0;
      int field_is_array = 0;
      const char *stype = node->type_name ? node->type_name : "int";
      Node *base_ptr = NULL;
      Node *addn = NULL;
      Node *offn = NULL;
      Node *deref = NULL;
      next();
      if (token.type == TK_ID) {
        if (!is_arrow && node->ptr_level == 0) {
          if (node->kind == ND_DEREF) {
            base_ptr = node->lhs;
          } else {
            Node *addr = new_node(ND_ADDR);
            addr->lhs = node;
            addr->type_name = node->type_name;
            addr->ptr_level = node->ptr_level + 1;
            base_ptr = addr;
          }
        } else {
          base_ptr = node;
        }
        if (lookup_struct_field(stype, token.name, &off, &ftype, &fptr)) {
          if (fptr < 0) {
            field_is_array = 1;
            fptr = -fptr - 1;
          }
          offn = new_node(ND_NUM);
          offn->val = off;
          offn->type_name = (char *)"int";
          offn->ptr_level = 0;

          addn = new_node(ND_ADD);
          addn->lhs = base_ptr;
          addn->rhs = offn;
          addn->type_name = base_ptr->type_name;
          addn->ptr_level = base_ptr->ptr_level;

          if (field_is_array) {
            addn->type_name = (char *)ftype;
            addn->ptr_level = fptr;
            node = addn;
          } else {
            deref = new_node(ND_DEREF);
            deref->lhs = addn;
            deref->type_name = (char *)ftype;
            deref->ptr_level = fptr;
            node = deref;
          }
        }
        next();
      }
      continue;
    }
    if (token.type == TK_INC) {
      Node *n = new_node(ND_POST_INC);
      next();
      n->lhs = node;
      n->type_name = node->type_name;
      n->ptr_level = node->ptr_level;
      node = n;
      continue;
    }
    if (token.type == TK_DEC) {
      Node *n = new_node(ND_POST_DEC);
      next();
      n->lhs = node;
      n->type_name = node->type_name;
      n->ptr_level = node->ptr_level;
      node = n;
      continue;
    }
    return node;
  }
}

Node *unary() {
  if (token.type == TK_INC) { next(); Node *node = new_node(ND_PRE_INC); node->lhs = unary(); node->type_name = node->lhs->type_name; node->ptr_level = node->lhs->ptr_level; return node; }
  if (token.type == TK_DEC) { next(); Node *node = new_node(ND_PRE_DEC); node->lhs = unary(); node->type_name = node->lhs->type_name; node->ptr_level = node->lhs->ptr_level; return node; }
  if (token.type == '+') { next(); return unary(); }
  if (token.type == '-') { next(); Node *node = new_node(ND_NEG); node->lhs = unary(); node->type_name = (char *)"int"; node->ptr_level = 0; return node; }
  if (token.type == '~') { next(); Node *node = new_node(ND_BNOT); node->lhs = unary(); node->type_name = (char *)"int"; node->ptr_level = 0; return node; }
  if (token.type == '&') { next(); Node *node = new_node(ND_ADDR); node->lhs = unary(); node->type_name = node->lhs->type_name; node->ptr_level = node->lhs->ptr_level + 1; return node; }
  if (token.type == '*') { next(); Node *node = new_node(ND_DEREF); node->lhs = unary(); node->type_name = node->lhs->type_name; node->ptr_level = node->lhs->ptr_level > 0 ? node->lhs->ptr_level - 1 : 0; return node; }
  if (token.type == '!') { next(); Node *node = new_node(ND_NOT); node->lhs = unary(); node->type_name = (char *)"int"; node->ptr_level = 0; return node; }
  if (token.type == TK_SIZEOF) {
    Node *node = new_node(ND_NUM);
    int sz = 8;
    next();
    if (token.type == '(') {
      char *saved_src = src;
      Token saved_tok = token;
      int tsz;
      next();
      tsz = parse_type_name_size();
      if (tsz >= 0 && token.type == ')') {
        sz = tsz;
        next();
      } else {
        src = saved_src;
        token = saved_tok;
        expect('(');
        (void)expr();
        expect(')');
      }
    } else {
      (void)unary();
    }
    node->val = sz;
    node->type_name = (char *)"int";
    node->ptr_level = 0;
    return node;
  }
  if (token.type == '(') {
    char *saved_src = src;
    Token saved_tok = token;
    Node *casted = NULL;
    int tsz;
    next();
    tsz = parse_type_name_size();
    if (tsz >= 0 && token.type == ')') {
      next();
      casted = unary();
      return casted;
    }
    src = saved_src;
    token = saved_tok;
  }
  return postfix();
}
Node *mul() {
  Node *node = unary();
  for (;;) {
    if (token.type == '*') { next(); Node *n = new_node(ND_MUL); n->lhs = node; n->rhs = unary(); n->type_name = (char *)"int"; n->ptr_level = 0; node = n; }
    else if (token.type == '/') { next(); Node *n = new_node(ND_DIV); n->lhs = node; n->rhs = unary(); n->type_name = (char *)"int"; n->ptr_level = 0; node = n; }
    else return node;
  }
}

Node *add() {
  Node *node = mul();
  for (;;) {
    if (token.type == '+' || token.type == '-') {
      int is_add = (token.type == '+');
      Node *rhs;
      Node *n;
      next();
      rhs = mul();
      if (node->ptr_level > 0 && rhs->ptr_level == 0) {
        int esz = pointee_size(node->type_name, node->ptr_level);
        if (esz > 1) {
          Node *m = new_node(ND_MUL);
          Node *cn = new_node(ND_NUM);
          cn->val = esz;
          cn->type_name = (char *)"int";
          cn->ptr_level = 0;
          m->lhs = rhs;
          m->rhs = cn;
          m->type_name = (char *)"int";
          m->ptr_level = 0;
          rhs = m;
        }
      }
      if (rhs->ptr_level > 0 && node->ptr_level == 0 && is_add) {
        int esz = pointee_size(rhs->type_name, rhs->ptr_level);
        if (esz > 1) {
          Node *m = new_node(ND_MUL);
          Node *cn = new_node(ND_NUM);
          cn->val = esz;
          cn->type_name = (char *)"int";
          cn->ptr_level = 0;
          m->lhs = node;
          m->rhs = cn;
          m->type_name = (char *)"int";
          m->ptr_level = 0;
          node = m;
        }
      }
      n = new_node(is_add ? ND_ADD : ND_SUB);
      n->lhs = node;
      n->rhs = rhs;
      if (node->ptr_level > 0 && rhs->ptr_level == 0) {
        n->type_name = node->type_name;
        n->ptr_level = node->ptr_level;
      } else if (rhs->ptr_level > 0 && node->ptr_level == 0 && is_add) {
        n->type_name = rhs->type_name;
        n->ptr_level = rhs->ptr_level;
      } else {
        n->type_name = (char *)"int";
        n->ptr_level = 0;
      }
      node = n;
    }
    else return node;
  }
}

Node *rel() {
  Node *node = add();
  for (;;) {
    if (token.type == '<') { next(); Node *n = new_node(ND_LT); n->lhs = node; n->rhs = add(); n->type_name = (char *)"int"; n->ptr_level = 0; node = n; }
    else if (token.type == '>') { next(); Node *n = new_node(ND_GT); n->lhs = node; n->rhs = add(); n->type_name = (char *)"int"; n->ptr_level = 0; node = n; }
    else if (token.type == TK_LE) { next(); Node *n = new_node(ND_LE); n->lhs = node; n->rhs = add(); n->type_name = (char *)"int"; n->ptr_level = 0; node = n; }
    else if (token.type == TK_GE) { next(); Node *n = new_node(ND_GE); n->lhs = node; n->rhs = add(); n->type_name = (char *)"int"; n->ptr_level = 0; node = n; }
    else return node;
  }
}

Node *equality() {
  Node *node = rel();
  for (;;) {
    if (token.type == TK_EQ) { next(); Node *n = new_node(ND_EQ); n->lhs = node; n->rhs = rel(); n->type_name = (char *)"int"; n->ptr_level = 0; node = n; }
    else if (token.type == TK_NE) { next(); Node *n = new_node(ND_NE); n->lhs = node; n->rhs = rel(); n->type_name = (char *)"int"; n->ptr_level = 0; node = n; }
    else return node;
  }
}

Node *bit_and() {
  Node *node = equality();
  while (token.type == '&') {
    Node *n = new_node(ND_BITAND);
    next();
    n->lhs = node;
    n->rhs = equality();
    n->type_name = (char *)"int";
    n->ptr_level = 0;
    node = n;
  }
  return node;
}

Node *bit_or() {
  Node *node = bit_and();
  while (token.type == '|') {
    Node *n = new_node(ND_BITOR);
    next();
    n->lhs = node;
    n->rhs = bit_and();
    n->type_name = (char *)"int";
    n->ptr_level = 0;
    node = n;
  }
  return node;
}

Node *log_and() {
  Node *node = bit_or();
  while (token.type == TK_LOGIC_AND) { next(); Node *n = new_node(ND_AND); n->lhs = node; n->rhs = bit_or(); n->type_name = (char *)"int"; n->ptr_level = 0; node = n; }
  return node;
}

Node *log_or() {
  Node *node = log_and();
  while (token.type == TK_LOGIC_OR) { next(); Node *n = new_node(ND_OR); n->lhs = node; n->rhs = log_and(); n->type_name = (char *)"int"; n->ptr_level = 0; node = n; }
  return node;
}

Node *conditional() {
  Node *node = log_or();
  if (token.type == '?') {
    Node *n = new_node(ND_TERNARY);
    next();
    n->cond = node;
    n->then = expr();
    expect(':');
    n->els = conditional();
    n->type_name = n->then ? n->then->type_name : (char *)"int";
    n->ptr_level = n->then ? n->then->ptr_level : 0;
    return n;
  }
  return node;
}

Node *assign() {
  Node *node = conditional();
  if (token.type == '=') {
    Node *n = new_node(ND_ASSIGN);
    next();
    n->lhs = node;
    n->rhs = assign();
    n->type_name = node->type_name;
    n->ptr_level = node->ptr_level;
    node = n;
  } else if (token.type == TK_ADD_ASSIGN || token.type == TK_SUB_ASSIGN ||
             token.type == TK_MUL_ASSIGN || token.type == TK_DIV_ASSIGN ||
             token.type == TK_AND_ASSIGN) {
    int op = ND_ADD;
    Node *n = new_node(ND_ASSIGN);
    Node *lhs_copy;
    Node *rhs_node;
    if (token.type == TK_SUB_ASSIGN) op = ND_SUB;
    else if (token.type == TK_MUL_ASSIGN) op = ND_MUL;
    else if (token.type == TK_DIV_ASSIGN) op = ND_DIV;
    else if (token.type == TK_AND_ASSIGN) op = ND_BITAND;
    next();
    rhs_node = assign();
    lhs_copy = clone_node_shallow(node);
    n->lhs = node;
    n->rhs = new_node(op);
    n->rhs->lhs = lhs_copy;
    n->rhs->rhs = rhs_node;
    n->rhs->type_name = node->type_name;
    n->rhs->ptr_level = node->ptr_level;
    n->type_name = node->type_name;
    n->ptr_level = node->ptr_level;
    node = n;
  }
  return node;
}

Node *expr() {
  Node *node = assign();
  while (token.type == ',') {
    Node *n = new_node(ND_COMMA);
    next();
    n->lhs = node;
    n->rhs = assign();
    n->type_name = n->rhs ? n->rhs->type_name : (char *)"int";
    n->ptr_level = n->rhs ? n->rhs->ptr_level : 0;
    node = n;
  }
  return node;
}

Node *stmt() {
  if (token.type == ';') { next(); return new_node(ND_BLOCK); }
  if (try_parse_typedef_or_record_decl()) return new_node(ND_BLOCK);
  if (token.type == TK_IF) {
    next(); expect('('); Node *node = new_node(ND_IF); node->cond = expr(); expect(')');
    node->then = stmt();
    if (token.type == TK_ELSE) { next(); node->els = stmt(); }
    return node;
  }
  if (token.type == TK_WHILE) {
    next(); expect('('); Node *node = new_node(ND_WHILE); node->cond = expr(); expect(')');
    node->body = stmt();
    return node;
  }
  if (token.type == TK_FOR) {
    next(); expect('('); Node *node = new_node(ND_FOR);
    if (token.type != ';') {
      if (is_decl_start_token()) node->init = parse_decl_stmt(1);
      else { node->init = expr(); expect(';'); }
    } else expect(';');
    if (token.type != ';') node->cond = expr(); expect(';');
    if (token.type != ')') node->inc = expr(); expect(')');
    node->body = stmt(); return node;
  }
  if (token.type == TK_SWITCH) return switch_stmt();
  if (token.type == TK_BREAK) { next(); Node *node = new_node(ND_BREAK); expect(';'); return node; }
  if (token.type == TK_CONTINUE) { next(); Node *node = new_node(ND_CONTINUE); expect(';'); return node; }
  if (token.type == TK_RETURN) {
    next();
    Node *node = new_node(ND_RETURN);
    if (token.type != ';') node->lhs = expr();
    expect(';');
    return node;
  }
  if (token.type == '{') {
    next(); Node *node = new_node(ND_BLOCK); Node head = {0}; Node *cur = &head;
    while (token.type != '}') { cur->next = stmt(); cur = cur->next; }
    next(); node->body = head.next; return node;
  }
  if (is_decl_start_token()) return parse_decl_stmt(1);
  Node *node = expr(); expect(';'); return node;
}

Node *switch_stmt() {
  next();
  expect('(');
  Node *node = new_node(ND_SWITCH);
  node->cond = expr();
  expect(')');
  expect('{');

  Node head = {0};
  Node *cur_case = NULL;
  Node *cur_tail = NULL;

  while (token.type != '}') {
    if (token.type == TK_CASE || token.type == TK_DEFAULT) {
      Node *label = new_node(token.type == TK_CASE ? ND_CASE : ND_DEFAULT);
      if (token.type == TK_CASE) {
        next();
        if (token.type != TK_NUM && token.type != TK_FLOAT_LIT) {
          eprintf("Error: Expected case value\n", 0, 0, 0, 0);
          exit(1);
        }
        label->val = (int)token.val;
        next();
      } else {
        next();
      }
      expect(':');
      if (!head.next) head.next = label;
      else cur_case->next = label;
      cur_case = label;
      cur_tail = NULL;
      continue;
    }

    if (!cur_case) {
      Node *label = new_node(ND_DEFAULT);
      if (!head.next) head.next = label;
      else cur_case->next = label;
      cur_case = label;
      cur_tail = NULL;
    }

    Node *s = stmt();
    if (!cur_case->body) cur_case->body = s;
    else cur_tail->next = s;
    cur_tail = s;
  }

  expect('}');
  node->body = head.next;
  return node;
}

int scan_block_simple(char *start, char **end) {
  int depth = 0;
  int in_string = 0;
  int in_char = 0;
  int in_line_comment = 0;
  int in_block_comment = 0;

  for (char *p = start; *p; p++) {
    if (in_line_comment) {
      if (*p == '\n') in_line_comment = 0;
      continue;
    }
    if (in_block_comment) {
      if (*p == '*' && p[1] == '/') { in_block_comment = 0; p++; }
      continue;
    }
    if (in_string) {
      if (*p == '\\' && p[1]) { p++; continue; }
      if (*p == '"') in_string = 0;
      continue;
    }
    if (in_char) {
      if (*p == '\\' && p[1]) { p++; continue; }
      if (*p == '\'') in_char = 0;
      continue;
    }

    if (*p == '/' && p[1] == '/') { in_line_comment = 1; p++; continue; }
    if (*p == '/' && p[1] == '*') { in_block_comment = 1; p++; continue; }
    if (*p == '"') { in_string = 1; continue; }
    if (*p == '\'') { in_char = 1; continue; }

    if (*p == '{') depth++;
    if (*p == '}') {
      depth--;
      if (depth == 0) {
        *end = p + 1;
        return 1;
      }
    }
  }

  *end = start;
  return 0;
}

Node *function() {
  locals = NULL; // Reset locals for each function
  local_stack_size = 0;

  // Skip non-function top-level tokens.
  if (!is_decl_start_token()) {
    next();
    return NULL;
  }

  // Return type/declaration specifiers.
  {
    char *ret_base = NULL;
    if (!parse_type_base_for_decl(&ret_base)) {
      next();
      return NULL;
    }
  }
  while (token.type == '*') next(); // pointer return type

  if (token.type != TK_ID) {
    while (token.type != TK_EOF && token.type != ';') next();
    if (token.type == ';') next();
    return NULL;
  }

  Node *node = new_node(ND_FUNC);
  node->name = token.name;
  if (parse_verbose) eprintf("[v] parse function %s at off=%ld\n", (long)node->name, src_base ? (long)(src - src_base) : -1, 0, 0);
  next();

  // Not a function declaration/definition.
  if (token.type != '(') {
    while (token.type != TK_EOF && token.type != ';') next();
    if (token.type == ';') next();
    return NULL;
  }

  // Parameter scan for prototypes/definitions.
  next();
  Node phead = {0};
  Node *pcur = &phead;
  while (token.type != TK_EOF && token.type != ')') {
    if (token.type == '.') {
      int dots = 0;
      while (token.type == '.' && dots < 3) { next(); dots++; } // varargs
      while (token.type != TK_EOF && token.type != ')') next();
      break;
    }

    char *pname = NULL;
    char *ptype = (char *)"int";
    int pptr = 0;
    int depth = 0;

    (void)parse_type_base_for_decl(&ptype);
    while (token.type == '*') { pptr++; next(); }
    if (token.type == TK_ID) {
      pname = token.name;
      next();
    }
    while (token.type == '[') {
      pptr++; // array param decays to pointer
      next();
      if (token.type != ']') {
        if (token.type == TK_NUM || token.type == TK_FLOAT_LIT) next();
        else (void)expr();
      }
      expect(']');
    }

    // Skip residual tokens in this parameter chunk (e.g. function pointers).
    while (token.type != TK_EOF) {
      if (token.type == '(') { depth++; next(); continue; }
      if (token.type == ')') {
        if (depth == 0) break;
        depth--;
        next();
        continue;
      }
      if (depth == 0 && token.type == ',') break;
      next();
    }

    if (pname) {
      Symbol *ps = add_local_t(pname, ptype, pptr);
      Node *pn = new_node(ND_ID);
      pn->name = pname;
      pn->sym = ps;
      pn->type_name = ptype;
      pn->ptr_level = pptr;
      pcur->next = pn;
      pcur = pn;
    }
    if (token.type == ',') next();
  }
  node->args = phead.next;
  expect(')');

  // Function prototype.
  if (token.type == ';') {
    next();
    return NULL;
  }

  // Skip unsupported function body syntax, but keep top-level scan alive.
  if (token.type != '{') return NULL;

  char *block_end = NULL;
  if (!scan_block_simple(src - 1, &block_end)) {
    src = block_end;
    next();
    node->body = NULL;
    node->val = local_stack_size;
    return node;
  }

  node->body = stmt();
  node->val = local_stack_size;
  return node;
}

char *read_stdin_source() {
  size_t cap = CC_CFG_IO_BUFFER_INIT;
  size_t len = 0;
  char *buf = malloc(cap + 1);
  if (!buf) return NULL;
  while (1) {
    long n;
    if (len == cap) {
      cap *= 2;
      buf = realloc(buf, cap + 1);
      if (!buf) return NULL;
    }
    n = read(0, buf + len, cap - len);
    if (n < 0) { free(buf); return NULL; }
    if (n == 0) break;
    len += (size_t)n;
  }
  buf[len] = 0;
  return buf;
}

Node *parse_program_functions(char *source) {
  src = source;
  src_base = source;
  src_ptr = 0;
  macros = NULL;
  next();

  Node head = {0};
  Node *cur = &head;
  while (token.type != TK_EOF) {
    if (try_parse_typedef_or_record_decl()) continue;
    Node *fn = function();
    if (fn) {
      cur->next = fn;
      cur = cur->next;
    }
  }
  return head.next;
}
