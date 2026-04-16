// --- CodeGen ---
int label_id = 0;
int break_labels[64];
int break_top = 0;
int continue_labels[64];
int continue_top = 0;

void gen(Node *node) {
  if (!node) return;
  switch (node->kind) {
    case ND_NUM: printf("  push %d\n", node->val); return;
    case ND_ID: 
      if (node->sym) printf("  push [rbp-%d] # load %s\n", node->sym->offset, node->name);
      else printf("  push [rbp-8] # fallback load %s\n", node->name);
      return;
    case ND_ASSIGN:
      if (node->lhs && node->lhs->kind == ND_DEREF) {
        gen(node->lhs->lhs);
        gen(node->rhs);
        if (node->lhs->ptr_level == 0 && type_size_from_name(node->lhs->type_name) == 1)
          printf("  pop rax\n  pop rdi\n  mov [rdi], al\n  push rax\n");
        else if (node->lhs->ptr_level == 0 && type_size_from_name(node->lhs->type_name) == 2)
          printf("  pop rax\n  pop rdi\n  mov [rdi], ax\n  push rax\n");
        else if (node->lhs->ptr_level == 0 && type_size_from_name(node->lhs->type_name) == 4)
          printf("  pop rax\n  pop rdi\n  mov [rdi], eax\n  push rax\n");
        else
          printf("  pop rax\n  pop rdi\n  mov [rdi], rax\n  push rax\n");
        return;
      }
      gen(node->rhs);
      printf("  pop rax\n");
      if (node->lhs && node->lhs->sym) printf("  mov [rbp-%d], rax # store %s\n", node->lhs->sym->offset, node->lhs->name);
      else if (node->lhs) printf("  mov [rbp-8], rax # fallback store %s\n", node->lhs->name ? node->lhs->name : "?");
      else printf("  mov [rbp-8], rax\n");
      printf("  push rax\n");
      return;
    case ND_ADDR:
      if (node->lhs && node->lhs->kind == ND_DEREF) {
        gen(node->lhs->lhs);
      } else if (node->lhs && node->lhs->sym) {
        printf("  lea rax, [rbp-%d]\n  push rax\n", node->lhs->sym->offset);
      } else {
        printf("  push 0\n");
      }
      return;
    case ND_DEREF:
      gen(node->lhs);
      if (node->ptr_level == 0 && type_size_from_name(node->type_name) == 1)
        printf("  pop rax\n  movzx eax, byte ptr [rax]\n  push rax\n");
      else if (node->ptr_level == 0 && type_size_from_name(node->type_name) == 2)
        printf("  pop rax\n  movzx eax, word ptr [rax]\n  push rax\n");
      else if (node->ptr_level == 0 && type_size_from_name(node->type_name) == 4)
        printf("  pop rax\n  mov eax, dword ptr [rax]\n  push rax\n");
      else
        printf("  pop rax\n  mov rax, [rax]\n  push rax\n");
      return;
    case ND_NOT:
      gen(node->lhs);
      printf("  pop rax\n  cmp rax, 0\n  sete al\n  movzx rax, al\n  push rax\n");
      return;
    case ND_BNOT:
      gen(node->lhs);
      printf("  pop rax\n  not rax\n  push rax\n");
      return;
    case ND_NEG:
      gen(node->lhs);
      printf("  pop rax\n  neg rax\n  push rax\n");
      return;
    case ND_TERNARY: {
      int id = label_id++;
      gen(node->cond);
      printf("  pop rax\n  cmp rax, 0\n  je .Lelse%d\n", id);
      gen(node->then);
      printf("  jmp .Lend%d\n.Lelse%d:\n", id, id);
      gen(node->els);
      printf(".Lend%d:\n", id);
      return;
    }
    case ND_PRE_INC:
    case ND_POST_INC:
    case ND_PRE_DEC:
    case ND_POST_DEC: {
      int add = (node->kind == ND_PRE_INC || node->kind == ND_POST_INC) ? 1 : -1;
      int post = (node->kind == ND_POST_INC || node->kind == ND_POST_DEC);
      if (node->lhs && node->lhs->kind == ND_DEREF) {
        gen(node->lhs->lhs);
        printf("  pop rdi\n  mov rax, [rdi]\n");
        if (post) printf("  push rax\n");
        if (add > 0) printf("  add rax, 1\n");
        else printf("  sub rax, 1\n");
        printf("  mov [rdi], rax\n");
        if (!post) printf("  push rax\n");
        return;
      }
      if (node->lhs && node->lhs->sym) {
        printf("  mov rax, [rbp-%d]\n", node->lhs->sym->offset);
        if (post) printf("  push rax\n");
        if (add > 0) printf("  add rax, 1\n");
        else printf("  sub rax, 1\n");
        printf("  mov [rbp-%d], rax\n", node->lhs->sym->offset);
        if (!post) printf("  push rax\n");
      } else {
        printf("  push 0\n");
      }
      return;
    }
    case ND_COMMA:
      gen(node->lhs);
      printf("  pop rax\n");
      gen(node->rhs);
      return;
    case ND_IF: {
      int id = label_id++; gen(node->cond); printf("  pop rax\n  cmp rax, 0\n  je .Lelse%d\n", id);
      gen(node->then); printf("  jmp .Lend%d\n.Lelse%d:\n", id, id);
      if (node->els) gen(node->els); printf(".Lend%d:\n", id); return;
    }
    case ND_WHILE: {
      int id = label_id++; printf(".Lcont%d:\n", id); gen(node->cond);
      printf("  pop rax\n  cmp rax, 0\n  je .Lend%d\n", id);
      break_labels[break_top++] = id;
      continue_labels[continue_top++] = id;
      gen(node->body);
      continue_top--;
      break_top--;
      printf("  jmp .Lcont%d\n.Lend%d:\n", id, id); return;
    }
    case ND_FOR: {
      int id = label_id++;
      if (node->init) gen(node->init);
      printf(".Lbegin%d:\n", id);
      if (node->cond) { gen(node->cond); printf("  pop rax\n  cmp rax, 0\n  je .Lend%d\n", id); }
      break_labels[break_top++] = id;
      continue_labels[continue_top++] = id;
      gen(node->body);
      printf(".Lcont%d:\n", id);
      continue_top--;
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
    case ND_CONTINUE:
      if (continue_top > 0) printf("  jmp .Lcont%d\n", continue_labels[continue_top - 1]);
      return;
    case ND_RETURN:
      if (node->lhs) { gen(node->lhs); printf("  pop rax\n"); }
      else printf("  mov rax, 0\n");
      printf("  mov rsp, rbp\n  pop rbp\n  ret\n");
      return;
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
    case ND_BITAND: printf("  and rax, rdi\n"); break;
  }
  printf("  push rax\n");
}

void emit_elf_header() {
  printf("# Minimal ELF64 Header (x86_64)\n");
  printf("# .byte 0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00...\n");
}

int emit_asm_only() {
  src = read_stdin_source();
  if (!src) {
    eprintf("Error: cannot read stdin\n", 0, 0, 0, 0, 0, 0);
    return 1;
  }
  src_base = src;

  printf(".intel_syntax noprefix\n");
  emit_elf_header();
  next();
  while (token.type != TK_EOF) {
    if (try_parse_typedef_or_record_decl()) continue;
    Node *fn = function();
    if (fn) gen(fn);
  }
  printf(".section .note.GNU-stack,\"\",@progbits\n");
  return 0;
}
