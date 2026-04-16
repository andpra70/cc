typedef struct {
  unsigned char *data;
  size_t len;
  size_t cap;
} BinBuf;

typedef struct {
  int label;
  size_t at;
} RelFix;

typedef struct {
  char *name;
  Node *fn;
  int label;
} FnLabel;

typedef struct {
  char *text;
  int label;
} StrLit;

typedef struct {
  BinBuf code;
  size_t *label_pos;
  unsigned char *label_set;
  int nlabels;
  int caplabels;
  RelFix *fixups;
  int nfixups;
  int capfixups;
  FnLabel *funcs;
  int nfuncs;
  int break_labels[128];
  int break_top;
  int continue_labels[128];
  int continue_top;
  int ret_label;
  int globals_label;
} ElfCtx;

StrLit *str_lits = NULL;
int n_str_lits = 0;
int cap_str_lits = 0;

void bb_reserve(BinBuf *b, size_t add) {
  if (b->len + add <= b->cap) return;
  size_t ncap = b->cap ? b->cap : 1024;
  while (ncap < b->len + add) ncap *= 2;
  b->data = realloc(b->data, ncap);
  b->cap = ncap;
}

void bb_emit1(BinBuf *b, unsigned char v) {
  bb_reserve(b, 1);
  b->data[b->len++] = v;
}

void bb_emit4(BinBuf *b, int32_t v) {
  bb_reserve(b, 4);
  memcpy(b->data + b->len, &v, 4);
  b->len += 4;
}

void bb_patch4(BinBuf *b, size_t at, int32_t v) {
  memcpy(b->data + at, &v, 4);
}

void ctx_ensure_labels(ElfCtx *c, int need) {
  if (need <= c->caplabels) return;
  int ncap = c->caplabels ? c->caplabels : 64;
  while (ncap < need) ncap *= 2;
  c->label_pos = realloc(c->label_pos, sizeof(size_t) * ncap);
  c->label_set = realloc(c->label_set, sizeof(unsigned char) * ncap);
  memset(c->label_set + c->caplabels, 0, ncap - c->caplabels);
  c->caplabels = ncap;
}

int ctx_new_label(ElfCtx *c) {
  int id = c->nlabels++;
  ctx_ensure_labels(c, c->nlabels);
  c->label_set[id] = 0;
  c->label_pos[id] = 0;
  return id;
}

void ctx_place_label(ElfCtx *c, int label) {
  c->label_set[label] = 1;
  c->label_pos[label] = c->code.len;
}

void ctx_add_fixup(ElfCtx *c, int label, size_t at) {
  if (c->nfixups == c->capfixups) {
    int ncap = c->capfixups ? c->capfixups * 2 : 64;
    c->fixups = realloc(c->fixups, sizeof(RelFix) * ncap);
    c->capfixups = ncap;
  }
  c->fixups[c->nfixups].label = label;
  c->fixups[c->nfixups].at = at;
  c->nfixups++;
}

void emit_rel32_fixup(ElfCtx *c, int label) {
  size_t at = c->code.len;
  bb_emit4(&c->code, 0);
  ctx_add_fixup(c, label, at);
}

void emit_call_label(ElfCtx *c, int label) {
  bb_emit1(&c->code, 0xe8);
  emit_rel32_fixup(c, label);
}

void emit_jmp_label(ElfCtx *c, int label) {
  bb_emit1(&c->code, 0xe9);
  emit_rel32_fixup(c, label);
}

void emit_je_label(ElfCtx *c, int label) {
  bb_emit1(&c->code, 0x0f);
  bb_emit1(&c->code, 0x84);
  emit_rel32_fixup(c, label);
}

void emit_push_rax(ElfCtx *c) { bb_emit1(&c->code, 0x50); }
void emit_pop_rax(ElfCtx *c) { bb_emit1(&c->code, 0x58); }
void emit_pop_rdi(ElfCtx *c) { bb_emit1(&c->code, 0x5f); }

void emit_push_imm32(ElfCtx *c, int v) {
  bb_emit1(&c->code, 0x68);
  bb_emit4(&c->code, v);
}

void reset_string_literals() {
  if (str_lits) free(str_lits);
  str_lits = NULL;
  n_str_lits = 0;
  cap_str_lits = 0;
}

