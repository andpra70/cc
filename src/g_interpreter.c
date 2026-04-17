typedef struct {
  int kind; // 0 normal, 1 break, 2 return, 3 continue
  long val;
} ExecResult;

long eval_expr(Node *node, long *vars);
ExecResult exec_stmt(Node *node, long *vars);

unsigned char *interp_mem = NULL;
unsigned char *interp_base = NULL;
int interp_mem_size = 0;

static unsigned char *local_addr(Symbol *sym) {
  int off = sym ? sym->offset : 8;
  if (off < 0) off = 0;
  if (off > interp_mem_size) off = interp_mem_size;
  return interp_base - off;
}

long slot_get(Symbol *sym, long *vars) {
  long v = 0;
  unsigned char *p = local_addr(sym);
  (void)vars;
  memcpy(&v, p, sizeof(long));
  return v;
}

void slot_set(Symbol *sym, long *vars, long v) {
  unsigned char *p = local_addr(sym);
  (void)vars;
  memcpy(p, &v, sizeof(long));
}

long eval_expr(Node *node, long *vars) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_NUM:
      if (node->ptr_level > 0 && node->name) return (long)node->name;
      return node->val;
    case ND_ID:
      if (node->sym && node->sym->is_array) return (long)local_addr(node->sym);
      return slot_get(node->sym, vars);
    case ND_NEG: return -eval_expr(node->lhs, vars);
    case ND_BNOT: return ~eval_expr(node->lhs, vars);
    case ND_ADDR:
      if (node->lhs && node->lhs->kind == ND_ID && node->lhs->sym) return (long)local_addr(node->lhs->sym);
      if (node->lhs && node->lhs->kind == ND_DEREF) return eval_expr(node->lhs->lhs, vars);
      return 0;
    case ND_DEREF: {
      long a = eval_expr(node->lhs, vars);
      int lsz = (node->ptr_level == 0) ? type_size_from_name(node->type_name) : 8;
      if (!a) return 0;
      if (lsz == 1) return (long)(*(unsigned char *)(long)a);
      if (lsz == 2) return (long)(*(unsigned short *)(long)a);
      if (lsz == 4) return (long)(*(unsigned int *)(long)a);
      return *(long *)(long)a;
    }
    case ND_ASSIGN: {
      long v = eval_expr(node->rhs, vars);
      if (node->lhs && node->lhs->kind == ND_ID) slot_set(node->lhs->sym, vars, v);
      else if (node->lhs && node->lhs->kind == ND_DEREF) {
        long a = eval_expr(node->lhs->lhs, vars);
        int lsz = (node->lhs->ptr_level == 0) ? type_size_from_name(node->lhs->type_name) : 8;
        if (a) {
          if (lsz == 1) *(unsigned char *)(long)a = (unsigned char)v;
          else if (lsz == 2) *(unsigned short *)(long)a = (unsigned short)v;
          else if (lsz == 4) *(unsigned int *)(long)a = (unsigned int)v;
          else *(long *)(long)a = v;
        }
      }
      return v;
    }
    case ND_ADD: return eval_expr(node->lhs, vars) + eval_expr(node->rhs, vars);
    case ND_SUB: return eval_expr(node->lhs, vars) - eval_expr(node->rhs, vars);
    case ND_MUL: return eval_expr(node->lhs, vars) * eval_expr(node->rhs, vars);
    case ND_DIV: {
      long r = eval_expr(node->rhs, vars);
      if (r == 0) return 0;
      return eval_expr(node->lhs, vars) / r;
    }
    case ND_EQ: return eval_expr(node->lhs, vars) == eval_expr(node->rhs, vars);
    case ND_NE: return eval_expr(node->lhs, vars) != eval_expr(node->rhs, vars);
    case ND_LT: return eval_expr(node->lhs, vars) < eval_expr(node->rhs, vars);
    case ND_LE: return eval_expr(node->lhs, vars) <= eval_expr(node->rhs, vars);
    case ND_GT: return eval_expr(node->lhs, vars) > eval_expr(node->rhs, vars);
    case ND_GE: return eval_expr(node->lhs, vars) >= eval_expr(node->rhs, vars);
    case ND_AND: return eval_expr(node->lhs, vars) && eval_expr(node->rhs, vars);
    case ND_OR: return eval_expr(node->lhs, vars) || eval_expr(node->rhs, vars);
    case ND_BITAND: return eval_expr(node->lhs, vars) & eval_expr(node->rhs, vars);
    case ND_BITOR: return eval_expr(node->lhs, vars) | eval_expr(node->rhs, vars);
    case ND_NOT: return !eval_expr(node->lhs, vars);
    case ND_TERNARY: return eval_expr(node->cond, vars) ? eval_expr(node->then, vars) : eval_expr(node->els, vars);
    case ND_PRE_INC:
      if (node->lhs && node->lhs->kind == ND_ID) {
        long v = slot_get(node->lhs->sym, vars) + 1;
        slot_set(node->lhs->sym, vars, v);
        return v;
      }
      return 0;
    case ND_PRE_DEC:
      if (node->lhs && node->lhs->kind == ND_ID) {
        long v = slot_get(node->lhs->sym, vars) - 1;
        slot_set(node->lhs->sym, vars, v);
        return v;
      }
      return 0;
    case ND_POST_INC:
      if (node->lhs && node->lhs->kind == ND_ID) {
        long v = slot_get(node->lhs->sym, vars);
        slot_set(node->lhs->sym, vars, v + 1);
        return v;
      }
      return 0;
    case ND_POST_DEC:
      if (node->lhs && node->lhs->kind == ND_ID) {
        long v = slot_get(node->lhs->sym, vars);
        slot_set(node->lhs->sym, vars, v - 1);
        return v;
      }
      return 0;
    case ND_COMMA:
      (void)eval_expr(node->lhs, vars);
      return eval_expr(node->rhs, vars);
    case ND_CALL: {
      long call_args[16];
      long rv;
      const char *direct;
      const char *abi;
      int argc = 0;
      Node *a = node->args;
      while (a && argc < 16) {
        call_args[argc++] = eval_expr(a, vars);
        a = a->next;
      }
      direct = node->name ? node->name : "";
      abi = kernel_abi_symbol(direct);
      rv = kernel_abi_call(abi ? abi : direct, call_args, argc);
      if (rv == (long)KERNEL_ABI_UNKNOWN && abi && strcmp(abi, direct)) {
        rv = kernel_abi_call(direct, call_args, argc);
      }
      if (rv == (long)KERNEL_ABI_UNKNOWN) return 0;
      return rv;
    }
    default: return 0;
  }
}

