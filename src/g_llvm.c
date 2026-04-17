/*
 * Symbolic stack-machine IR backend (LLVM-like textual shape).
 */

int ast_label_id = 0;
int ast_break_labels[64];
int ast_break_top = 0;
int ast_continue_labels[64];
int ast_continue_top = 0;

void ast_emit_expr(Node *node);
void ast_emit_stmt(Node *node);

void ast_emit_expr(Node *node) {
  if (!node) {
    printf("  push.const 0\n");
    return;
  }

  switch (node->kind) {
    case ND_NUM:
      if (node->ptr_level > 0 && node->name) printf("  push.str \"%s\"\n", node->name);
      else printf("  push.const %d\n", node->val);
      return;
    case ND_ID:
      if (node->sym && node->sym->is_array) printf("  addr.local @%s ; off=%d\n", node->name ? node->name : "?", node->sym->offset);
      else if (node->sym) printf("  load.local @%s ; off=%d\n", node->name ? node->name : "?", node->sym->offset);
      else printf("  load.global @%s\n", node->name ? node->name : "?");
      return;
    case ND_ASSIGN:
      if (node->lhs && node->lhs->kind == ND_DEREF) {
        int lsz = (node->lhs->ptr_level == 0) ? type_size_from_name(node->lhs->type_name) : 8;
        ast_emit_expr(node->lhs->lhs);
        ast_emit_expr(node->rhs);
        if (lsz == 1) printf("  store.indirect1\n");
        else if (lsz == 2) printf("  store.indirect2\n");
        else if (lsz == 4) printf("  store.indirect4\n");
        else printf("  store.indirect8\n");
        return;
      }
      ast_emit_expr(node->rhs);
      if (node->lhs && node->lhs->sym) printf("  store.local @%s ; off=%d\n", node->lhs->name ? node->lhs->name : "?", node->lhs->sym->offset);
      else if (node->lhs && node->lhs->name) printf("  store.global @%s\n", node->lhs->name);
      else printf("  drop\n");
      return;
    case ND_ADDR:
      if (node->lhs && node->lhs->sym) printf("  addr.local @%s ; off=%d\n", node->lhs->name ? node->lhs->name : "?", node->lhs->sym->offset);
      else if (node->lhs && node->lhs->name) printf("  addr.global @%s\n", node->lhs->name);
      else if (node->lhs && node->lhs->kind == ND_DEREF) ast_emit_expr(node->lhs->lhs);
      else printf("  push.const 0\n");
      return;
    case ND_DEREF:
      {
      int lsz = (node->ptr_level == 0) ? type_size_from_name(node->type_name) : 8;
      ast_emit_expr(node->lhs);
      if (lsz == 1) printf("  load.indirect1\n");
      else if (lsz == 2) printf("  load.indirect2\n");
      else if (lsz == 4) printf("  load.indirect4\n");
      else printf("  load.indirect8\n");
      return;
      }
    case ND_CALL: {
      int argc = 0;
      const char *callee;
      for (Node *a = node->args; a; a = a->next) {
        ast_emit_expr(a);
        argc++;
      }
      callee = kernel_abi_symbol(node->name ? node->name : "?");
      printf("  call @%s %d\n", (long)callee, argc);
      return;
    }
    case ND_NOT:
      ast_emit_expr(node->lhs);
      printf("  un.not\n");
      return;
    case ND_NEG:
      ast_emit_expr(node->lhs);
      printf("  un.neg\n");
      return;
    case ND_BNOT:
      ast_emit_expr(node->lhs);
      printf("  un.bnot\n");
      return;
    case ND_PRE_INC:
    case ND_PRE_DEC:
    case ND_POST_INC:
    case ND_POST_DEC: {
      int is_inc = (node->kind == ND_PRE_INC || node->kind == ND_POST_INC);
      int is_post = (node->kind == ND_POST_INC || node->kind == ND_POST_DEC);
      if (node->lhs && node->lhs->kind == ND_ID && node->lhs->sym) {
        ast_emit_expr(node->lhs);
        printf("  push.const 1\n");
        if (is_inc) printf("  bin.add\n");
        else printf("  bin.sub\n");
        if (node->lhs && node->lhs->sym) printf("  store.local @%s ; off=%d\n", node->lhs->name ? node->lhs->name : "?", node->lhs->sym->offset);
        if (is_post) {
          printf("  push.const 1\n");
          if (is_inc) printf("  bin.sub\n");
          else printf("  bin.add\n");
        }
      } else {
        printf("  push.const 0\n");
      }
      return;
    }
    case ND_COMMA:
      ast_emit_expr(node->lhs);
      printf("  drop\n");
      ast_emit_expr(node->rhs);
      return;
    case ND_TERNARY: {
      int id = ast_label_id++;
      ast_emit_expr(node->cond);
      printf("  br.false .Lelse%d\n", id);
      ast_emit_expr(node->then);
      printf("  br .Lend%d\n", id);
      printf(".Lelse%d:\n", id);
      ast_emit_expr(node->els);
      printf(".Lend%d:\n", id);
      return;
    }
    default:
      break;
  }

  ast_emit_expr(node->lhs);
  ast_emit_expr(node->rhs);
  switch (node->kind) {
    case ND_ADD: printf("  bin.add\n"); break;
    case ND_SUB: printf("  bin.sub\n"); break;
    case ND_MUL: printf("  bin.mul\n"); break;
    case ND_DIV: printf("  bin.div\n"); break;
    case ND_EQ: printf("  cmp.eq\n"); break;
    case ND_NE: printf("  cmp.ne\n"); break;
    case ND_LT: printf("  cmp.lt\n"); break;
    case ND_LE: printf("  cmp.le\n"); break;
    case ND_GT: printf("  cmp.gt\n"); break;
    case ND_GE: printf("  cmp.ge\n"); break;
    case ND_AND: printf("  bin.land\n"); break;
    case ND_OR: printf("  bin.lor\n"); break;
    case ND_BITAND: printf("  bin.band\n"); break;
    case ND_BITOR: printf("  bin.bor\n"); break;
    default: printf("  ; unsupported expr kind=%d\n", node->kind); break;
  }
}