int get_string_label(ElfCtx *c, const char *s) {
  for (int i = 0; i < n_str_lits; i++) {
    if (!strcmp(str_lits[i].text, s)) return str_lits[i].label;
  }
  if (n_str_lits >= cap_str_lits) {
    int ncap = cap_str_lits ? cap_str_lits * 2 : 64;
    StrLit *ns = realloc(str_lits, sizeof(StrLit) * ncap);
    if (!ns) {
      eprintf("Error: out of memory for string literals\n", 0, 0, 0, 0, 0, 0);
      exit(1);
    }
    str_lits = ns;
    cap_str_lits = ncap;
  }
  str_lits[n_str_lits].text = (char *)s;
  str_lits[n_str_lits].label = ctx_new_label(c);
  n_str_lits++;
  return str_lits[n_str_lits - 1].label;
}

void emit_push_string_addr(ElfCtx *c, const char *s) {
  int lbl = get_string_label(c, s);
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8d); bb_emit1(&c->code, 0x05); // lea rax,[rip+rel32]
  emit_rel32_fixup(c, lbl);
  emit_push_rax(c);
}

void emit_string_literals(ElfCtx *c) {
  for (int i = 0; i < n_str_lits; i++) {
    const char *s = str_lits[i].text;
    ctx_place_label(c, str_lits[i].label);
    while (*s) bb_emit1(&c->code, (unsigned char)*s++);
    bb_emit1(&c->code, 0);
  }
}

void emit_mov_rax_local(ElfCtx *c, int offset) {
  int32_t disp = -offset;
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8b); bb_emit1(&c->code, 0x85); bb_emit4(&c->code, disp);
}

void emit_mov_local_rax(ElfCtx *c, int offset) {
  int32_t disp = -offset;
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x85); bb_emit4(&c->code, disp);
}

void emit_lea_rax_local(ElfCtx *c, int offset) {
  int32_t disp = -offset;
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8d); bb_emit1(&c->code, 0x85); bb_emit4(&c->code, disp);
}

void emit_lea_rax_global(ElfCtx *c, int offset) {
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8d); bb_emit1(&c->code, 0x05); // lea rax,[rip+rel32]
  emit_rel32_fixup(c, c->globals_label);
  if (offset != 0) {
    bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x05); bb_emit4(&c->code, offset); // add rax,imm32
  }
}

void emit_mov_rax_global(ElfCtx *c, int offset) {
  emit_lea_rax_global(c, offset);
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8b); bb_emit1(&c->code, 0x00); // mov rax,[rax]
}

void emit_mov_global_rax(ElfCtx *c, int offset) {
  emit_push_rax(c);
  emit_lea_rax_global(c, offset);
  emit_pop_rdi(c);
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x38); // mov [rax],rdi
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xf8); // mov rax,rdi
}

void emit_cmp_rax_zero(ElfCtx *c) {
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xf8); bb_emit1(&c->code, 0x00);
}

void emit_movzx_rax_al(ElfCtx *c) {
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x0f); bb_emit1(&c->code, 0xb6); bb_emit1(&c->code, 0xc0);
}

void emit_setcc_al(ElfCtx *c, unsigned char cc) {
  bb_emit1(&c->code, 0x0f); bb_emit1(&c->code, cc); bb_emit1(&c->code, 0xc0);
}

int find_func_label(ElfCtx *c, const char *name) {
  for (int i = 0; i < c->nfuncs; i++) {
    if (!strcmp(c->funcs[i].name, name)) return c->funcs[i].label;
  }
  return -1;
}

int emit_builtin_syscall_call(ElfCtx *c, const char *name) {
  int sysno = -1;
  int needs_r10 = 0;
  if (!strcmp(name, "read")) sysno = 0;
  else if (!strcmp(name, "write")) sysno = 1;
  else if (!strcmp(name, "close")) sysno = 3;
  else if (!strcmp(name, "mmap")) { sysno = 9; needs_r10 = 1; }
  else if (!strcmp(name, "munmap")) sysno = 11;
  else if (!strcmp(name, "openat")) { sysno = 257; needs_r10 = 1; }
  else if (!strcmp(name, "exit")) sysno = 60;
  if (sysno < 0) return 0;

  if (needs_r10) {
    bb_emit1(&c->code, 0x49); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xca); // mov r10,rcx
  }
  bb_emit1(&c->code, 0xb8); bb_emit4(&c->code, sysno); // mov eax,sysno
  bb_emit1(&c->code, 0x0f); bb_emit1(&c->code, 0x05); // syscall
  return 1;
}