ExecResult exec_stmt_list(Node *node, long *vars) {
  for (Node *s = node; s; s = s->next) {
    ExecResult r = exec_stmt(s, vars);
    if (r.kind) return r;
  }
  ExecResult ok = {0, 0};
  return ok;
}

ExecResult exec_stmt(Node *node, long *vars) {
  ExecResult r = {0, 0};
  if (!node) return r;
  switch (node->kind) {
    case ND_BLOCK:
      return exec_stmt_list(node->body, vars);
    case ND_VAR:
      if (node->lhs) slot_set(node->sym, vars, eval_expr(node->lhs, vars));
      return r;
    case ND_IF:
      if (eval_expr(node->cond, vars)) return exec_stmt(node->then, vars);
      if (node->els) return exec_stmt(node->els, vars);
      return r;
    case ND_WHILE:
      while (eval_expr(node->cond, vars)) {
        ExecResult rr = exec_stmt(node->body, vars);
        if (rr.kind == 1) break;
        if (rr.kind == 3) continue;
        if (rr.kind == 2) return rr;
      }
      return r;
    case ND_FOR:
      if (node->init) eval_expr(node->init, vars);
      while (!node->cond || eval_expr(node->cond, vars)) {
        ExecResult rr = exec_stmt(node->body, vars);
        if (rr.kind == 1) break;
        if (rr.kind == 2) return rr;
        if (node->inc) eval_expr(node->inc, vars);
        if (rr.kind == 3) continue;
      }
      return r;
    case ND_SWITCH: {
      long v = eval_expr(node->cond, vars);
      Node *def = NULL;
      Node *start = NULL;
      for (Node *c = node->body; c; c = c->next) {
        if (c->kind == ND_DEFAULT) def = c;
        if (c->kind == ND_CASE && c->val == v) { start = c; break; }
      }
      if (!start) start = def;
      for (Node *c = start; c; c = c->next) {
        ExecResult rr = exec_stmt_list(c->body, vars);
        if (rr.kind == 1) return r;
        if (rr.kind == 2) return rr;
        if (rr.kind == 3) return rr;
      }
      return r;
    }
    case ND_BREAK:
      r.kind = 1;
      return r;
    case ND_CONTINUE:
      r.kind = 3;
      return r;
    case ND_RETURN:
      r.kind = 2;
      r.val = eval_expr(node->lhs, vars);
      return r;
    default:
      eval_expr(node, vars);
      return r;
  }
}

int run_source_as_program(char *source) {
  Node *funcs = parse_program_functions(source);
  Node *main_fn = NULL;
  int max_off = 0;
  for (Node *f = funcs; f; f = f->next) {
    if (f->kind == ND_FUNC && f->name && !strcmp(f->name, "main")) {
      main_fn = f;
      break;
    }
  }
  if (!main_fn || !main_fn->body) return 1;

  {
    Node *stack[4096];
    int sp = 0;
    stack[sp++] = main_fn->body;
    while (sp > 0) {
      Node *n = stack[--sp];
      if (!n) continue;
      if (n->sym && n->sym->offset > max_off) max_off = n->sym->offset;
      if (n->lhs) stack[sp++] = n->lhs;
      if (n->rhs) stack[sp++] = n->rhs;
      if (n->cond) stack[sp++] = n->cond;
      if (n->then) stack[sp++] = n->then;
      if (n->els) stack[sp++] = n->els;
      if (n->body) stack[sp++] = n->body;
      if (n->init) stack[sp++] = n->init;
      if (n->inc) stack[sp++] = n->inc;
      if (n->next) stack[sp++] = n->next;
      if (n->args) stack[sp++] = n->args;
    }
  }

  interp_mem_size = ((max_off + 64 + 15) & ~15);
  if (interp_mem_size < 256) interp_mem_size = 256;
  interp_mem = calloc(1, interp_mem_size);
  if (!interp_mem) return 1;
  interp_base = interp_mem + interp_mem_size;

  long vars[1];
  vars[0] = 0;
  ExecResult r = exec_stmt(main_fn->body, vars);
  free(interp_mem);
  interp_mem = NULL;
  interp_base = NULL;
  interp_mem_size = 0;
  if (r.kind == 2) return (int)(r.val & 255);
  return 0;
}