void ast_emit_stmt_list(Node *node) {
  for (Node *s = node; s; s = s->next) ast_emit_stmt(s);
}

void ast_emit_stmt(Node *node) {
  if (!node) return;
  switch (node->kind) {
    case ND_BLOCK:
      ast_emit_stmt_list(node->body);
      return;
    case ND_VAR:
      if (node->lhs) {
        ast_emit_expr(node->lhs);
        if (node->sym) printf("  store.local @%s ; off=%d\n", node->name ? node->name : "?", node->sym->offset);
      }
      return;
    case ND_IF: {
      int id = ast_label_id++;
      ast_emit_expr(node->cond);
      printf("  br.false .Lelse%d\n", id);
      ast_emit_stmt(node->then);
      printf("  br .Lend%d\n", id);
      printf(".Lelse%d:\n", id);
      if (node->els) ast_emit_stmt(node->els);
      printf(".Lend%d:\n", id);
      return;
    }
    case ND_WHILE: {
      int id = ast_label_id++;
      ast_break_labels[ast_break_top++] = id;
      ast_continue_labels[ast_continue_top++] = id;
      printf(".Lcond%d:\n", id);
      ast_emit_expr(node->cond);
      printf("  br.false .Lend%d\n", id);
      ast_emit_stmt(node->body);
      printf("  br .Lcond%d\n", id);
      printf(".Lend%d:\n", id);
      ast_continue_top--;
      ast_break_top--;
      return;
    }
    case ND_FOR: {
      int id = ast_label_id++;
      if (node->init) ast_emit_expr(node->init);
      printf(".Lcond%d:\n", id);
      if (node->cond) {
        ast_emit_expr(node->cond);
        printf("  br.false .Lend%d\n", id);
      }
      ast_break_labels[ast_break_top++] = id;
      ast_continue_labels[ast_continue_top++] = id;
      ast_emit_stmt(node->body);
      printf(".Lcont%d:\n", id);
      if (node->inc) ast_emit_expr(node->inc);
      printf("  br .Lcond%d\n", id);
      printf(".Lend%d:\n", id);
      ast_continue_top--;
      ast_break_top--;
      return;
    }
    case ND_SWITCH: {
      int id = ast_label_id++;
      int idx = 0;
      int has_default = 0;
      int def_idx = -1;
      ast_break_labels[ast_break_top++] = id;
      for (Node *c = node->body; c; c = c->next) {
        if (c->kind == ND_DEFAULT) { has_default = 1; def_idx = idx; }
        idx++;
      }
      idx = 0;
      for (Node *c = node->body; c; c = c->next) {
        if (c->kind == ND_CASE) {
          ast_emit_expr(node->cond);
          printf("  push.const %d\n", c->val);
          printf("  cmp.eq\n");
          printf("  br.false .Lsw_next%d_%d\n", id, idx);
          printf("  br .Lsw_case%d_%d\n", id, idx);
          printf(".Lsw_next%d_%d:\n", id, idx);
        } else if (c->kind == ND_DEFAULT) {
          printf("  br .Lsw_case%d_%d\n", id, idx);
        }
        idx++;
      }
      if (!has_default) {
        printf("  br .Lend%d\n", id);
      } else {
        printf("  br .Lsw_case%d_%d\n", id, def_idx);
      }
      idx = 0;
      for (Node *c = node->body; c; c = c->next) {
        printf(".Lsw_case%d_%d:\n", id, idx);
        ast_emit_stmt_list(c->body);
        idx++;
      }
      printf(".Lend%d:\n", id);
      ast_break_top--;
      return;
    }
    case ND_BREAK:
      if (ast_break_top > 0) printf("  br .Lend%d\n", ast_break_labels[ast_break_top - 1]);
      return;
    case ND_CONTINUE:
      if (ast_continue_top > 0) printf("  br .Lcont%d\n", ast_continue_labels[ast_continue_top - 1]);
      return;
    case ND_RETURN:
      if (node->lhs) ast_emit_expr(node->lhs);
      else printf("  push.const 0\n");
      printf("  ret\n");
      return;
    default:
      ast_emit_expr(node);
      printf("  drop\n");
      return;
  }
}

int emit_ast_from_source(char *source);

int emit_ast_only() {
  char *source = read_stdin_source();
  if (!source) {
    eprintf("Error: cannot read stdin\n", 0, 0, 0, 0);
    return 1;
  }
  return emit_ast_from_source(source);
}

int emit_ast_from_source(char *source) {
  Node *funcs = parse_program_functions(source);
  printf("; symbolic stack-machine IR\n");
  for (Node *f = funcs; f; f = f->next) {
    if (f->kind != ND_FUNC || !f->name || !f->body) continue;
    printf("func @%s\n", f->name);
    ast_emit_stmt(f->body);
    printf("endfunc\n\n");
  }
  return 0;
}