void emit_expr_elf(ElfCtx *c, Node *node);
void emit_stmt_elf(ElfCtx *c, Node *node);

void emit_expr_discard_elf(ElfCtx *c, Node *node) {
  if (!node) return;
  if (node->kind == ND_BLOCK) {
    emit_stmt_elf(c, node);
    return;
  }
  emit_expr_elf(c, node);
  emit_pop_rax(c);
}

void emit_expr_elf(ElfCtx *c, Node *node) {
  if (!node) return;
  switch (node->kind) {
    case ND_NUM:
      if (node->ptr_level > 0 && node->name) {
        emit_push_string_addr(c, node->name);
        return;
      }
      emit_push_imm32(c, node->val);
      return;
    case ND_ID: {
      int goff = 0;
      int garr = 0;
      if (node->sym) {
        emit_mov_rax_local(c, node->sym->offset);
      } else if (node->name && lookup_global_info(node->name, &goff, NULL, NULL, &garr, NULL)) {
        if (garr) emit_lea_rax_global(c, goff);
        else emit_mov_rax_global(c, goff);
      } else {
        emit_mov_rax_local(c, 8);
      }
      emit_push_rax(c);
      return;
    }
    case ND_ASSIGN: {
      if (node->lhs && node->lhs->kind == ND_DEREF) {
        int lsz = type_size_from_name(node->lhs->type_name);
        emit_expr_elf(c, node->lhs->lhs);
        emit_expr_elf(c, node->rhs);
        emit_pop_rax(c); // value
        emit_pop_rdi(c); // address
        if (node->lhs->ptr_level == 0 && lsz == 1) {
          bb_emit1(&c->code, 0x88); bb_emit1(&c->code, 0x07); // mov [rdi],al
        } else if (node->lhs->ptr_level == 0 && lsz == 2) {
          bb_emit1(&c->code, 0x66); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x07); // mov [rdi],ax
        } else if (node->lhs->ptr_level == 0 && lsz == 4) {
          bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x07); // mov [rdi],eax
        } else {
          bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x07); // mov [rdi],rax
        }
        emit_push_rax(c);
        return;
      }
      emit_expr_elf(c, node->rhs);
      emit_pop_rax(c);
      {
        int goff = 0;
        if (node->lhs && node->lhs->sym) emit_mov_local_rax(c, node->lhs->sym->offset);
        else if (node->lhs && node->lhs->name && lookup_global_info(node->lhs->name, &goff, NULL, NULL, NULL, NULL))
          emit_mov_global_rax(c, goff);
        else emit_mov_local_rax(c, 8);
      }
      emit_push_rax(c);
      return;
    }
    case ND_ADDR: {
      if (node->lhs && node->lhs->kind == ND_DEREF) {
        emit_expr_elf(c, node->lhs->lhs);
      } else {
        int goff = 0;
        if (node->lhs && node->lhs->sym) emit_lea_rax_local(c, node->lhs->sym->offset);
        else if (node->lhs && node->lhs->name && lookup_global_info(node->lhs->name, &goff, NULL, NULL, NULL, NULL))
          emit_lea_rax_global(c, goff);
        else emit_lea_rax_local(c, 8);
        emit_push_rax(c);
      }
      return;
    }
    case ND_DEREF:
      {
      int lsz = type_size_from_name(node->type_name);
      emit_expr_elf(c, node->lhs);
      emit_pop_rax(c);
      if (node->ptr_level == 0 && lsz == 1) {
        bb_emit1(&c->code, 0x0f); bb_emit1(&c->code, 0xb6); bb_emit1(&c->code, 0x00); // movzx eax, byte [rax]
      } else if (node->ptr_level == 0 && lsz == 2) {
        bb_emit1(&c->code, 0x0f); bb_emit1(&c->code, 0xb7); bb_emit1(&c->code, 0x00); // movzx eax, word [rax]
      } else if (node->ptr_level == 0 && lsz == 4) {
        bb_emit1(&c->code, 0x8b); bb_emit1(&c->code, 0x00); // mov eax, [rax]
      } else {
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8b); bb_emit1(&c->code, 0x00); // mov rax, [rax]
      }
      emit_push_rax(c);
      return;
      }
    case ND_CALL: {
      int argc = 0;
      for (Node *a = node->args; a; a = a->next) {
        emit_expr_elf(c, a);
        argc++;
      }
      for (int j = argc - 1; j >= 0; j--) {
        if (j == 0) bb_emit1(&c->code, 0x5f);          // pop rdi
        else if (j == 1) bb_emit1(&c->code, 0x5e);     // pop rsi
        else if (j == 2) bb_emit1(&c->code, 0x5a);     // pop rdx
        else if (j == 3) bb_emit1(&c->code, 0x59);     // pop rcx
        else if (j == 4) { bb_emit1(&c->code, 0x41); bb_emit1(&c->code, 0x58); } // pop r8
        else if (j == 5) { bb_emit1(&c->code, 0x41); bb_emit1(&c->code, 0x59); } // pop r9
        else bb_emit1(&c->code, 0x58);                 // pop rax discard
      }
      if (emit_builtin_syscall_call(c, node->name)) {
        emit_push_rax(c);
        return;
      } else {
        int lbl = find_func_label(c, node->name);
        if (lbl >= 0) emit_call_label(c, lbl);
        else { bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xc0); } // xor eax,eax
      }
      emit_push_rax(c);
      return;
    }
    case ND_NOT:
      emit_expr_elf(c, node->lhs);
      emit_pop_rax(c);
      emit_cmp_rax_zero(c);
      emit_setcc_al(c, 0x94);
      emit_movzx_rax_al(c);
      emit_push_rax(c);
      return;
    case ND_BNOT:
      emit_expr_elf(c, node->lhs);
      emit_pop_rax(c);
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0xf7); bb_emit1(&c->code, 0xd0); // not rax
      emit_push_rax(c);
      return;
    case ND_NEG:
      emit_expr_elf(c, node->lhs);
      emit_pop_rax(c);
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0xf7); bb_emit1(&c->code, 0xd8); // neg rax
      emit_push_rax(c);
      return;
    case ND_TERNARY: {
      int l_else = ctx_new_label(c);
      int l_end = ctx_new_label(c);
      emit_expr_elf(c, node->cond);
      emit_pop_rax(c);
      emit_cmp_rax_zero(c);
      emit_je_label(c, l_else);
      emit_expr_elf(c, node->then);
      emit_jmp_label(c, l_end);
      ctx_place_label(c, l_else);
      emit_expr_elf(c, node->els);
      ctx_place_label(c, l_end);
      return;
    }
    case ND_AND: {
      int l_false = ctx_new_label(c);
      int l_end = ctx_new_label(c);
      emit_expr_elf(c, node->lhs);
      emit_pop_rax(c);
      emit_cmp_rax_zero(c);
      emit_je_label(c, l_false);
      emit_expr_elf(c, node->rhs);
      emit_pop_rax(c);
      emit_cmp_rax_zero(c);
      emit_je_label(c, l_false);
      emit_push_imm32(c, 1);
      emit_jmp_label(c, l_end);
      ctx_place_label(c, l_false);
      emit_push_imm32(c, 0);
      ctx_place_label(c, l_end);
      return;
    }
    case ND_OR: {
      int l_true = ctx_new_label(c);
      int l_end = ctx_new_label(c);
      emit_expr_elf(c, node->lhs);
      emit_pop_rax(c);
      emit_cmp_rax_zero(c);
      bb_emit1(&c->code, 0x0f); bb_emit1(&c->code, 0x85); // jne
      emit_rel32_fixup(c, l_true);
      emit_expr_elf(c, node->rhs);
      emit_pop_rax(c);
      emit_cmp_rax_zero(c);
      bb_emit1(&c->code, 0x0f); bb_emit1(&c->code, 0x85); // jne
      emit_rel32_fixup(c, l_true);
      emit_push_imm32(c, 0);
      emit_jmp_label(c, l_end);
      ctx_place_label(c, l_true);
      emit_push_imm32(c, 1);
      ctx_place_label(c, l_end);
      return;
    }
    case ND_PRE_INC:
    case ND_POST_INC:
    case ND_PRE_DEC:
    case ND_POST_DEC: {
      int inc = (node->kind == ND_PRE_INC || node->kind == ND_POST_INC) ? 1 : -1;
      int post = (node->kind == ND_POST_INC || node->kind == ND_POST_DEC);
      if (node->lhs && node->lhs->kind == ND_DEREF) {
        emit_expr_elf(c, node->lhs->lhs);
        emit_pop_rdi(c);
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8b); bb_emit1(&c->code, 0x07); // mov rax,[rdi]
        if (post) emit_push_rax(c);
        if (inc > 0) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xc0); bb_emit1(&c->code, 0x01); }
        else { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xe8); bb_emit1(&c->code, 0x01); }
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x07); // mov [rdi],rax
        if (!post) emit_push_rax(c);
        return;
      }
      if (node->lhs && node->lhs->sym) {
        int off = node->lhs->sym->offset;
        emit_mov_rax_local(c, off);
        if (post) emit_push_rax(c);
        if (inc > 0) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xc0); bb_emit1(&c->code, 0x01); }
        else { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xe8); bb_emit1(&c->code, 0x01); }
        emit_mov_local_rax(c, off);
        if (!post) emit_push_rax(c);
        return;
      }
      if (node->lhs && node->lhs->name) {
        int goff = 0;
        if (lookup_global_info(node->lhs->name, &goff, NULL, NULL, NULL, NULL)) {
          emit_mov_rax_global(c, goff);
          if (post) emit_push_rax(c);
          if (inc > 0) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xc0); bb_emit1(&c->code, 0x01); }
          else { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xe8); bb_emit1(&c->code, 0x01); }
          emit_mov_global_rax(c, goff);
          if (!post) emit_push_rax(c);
          return;
        }
      }
      emit_push_imm32(c, 0);
      return;
    }
    case ND_COMMA:
      emit_expr_elf(c, node->lhs);
      emit_pop_rax(c);
      emit_expr_elf(c, node->rhs);
      return;
    default:
      break;
  }

  emit_expr_elf(c, node->lhs);
  emit_expr_elf(c, node->rhs);
  emit_pop_rdi(c);
  emit_pop_rax(c);

  switch (node->kind) {
    case ND_ADD: bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x01); bb_emit1(&c->code, 0xf8); break;
    case ND_SUB: bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x29); bb_emit1(&c->code, 0xf8); break;
    case ND_MUL: bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x0f); bb_emit1(&c->code, 0xaf); bb_emit1(&c->code, 0xc7); break;
    case ND_DIV: bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x99); bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0xf7); bb_emit1(&c->code, 0xff); break;
    case ND_EQ:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x94); emit_movzx_rax_al(c); break;
    case ND_NE:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x95); emit_movzx_rax_al(c); break;
    case ND_LT:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x9c); emit_movzx_rax_al(c); break;
    case ND_LE:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x9e); emit_movzx_rax_al(c); break;
    case ND_GT:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x9f); emit_movzx_rax_al(c); break;
    case ND_GE:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x9d); emit_movzx_rax_al(c); break;
    case ND_BITAND:
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x21); bb_emit1(&c->code, 0xf8);
      break;
    default:
      bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xc0); // xor eax,eax
      break;
  }

  emit_push_rax(c);
}

