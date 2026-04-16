typedef struct {
  int kind; // 0 normal, 1 break, 2 return, 3 continue
  long val;
} ExecResult;

long eval_expr(Node *node, long *vars);
ExecResult exec_stmt(Node *node, long *vars);

long slot_get(Symbol *sym, long *vars) {
  int idx = sym ? sym->offset / 8 : 1;
  if (idx < 0) idx = 0;
  if (idx > 255) idx = 255;
  return vars[idx];
}

void slot_set(Symbol *sym, long *vars, long v) {
  int idx = sym ? sym->offset / 8 : 1;
  if (idx < 0) idx = 0;
  if (idx > 255) idx = 255;
  vars[idx] = v;
}

long eval_expr(Node *node, long *vars) {
  if (!node) return 0;
  switch (node->kind) {
    case ND_NUM: return node->val;
    case ND_ID: return slot_get(node->sym, vars);
    case ND_NEG: return -eval_expr(node->lhs, vars);
    case ND_BNOT: return ~eval_expr(node->lhs, vars);
    case ND_ASSIGN: {
      long v = eval_expr(node->rhs, vars);
      if (node->lhs && node->lhs->kind == ND_ID) slot_set(node->lhs->sym, vars, v);
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
  for (Node *f = funcs; f; f = f->next) {
    if (f->kind == ND_FUNC && f->name && !strcmp(f->name, "main")) {
      main_fn = f;
      break;
    }
  }
  if (!main_fn || !main_fn->body) return 1;

  long vars[256];
  memset(vars, 0, sizeof(vars));
  ExecResult r = exec_stmt(main_fn->body, vars);
  if (r.kind == 2) return (int)(r.val & 255);
  return 0;
}
