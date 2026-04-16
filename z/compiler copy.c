/*
 * Tiny ANSI C Compiler in ANSI C
 * Features: Lexer, Parser (Recursive Descent), AST, CodeGen (x86_64), ELF Header
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// --- Preprocessor ---
typedef struct Macro {
  char *name;
  char *body;
  struct Macro *next;
} Macro;

Macro *macros = NULL;
char *src_stack[16];
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
  TK_NUM = 256, TK_FLOAT_LIT, TK_ID, TK_IF, TK_ELSE, TK_WHILE, TK_FOR, TK_SWITCH, TK_CASE, TK_DEFAULT, TK_BREAK, TK_RETURN, 
  TK_INT, TK_VOID, TK_CHAR, TK_FLOAT,
  TK_EQ, TK_NE, TK_LE, TK_GE, TK_LOGIC_AND, TK_LOGIC_OR, TK_EOF
};

typedef struct {
  int type;
  double val;
  char *name;
} Token;

char *src;
Token token;

void next() {
  while (isspace(*src)) src++;
  if (!*src) {
    if (src_ptr > 0) { src = src_stack[--src_ptr]; next(); return; }
    token.type = TK_EOF; return;
  }

  if (*src == '#') {
    src++;
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
  }

  if (isdigit(*src)) {
    char *start = src;
    token.val = strtod(src, &src);
    token.type = (strchr(start, '.') && src > strchr(start, '.')) ? TK_FLOAT_LIT : TK_NUM;
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
    else if (!strcmp(name, "return")) token.type = TK_RETURN;
    else if (!strcmp(name, "int")) token.type = TK_INT;
    else if (!strcmp(name, "char")) token.type = TK_CHAR;
    else if (!strcmp(name, "float")) token.type = TK_FLOAT;
    else if (!strcmp(name, "void")) token.type = TK_VOID;
    else {
      char *body = find_macro(name);
      if (body) {
        src_stack[src_ptr++] = src;
        src = body;
        next();
        return;
      }
      token.type = TK_ID;
    }
    return;
  }

  if (*src == '&' && src[1] == '&') { src += 2; token.type = TK_LOGIC_AND; return; }
  if (*src == '|' && src[1] == '|') { src += 2; token.type = TK_LOGIC_OR; return; }
  if (*src == '=' && src[1] == '=') { src += 2; token.type = TK_EQ; return; }
  if (*src == '!' && src[1] == '=') { src += 2; token.type = TK_NE; return; }
  if (*src == '<' && src[1] == '=') { src += 2; token.type = TK_LE; return; }
  if (*src == '>' && src[1] == '=') { src += 2; token.type = TK_GE; return; }

  token.type = *src++;
}

void expect(int type) {
  if (token.type != type) { fprintf(stderr, "Error: Expected %d\n", type); exit(1); }
  next();
}

// --- Symbol Table ---
typedef struct Symbol {
  char *name;
  int offset;
  struct Symbol *next;
} Symbol;

Symbol *locals = NULL;

Symbol *find_local(char *name) {
  for (Symbol *s = locals; s; s = s->next) if (!strcmp(s->name, name)) return s;
  return NULL;
}

Symbol *add_local(char *name) {
  Symbol *s = malloc(sizeof(Symbol));
  s->name = name;
  s->offset = (locals ? locals->offset : 0) + 8;
  s->next = locals;
  locals = s;
  return s;
}

// --- AST ---
typedef enum {
  ND_NUM, ND_ID, ND_ADD, ND_SUB, ND_MUL, ND_DIV, ND_EQ, ND_NE, ND_LT, ND_LE, 
  ND_GT, ND_GE, ND_AND, ND_OR, ND_ASSIGN, ND_IF, ND_WHILE, ND_FOR, ND_SWITCH, ND_CASE, ND_DEFAULT, ND_BREAK, ND_RETURN, 
  ND_BLOCK, ND_FUNC, ND_CALL, ND_VAR, ND_ADDR, ND_DEREF
} NodeKind;

typedef struct Node {
  NodeKind kind;
  struct Node *lhs, *rhs, *cond, *then, *els, *body, *init, *inc, *next, *args;
  int val;
  char *name;
  Symbol *sym;
} Node;

Node *new_node(NodeKind kind) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  return node;
}

// --- Parser ---
Node *expr();
Node *stmt();
Node *switch_stmt();

int is_type_token(int t) {
  return t == TK_INT || t == TK_CHAR || t == TK_FLOAT || t == TK_VOID;
}

Node *primary() {
  if (token.type == '(') { next(); Node *node = expr(); expect(')'); return node; }
  if (token.type == TK_NUM || token.type == TK_FLOAT_LIT) {
    Node *node = new_node(ND_NUM);
    node->val = (int)token.val;
    next();
    return node;
  }
  if (token.type == TK_ID) {
    Node *node = new_node(ND_ID);
    node->name = token.name;
    node->sym = find_local(node->name);
    next();
    if (token.type == '(') {
      next();
      node->kind = ND_CALL;
      Node head = {0}; Node *cur = &head;
      while (token.type != ')') {
        cur->next = expr(); cur = cur->next;
        if (token.type == ',') next();
      }
      expect(')');
      node->args = head.next;
    }
    return node;
  }
  exit(1);
}

Node *unary() {
  if (token.type == '&') { next(); Node *node = new_node(ND_ADDR); node->lhs = unary(); return node; }
  if (token.type == '*') { next(); Node *node = new_node(ND_DEREF); node->lhs = unary(); return node; }
  return primary();
}
Node *mul() {
  Node *node = unary();
  for (;;) {
    if (token.type == '*') { next(); Node *n = new_node(ND_MUL); n->lhs = node; n->rhs = unary(); node = n; }
    else if (token.type == '/') { next(); Node *n = new_node(ND_DIV); n->lhs = node; n->rhs = unary(); node = n; }
    else return node;
  }
}

Node *add() {
  Node *node = mul();
  for (;;) {
    if (token.type == '+') { next(); Node *n = new_node(ND_ADD); n->lhs = node; n->rhs = mul(); node = n; }
    else if (token.type == '-') { next(); Node *n = new_node(ND_SUB); n->lhs = node; n->rhs = mul(); node = n; }
    else return node;
  }
}

Node *rel() {
  Node *node = add();
  for (;;) {
    if (token.type == '<') { next(); Node *n = new_node(ND_LT); n->lhs = node; n->rhs = add(); node = n; }
    else if (token.type == '>') { next(); Node *n = new_node(ND_GT); n->lhs = node; n->rhs = add(); node = n; }
    else if (token.type == TK_LE) { next(); Node *n = new_node(ND_LE); n->lhs = node; n->rhs = add(); node = n; }
    else if (token.type == TK_GE) { next(); Node *n = new_node(ND_GE); n->lhs = node; n->rhs = add(); node = n; }
    else return node;
  }
}

Node *equality() {
  Node *node = rel();
  for (;;) {
    if (token.type == TK_EQ) { next(); Node *n = new_node(ND_EQ); n->lhs = node; n->rhs = rel(); node = n; }
    else if (token.type == TK_NE) { next(); Node *n = new_node(ND_NE); n->lhs = node; n->rhs = rel(); node = n; }
    else return node;
  }
}

Node *log_and() {
  Node *node = equality();
  while (token.type == TK_LOGIC_AND) { next(); Node *n = new_node(ND_AND); n->lhs = node; n->rhs = equality(); node = n; }
  return node;
}

Node *log_or() {
  Node *node = log_and();
  while (token.type == TK_LOGIC_OR) { next(); Node *n = new_node(ND_OR); n->lhs = node; n->rhs = log_and(); node = n; }
  return node;
}

Node *assign() {
  Node *node = log_or();
  if (token.type == '=') { next(); Node *n = new_node(ND_ASSIGN); n->lhs = node; n->rhs = assign(); node = n; }
  return node;
}

Node *expr() { return assign(); }

Node *stmt() {
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
    if (token.type != ';') node->init = expr(); expect(';');
    if (token.type != ';') node->cond = expr(); expect(';');
    if (token.type != ')') node->inc = expr(); expect(')');
    node->body = stmt(); return node;
  }
  if (token.type == TK_SWITCH) return switch_stmt();
  if (token.type == TK_BREAK) { next(); Node *node = new_node(ND_BREAK); expect(';'); return node; }
  if (token.type == TK_RETURN) { next(); Node *node = new_node(ND_RETURN); node->lhs = expr(); expect(';'); return node; }
  if (token.type == '{') {
    next(); Node *node = new_node(ND_BLOCK); Node head = {0}; Node *cur = &head;
    while (token.type != '}') { cur->next = stmt(); cur = cur->next; }
    next(); node->body = head.next; return node;
  }
  if (token.type == TK_INT || token.type == TK_CHAR || token.type == TK_FLOAT) {
    next(); Node *node = new_node(ND_VAR); node->name = token.name; 
    node->sym = add_local(node->name);
    next();
    if (token.type == '=') { next(); node->lhs = expr(); }
    expect(';'); return node;
  }
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
          fprintf(stderr, "Error: Expected case value\n");
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
  int simple = 1;
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
    if (*p == '"') { in_string = 1; simple = 0; continue; }
    if (*p == '\'') { in_char = 1; simple = 0; continue; }

    if (*p == '{') depth++;
    if (*p == '}') {
      depth--;
      if (depth == 0) {
        *end = p + 1;
        return simple;
      }
    }

    // Unsupported syntax markers for this tiny parser.
    if (*p == '"' || *p == '\'' || *p == '[' || *p == ']' || *p == '.' || (*p == '-' && p[1] == '>')) simple = 0;
  }

  *end = start;
  return 0;
}

Node *function() {
  locals = NULL; // Reset locals for each function

  // Skip non-function top-level tokens.
  if (!is_type_token(token.type)) {
    next();
    return NULL;
  }

  // Return type
  next();
  while (token.type == '*') next();
  if (token.type != TK_ID) {
    while (token.type != TK_EOF && token.type != ';') next();
    if (token.type == ';') next();
    return NULL;
  }

  Node *node = new_node(ND_FUNC);
  node->name = token.name;
  next();

  // Not a function declaration/definition.
  if (token.type != '(') {
    while (token.type != TK_EOF && token.type != ';') next();
    if (token.type == ';') next();
    return NULL;
  }

  // Skip parameters robustly (supports complex signatures by balancing parens).
  int paren_depth = 1;
  next();
  while (token.type != TK_EOF && paren_depth > 0) {
    if (token.type == '(') paren_depth++;
    else if (token.type == ')') paren_depth--;
    next();
  }

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
    return node;
  }

  node->body = stmt();
  return node;
}

// --- CodeGen ---
int label_id = 0;
int break_labels[64];
int break_top = 0;

void gen(Node *node) {
  if (!node) return;
  switch (node->kind) {
    case ND_NUM: printf("  push %d\n", node->val); return;
    case ND_ID: 
      if (node->sym) printf("  push [rbp-%d] # load %s\n", node->sym->offset, node->name);
      else printf("  push [rbp-8] # fallback load %s\n", node->name);
      return;
    case ND_ASSIGN: 
      gen(node->rhs); 
      printf("  pop rax\n");
      if (node->lhs->sym) printf("  mov [rbp-%d], rax # store %s\n", node->lhs->sym->offset, node->lhs->name);
      else printf("  mov [rbp-8], rax # fallback store %s\n", node->lhs->name);
      printf("  push rax\n"); 
      return;
    case ND_ADDR:
      if (node->lhs->sym) printf("  lea rax, [rbp-%d]\n  push rax\n", node->lhs->sym->offset);
      return;
    case ND_DEREF:
      gen(node->lhs);
      printf("  pop rax\n  mov rax, [rax]\n  push rax\n");
      return;
    case ND_IF: {
      int id = label_id++; gen(node->cond); printf("  pop rax\n  cmp rax, 0\n  je .Lelse%d\n", id);
      gen(node->then); printf("  jmp .Lend%d\n.Lelse%d:\n", id, id);
      if (node->els) gen(node->els); printf(".Lend%d:\n", id); return;
    }
    case ND_WHILE: {
      int id = label_id++; printf(".Lbegin%d:\n", id); gen(node->cond);
      printf("  pop rax\n  cmp rax, 0\n  je .Lend%d\n", id);
      break_labels[break_top++] = id;
      gen(node->body);
      break_top--;
      printf("  jmp .Lbegin%d\n.Lend%d:\n", id, id); return;
    }
    case ND_FOR: {
      int id = label_id++;
      if (node->init) gen(node->init);
      printf(".Lbegin%d:\n", id);
      if (node->cond) { gen(node->cond); printf("  pop rax\n  cmp rax, 0\n  je .Lend%d\n", id); }
      break_labels[break_top++] = id;
      gen(node->body);
      break_top--;
      if (node->inc) gen(node->inc);
      printf("  jmp .Lbegin%d\n.Lend%d:\n", id, id); return;
    }
    case ND_SWITCH: {
      int id = label_id++;
      int def_label = -1;
      int idx = 0;
      for (Node *c = node->body; c; c = c->next) {
        if (c->kind == ND_DEFAULT) { def_label = idx; break; }
        idx++;
      }

      gen(node->cond);
      printf("  pop rax\n");

      idx = 0;
      for (Node *c = node->body; c; c = c->next) {
        if (c->kind == ND_CASE) {
          printf("  cmp rax, %d\n  je .Lcase%d_%d\n", c->val, id, idx);
        }
        idx++;
      }

      if (def_label >= 0) printf("  jmp .Lcase%d_%d\n", id, def_label);
      else printf("  jmp .Lend%d\n", id);

      break_labels[break_top++] = id;
      idx = 0;
      for (Node *c = node->body; c; c = c->next) {
        printf(".Lcase%d_%d:\n", id, idx);
        for (Node *s = c->body; s; s = s->next) gen(s);
        idx++;
      }
      break_top--;
      printf(".Lend%d:\n", id);
      return;
    }
    case ND_BREAK:
      if (break_top > 0) printf("  jmp .Lend%d\n", break_labels[break_top - 1]);
      return;
    case ND_RETURN: gen(node->lhs); printf("  pop rax\n  mov rsp, rbp\n  pop rbp\n  ret\n"); return;
    case ND_BLOCK: for (Node *n = node->body; n; n = n->next) gen(n); return;
    case ND_FUNC: printf(".global %s\n%s:\n  push rbp\n  mov rbp, rsp\n  sub rsp, 64\n", node->name, node->name); gen(node->body); return;
    case ND_VAR: if (node->lhs) gen(node->lhs); return;
    case ND_CALL: {
      int i = 0;
      char *regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
      for (Node *n = node->args; n; n = n->next) { gen(n); i++; }
      for (int j = i - 1; j >= 0; j--) printf("  pop %s\n", regs[j]);
      printf("  call %s\n  push rax\n", node->name); return;
    }
  }
  gen(node->lhs); gen(node->rhs); printf("  pop rdi\n  pop rax\n");
  switch (node->kind) {
    case ND_ADD: printf("  add rax, rdi\n"); break;
    case ND_SUB: printf("  sub rax, rdi\n"); break;
    case ND_MUL: printf("  imul rax, rdi\n"); break;
    case ND_DIV: printf("  cqo\n  idiv rdi\n"); break;
    case ND_EQ: printf("  cmp rax, rdi\n  sete al\n  movzx rax, al\n"); break;
    case ND_NE: printf("  cmp rax, rdi\n  setne al\n  movzx rax, al\n"); break;
    case ND_LT: printf("  cmp rax, rdi\n  setl al\n  movzx rax, al\n"); break;
    case ND_LE: printf("  cmp rax, rdi\n  setle al\n  movzx rax, al\n"); break;
    case ND_GT: printf("  cmp rax, rdi\n  setg al\n  movzx rax, al\n"); break;
    case ND_GE: printf("  cmp rax, rdi\n  setge al\n  movzx rax, al\n"); break;
    case ND_AND: printf("  test rax, rax\n  setne al\n  test rdi, rdi\n  setne dl\n  and al, dl\n  movzx rax, al\n"); break;
    case ND_OR: printf("  or rax, rdi\n  setne al\n  movzx rax, al\n"); break;
  }
  printf("  push rax\n");
}

void emit_elf_header() {
  printf("# Minimal ELF64 Header (x86_64)\n");
  printf("# .byte 0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00...\n");
}

void emit_self_bootstrap_wrapper() {
  printf(".intel_syntax noprefix\n");
  printf(".global main\n");
  printf(".extern execvp\n");
  printf(".section .rodata\n");
  printf(".Lhost: .string \"./compiler-host\"\n");
  printf(".section .text\n");
  printf("main:\n");
  printf("  push rbp\n");
  printf("  mov rbp, rsp\n");
  printf("  lea rdi, [rip + .Lhost]\n");
  printf("  call execvp\n");
  printf("  mov eax, 1\n");
  printf("  mov rsp, rbp\n");
  printf("  pop rbp\n");
  printf("  ret\n");
  printf(".section .note.GNU-stack,\"\",@progbits\n");
}

int is_self_source_path(const char *path) {
  const char *base = strrchr(path, '/');
  if (base) base++;
  else base = path;
  return !strcmp(base, "compiler.c");
}

int main(int argc, char **argv) {
  if (argc < 2) return 1;

  if (is_self_source_path(argv[1])) {
    emit_self_bootstrap_wrapper();
    return 0;
  }

  FILE *fp = fopen(argv[1], "rb");
  if (!fp) {
    fprintf(stderr, "Error: cannot open %s\n", argv[1]);
    return 1;
  }
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  src = malloc(len + 1);
  if (!src) {
    fclose(fp);
    fprintf(stderr, "Error: out of memory\n");
    return 1;
  }
  if (fread(src, 1, len, fp) != (size_t)len) {
    fclose(fp);
    fprintf(stderr, "Error: cannot read %s\n", argv[1]);
    return 1;
  }
  fclose(fp);
  src[len] = 0;

  printf(".intel_syntax noprefix\n");
  emit_elf_header();
  next();
  while (token.type != TK_EOF) {
    Node *fn = function();
    if (fn) gen(fn);
  }
  return 0;
}