void emit_stmt_list_elf(ElfCtx *c, Node *node) {
  for (Node *s = node; s; s = s->next) emit_stmt_elf(c, s);
}

void emit_stmt_elf(ElfCtx *c, Node *node) {
  if (!node) return;
  switch (node->kind) {
    case ND_BLOCK:
      emit_stmt_list_elf(c, node->body);
      return;
    case ND_VAR:
      if (node->lhs) {
        emit_expr_elf(c, node->lhs);
        emit_pop_rax(c);
        int off = node->sym ? node->sym->offset : 8;
        emit_mov_local_rax(c, off);
      }
      return;
    case ND_IF: {
      int l_else = ctx_new_label(c);
      int l_end = ctx_new_label(c);
      emit_expr_elf(c, node->cond);
      emit_pop_rax(c);
      emit_cmp_rax_zero(c);
      emit_je_label(c, l_else);
      emit_stmt_elf(c, node->then);
      emit_jmp_label(c, l_end);
      ctx_place_label(c, l_else);
      if (node->els) emit_stmt_elf(c, node->els);
      ctx_place_label(c, l_end);
      return;
    }
    case ND_WHILE: {
      int l_begin = ctx_new_label(c);
      int l_end = ctx_new_label(c);
      c->break_labels[c->break_top++] = l_end;
      c->continue_labels[c->continue_top++] = l_begin;
      ctx_place_label(c, l_begin);
      emit_expr_elf(c, node->cond);
      emit_pop_rax(c);
      emit_cmp_rax_zero(c);
      emit_je_label(c, l_end);
      emit_stmt_elf(c, node->body);
      emit_jmp_label(c, l_begin);
      ctx_place_label(c, l_end);
      c->continue_top--;
      c->break_top--;
      return;
    }
    case ND_FOR: {
      int l_begin = ctx_new_label(c);
      int l_cont = ctx_new_label(c);
      int l_end = ctx_new_label(c);
      if (node->init) {
        if (node->init->kind == ND_BLOCK) emit_stmt_elf(c, node->init);
        else emit_expr_discard_elf(c, node->init);
      }
      c->break_labels[c->break_top++] = l_end;
      c->continue_labels[c->continue_top++] = l_cont;
      ctx_place_label(c, l_begin);
      if (node->cond) {
        emit_expr_elf(c, node->cond);
        emit_pop_rax(c);
        emit_cmp_rax_zero(c);
        emit_je_label(c, l_end);
      }
      emit_stmt_elf(c, node->body);
      ctx_place_label(c, l_cont);
      if (node->inc) emit_expr_discard_elf(c, node->inc);
      emit_jmp_label(c, l_begin);
      ctx_place_label(c, l_end);
      c->continue_top--;
      c->break_top--;
      return;
    }
    case ND_SWITCH: {
      int n = 0;
      for (Node *cse = node->body; cse; cse = cse->next) n++;
      if (n == 0) return;
      Node **cases = malloc(sizeof(Node *) * n);
      int *labels = malloc(sizeof(int) * n);
      int def_idx = -1;
      int i = 0;
      for (Node *cse = node->body; cse; cse = cse->next) {
        cases[i] = cse;
        labels[i] = ctx_new_label(c);
        if (cse->kind == ND_DEFAULT) def_idx = i;
        i++;
      }
      int l_end = ctx_new_label(c);
      c->break_labels[c->break_top++] = l_end;

      emit_expr_elf(c, node->cond);
      emit_pop_rax(c);
      for (i = 0; i < n; i++) {
        if (cases[i]->kind != ND_CASE) continue;
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x3d); bb_emit4(&c->code, cases[i]->val); // cmp rax, imm32
        emit_je_label(c, labels[i]);
      }
      if (def_idx >= 0) emit_jmp_label(c, labels[def_idx]);
      else emit_jmp_label(c, l_end);

      for (i = 0; i < n; i++) {
        ctx_place_label(c, labels[i]);
        emit_stmt_list_elf(c, cases[i]->body);
      }
      ctx_place_label(c, l_end);
      c->break_top--;
      free(labels);
      free(cases);
      return;
    }
    case ND_BREAK:
      if (c->break_top > 0) emit_jmp_label(c, c->break_labels[c->break_top - 1]);
      return;
    case ND_CONTINUE:
      if (c->continue_top > 0) emit_jmp_label(c, c->continue_labels[c->continue_top - 1]);
      return;
    case ND_RETURN:
      if (node->lhs) {
        emit_expr_elf(c, node->lhs);
        emit_pop_rax(c);
      } else {
        bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xc0); // xor eax,eax
      }
      emit_jmp_label(c, c->ret_label);
      return;
    default:
      emit_expr_discard_elf(c, node);
      return;
  }
}

int max_node_offset(Node *node);

int max_single_offset(Node *n) {
  if (!n) return 0;
  int m = n->sym ? n->sym->offset : 0;
  int t = 0;
  t = max_node_offset(n->lhs); if (t > m) m = t;
  t = max_node_offset(n->rhs); if (t > m) m = t;
  t = max_node_offset(n->cond); if (t > m) m = t;
  t = max_node_offset(n->then); if (t > m) m = t;
  t = max_node_offset(n->els); if (t > m) m = t;
  t = max_node_offset(n->body); if (t > m) m = t;
  t = max_node_offset(n->init); if (t > m) m = t;
  t = max_node_offset(n->inc); if (t > m) m = t;
  t = max_node_offset(n->args); if (t > m) m = t;
  return m;
}

int max_node_offset(Node *node) {
  int m = 0;
  for (Node *n = node; n; n = n->next) {
    int t = max_single_offset(n);
    if (t > m) m = t;
  }
  return m;
}

void emit_function_elf(ElfCtx *c, FnLabel *fn) {
  ctx_place_label(c, fn->label);
  bb_emit1(&c->code, 0x55);                               // push rbp
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xe5); // mov rbp,rsp

  int frame = max_node_offset(fn->fn->body);
  int pmax = max_node_offset(fn->fn->args);
  if (pmax > frame) frame = pmax;
  if (frame < 16) frame = 16;
  frame = (frame + 15) & ~15;
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x81); bb_emit1(&c->code, 0xec); bb_emit4(&c->code, frame); // sub rsp, imm32
  /* Zero local frame so `{0}`-style local init works in the minimal frontend. */
  bb_emit1(&c->code, 0x57); // push rdi
  bb_emit1(&c->code, 0x51); // push rcx
  bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xc0); // xor eax,eax
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8d); bb_emit1(&c->code, 0xbd); bb_emit4(&c->code, -frame); // lea rdi,[rbp-frame]
  bb_emit1(&c->code, 0xb9); bb_emit4(&c->code, frame / 8); // mov ecx, qword_count
  bb_emit1(&c->code, 0xf3); bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0xab); // rep stosq
  bb_emit1(&c->code, 0x59); // pop rcx
  bb_emit1(&c->code, 0x5f); // pop rdi

  int pi = 0;
  for (Node *p = fn->fn->args; p && pi < 6; p = p->next, pi++) {
    if (pi == 0) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xf8); }      // mov rax,rdi
    else if (pi == 1) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xf0); } // mov rax,rsi
    else if (pi == 2) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xd0); } // mov rax,rdx
    else if (pi == 3) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc8); } // mov rax,rcx
    else if (pi == 4) { bb_emit1(&c->code, 0x4c); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc0); } // mov rax,r8
    else if (pi == 5) { bb_emit1(&c->code, 0x4c); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc8); } // mov rax,r9
    if (p->sym) emit_mov_local_rax(c, p->sym->offset);
  }

  c->ret_label = ctx_new_label(c);
  emit_stmt_elf(c, fn->fn->body);
  bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xc0); // xor eax,eax default return
  ctx_place_label(c, c->ret_label);
  bb_emit1(&c->code, 0xc9); // leave
  bb_emit1(&c->code, 0xc3); // ret
}

int patch_fixups(ElfCtx *c) {
  for (int i = 0; i < c->nfixups; i++) {
    int lbl = c->fixups[i].label;
    if (lbl < 0 || lbl >= c->nlabels || !c->label_set[lbl]) return 1;
    int64_t from = (int64_t)c->fixups[i].at + 4;
    int64_t to = (int64_t)c->label_pos[lbl];
    int64_t rel = to - from;
    if (rel < INT32_MIN || rel > INT32_MAX) return 1;
    bb_patch4(&c->code, c->fixups[i].at, (int32_t)rel);
  }
  return 0;
}

#pragma pack(push, 1)
typedef struct {
  unsigned char e_ident[16];
  uint16_t e_type;
  uint16_t e_machine;
  uint32_t e_version;
  uint64_t e_entry;
  uint64_t e_phoff;
  uint64_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
} Ehdr64;

typedef struct {
  uint32_t p_type;
  uint32_t p_flags;
  uint64_t p_offset;
  uint64_t p_vaddr;
  uint64_t p_paddr;
  uint64_t p_filesz;
  uint64_t p_memsz;
  uint64_t p_align;
} Phdr64;
#pragma pack(pop)

int write_elf_exec_stdout(BinBuf *code, size_t entry_off) {
  const uint64_t base = 0x400000;
  const size_t code_off = 0x1000;
  size_t file_size = code_off + code->len;
  unsigned char *img = calloc(1, file_size);
  if (!img) return 1;

  Ehdr64 *eh = (Ehdr64 *)img;
  Phdr64 *ph = (Phdr64 *)(img + sizeof(Ehdr64));
  memcpy(eh->e_ident, "\x7f""ELF", 4);
  eh->e_ident[4] = 2; // 64-bit
  eh->e_ident[5] = 1; // little endian
  eh->e_ident[6] = 1; // version
  eh->e_type = 2;     // ET_EXEC
  eh->e_machine = 62; // x86_64
  eh->e_version = 1;
  eh->e_entry = base + code_off + entry_off;
  eh->e_phoff = sizeof(Ehdr64);
  eh->e_ehsize = sizeof(Ehdr64);
  eh->e_phentsize = sizeof(Phdr64);
  eh->e_phnum = 1;

  ph->p_type = 1;     // PT_LOAD
  ph->p_flags = 7;    // PF_R | PF_W | PF_X
  ph->p_offset = 0;
  ph->p_vaddr = base;
  ph->p_paddr = base;
  ph->p_filesz = file_size;
  ph->p_memsz = file_size;
  ph->p_align = 0x1000;

  memcpy(img + code_off, code->data, code->len);

  size_t off = 0;
  while (off < file_size) {
    long n = write(1, img + off, file_size - off);
    if (n <= 0) {
      free(img);
      return 1;
    }
    off += (size_t)n;
  }
  free(img);
  return 0;
}

int compile_to_elf() {
  char *source = read_stdin_source();
  if (!source) {
    eprintf("Error: cannot read stdin\n", 0, 0, 0, 0, 0, 0);
    return 1;
  }
  reset_string_literals();

  Node *funcs = parse_program_functions(source);
  int nfunc = 0;
  for (Node *f = funcs; f; f = f->next) {
    if (f->kind == ND_FUNC && f->name && f->body) nfunc++;
  }
  if (nfunc == 0) {
    free(source);
    reset_string_literals();
    eprintf("Error: no functions compiled from stdin\n", 0, 0, 0, 0, 0, 0);
    return 1;
  }

  ElfCtx c = {0};
  c.funcs = calloc(nfunc, sizeof(FnLabel));
  c.nfuncs = nfunc;
  c.globals_label = ctx_new_label(&c);

  int idx = 0;
  for (Node *f = funcs; f; f = f->next) {
    if (f->kind == ND_FUNC && f->name && f->body) {
      c.funcs[idx].name = f->name;
      c.funcs[idx].fn = f;
      c.funcs[idx].label = ctx_new_label(&c);
      idx++;
    }
  }

  int main_lbl = find_func_label(&c, "main");
  if (main_lbl < 0) {
    free(c.funcs);
    free(source);
    reset_string_literals();
    eprintf("Error: main() not compiled from stdin\n", 0, 0, 0, 0, 0, 0);
    return 1;
  }

  int start_lbl = ctx_new_label(&c);
  ctx_place_label(&c, start_lbl);
  bb_emit1(&c.code, 0x48); bb_emit1(&c.code, 0x8b); bb_emit1(&c.code, 0x3c); bb_emit1(&c.code, 0x24);          // mov rdi,[rsp]
  bb_emit1(&c.code, 0x48); bb_emit1(&c.code, 0x8d); bb_emit1(&c.code, 0x74); bb_emit1(&c.code, 0x24); bb_emit1(&c.code, 0x08); // lea rsi,[rsp+8]
  emit_call_label(&c, main_lbl);                             // call main
  bb_emit1(&c.code, 0x89); bb_emit1(&c.code, 0xc7);         // mov edi,eax
  bb_emit1(&c.code, 0xb8); bb_emit4(&c.code, 60);           // mov eax,60
  bb_emit1(&c.code, 0x0f); bb_emit1(&c.code, 0x05);         // syscall

  for (int i = 0; i < c.nfuncs; i++) emit_function_elf(&c, &c.funcs[i]);
  emit_string_literals(&c);
  ctx_place_label(&c, c.globals_label);
  for (int i = 0; i < global_storage_size(); i++) bb_emit1(&c.code, 0);

  if (patch_fixups(&c) != 0) {
    eprintf("Error: unresolved labels in codegen\n", 0, 0, 0, 0, 0, 0);
    free(c.code.data); free(c.label_pos); free(c.label_set); free(c.fixups); free(c.funcs); free(source);
    reset_string_literals();
    return 1;
  }

  int rc = write_elf_exec_stdout(&c.code, c.label_pos[start_lbl]);
  free(c.code.data); free(c.label_pos); free(c.label_set); free(c.fixups); free(c.funcs); free(source);
  reset_string_literals();
  if (rc != 0) {
    eprintf("Error: cannot write ELF to stdout\n", 0, 0, 0, 0, 0, 0);
    return 1;
  }
  return 0;
}
