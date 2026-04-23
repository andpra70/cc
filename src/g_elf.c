#include "config.h"

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
  int sym;
  size_t at;
} ExtRelFix;

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
  int *break_labels;
  int break_top;
  int *continue_labels;
  int continue_top;
  int ret_label;
  int globals_label;
  StrLit *str_lits;
  int n_str_lits;
  char **ext_syms;
  int n_ext_syms;
  int cap_ext_syms;
  ExtRelFix *ext_fixups;
  int n_ext_fixups;
  int cap_ext_fixups;
  char **user_label_names;
  int *user_label_ids;
  int n_user_labels;
  int cap_user_labels;
  int emit_rel_obj;
} ElfCtx;

static int elf_cur_fn_fixed_args = 0;
static int elf_cur_fn_is_variadic = 0;
static int elf_cur_fn_arg_home_base = 8;
static const char *elf_cur_fn_name = 0;
static int elf_debug_enabled = 0;
static const char *elf_debug_source_path = 0;
static const char *elf_debug_comp_dir = ".";

void set_elf_debug_options(int enabled, const char *source_path, const char *comp_dir) {
  elf_debug_enabled = enabled;
  elf_debug_source_path = source_path;
  elf_debug_comp_dir = comp_dir ? comp_dir : ".";
}

void bb_reserve(BinBuf *b, size_t add) {
  if (b->len + add <= b->cap) return;
  size_t ncap = b->cap ? b->cap : CC_CFG_ELF_CODE_INIT;
  while (ncap < b->len + add) ncap *= 2;
  b->data = realloc(b->data, ncap);
  b->cap = ncap;
}

void bb_emit1(BinBuf *b, unsigned char v) {
  bb_reserve(b, 1);
  b->data[b->len] = v;
  b->len = b->len + 1;
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
  int ncap = c->caplabels ? c->caplabels : CC_CFG_ELF_LABELS_INIT;
  while (ncap < need) ncap *= 2;
  c->label_pos = realloc(c->label_pos, sizeof(size_t) * ncap);
  c->label_set = realloc(c->label_set, sizeof(unsigned char) * ncap);
  memset(c->label_set + c->caplabels, 0, ncap - c->caplabels);
  c->caplabels = ncap;
}

int ctx_new_label(ElfCtx *c) {
  int id = c->nlabels;
  c->nlabels = c->nlabels + 1;
  ctx_ensure_labels(c, c->nlabels);
  c->label_set[id] = 0;
  c->label_pos[id] = 0;
  return id;
}

void ctx_place_label(ElfCtx *c, int label) {
  unsigned char *ls;
  size_t *lp;
  if (!c) return;
  ls = c->label_set;
  lp = c->label_pos;
  if (!ls || !lp) return;
  ls[label] = 1;
  lp[label] = c->code.len;
}

void ctx_add_fixup(ElfCtx *c, int label, size_t at) {
  if (c->nfixups == c->capfixups) {
    int ncap = c->capfixups ? c->capfixups * 2 : CC_CFG_ELF_FIXUPS_INIT;
    c->fixups = realloc(c->fixups, sizeof(RelFix) * ncap);
    c->capfixups = ncap;
  }
  c->fixups[c->nfixups].label = label;
  c->fixups[c->nfixups].at = at;
  c->nfixups = c->nfixups + 1;
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

int find_ext_sym(ElfCtx *c, const char *name) {
  int i = 0;
  while (i < c->n_ext_syms) {
    if (!strcmp(c->ext_syms[i], name)) return i;
    i++;
  }
  return -1;
}

int get_ext_sym(ElfCtx *c, const char *name) {
  int idx = find_ext_sym(c, name);
  if (idx >= 0) return idx;
  if (c->n_ext_syms == c->cap_ext_syms) {
    int ncap = c->cap_ext_syms ? c->cap_ext_syms * 2 : 16;
    char **ns = realloc(c->ext_syms, sizeof(char *) * ncap);
    if (!ns) {
      eprintf("Error: out of memory for external symbols\n", 0, 0, 0, 0);
      exit(1);
    }
    c->ext_syms = ns;
    c->cap_ext_syms = ncap;
  }
  c->ext_syms[c->n_ext_syms] = (char *)name;
  c->n_ext_syms = c->n_ext_syms + 1;
  return c->n_ext_syms - 1;
}

void add_ext_fixup(ElfCtx *c, int sym, size_t at) {
  if (c->n_ext_fixups == c->cap_ext_fixups) {
    int ncap = c->cap_ext_fixups ? c->cap_ext_fixups * 2 : 16;
    ExtRelFix *nf = realloc(c->ext_fixups, sizeof(ExtRelFix) * ncap);
    if (!nf) {
      eprintf("Error: out of memory for external relocations\n", 0, 0, 0, 0);
      exit(1);
    }
    c->ext_fixups = nf;
    c->cap_ext_fixups = ncap;
  }
  c->ext_fixups[c->n_ext_fixups].sym = sym;
  c->ext_fixups[c->n_ext_fixups].at = at;
  c->n_ext_fixups = c->n_ext_fixups + 1;
}

void emit_call_extern(ElfCtx *c, const char *name) {
  int sym = get_ext_sym(c, name);
  bb_emit1(&c->code, 0xe8);
  {
    size_t at = c->code.len;
    bb_emit4(&c->code, 0);
    add_ext_fixup(c, sym, at);
  }
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
  // String literal table is now per-compilation in ElfCtx.
}

int get_string_label(ElfCtx *c, const char *s) {
  int i = 0;
  while (i < c->n_str_lits) {
    if (!strcmp(c->str_lits[i].text, s)) return c->str_lits[i].label;
    i++;
  }
  if (c->n_str_lits >= CC_CFG_ELF_STR_LIT_MAX) {
    eprintf("Error: too many string literals (max=%d)\n", CC_CFG_ELF_STR_LIT_MAX, 0, 0, 0);
    exit(1);
  }
  {
    int idx = c->n_str_lits;
    c->str_lits[idx].text = (char *)s;
    c->str_lits[idx].label = ctx_new_label(c);
    c->n_str_lits = c->n_str_lits + 1;
    return c->str_lits[idx].label;
  }
}

void emit_push_string_addr(ElfCtx *c, const char *s) {
  int lbl = get_string_label(c, s);
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8d); bb_emit1(&c->code, 0x05); // lea rax,[rip+rel32]
  emit_rel32_fixup(c, lbl);
  emit_push_rax(c);
}

void emit_string_literals(ElfCtx *c) {
  int i = 0;
  while (i < c->n_str_lits) {
    const char *s = c->str_lits[i].text;
    ctx_place_label(c, c->str_lits[i].label);
    while (*s) bb_emit1(&c->code, (unsigned char)*s++);
    bb_emit1(&c->code, 0);
    i++;
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
  int i = 0;
  while (i < c->nfuncs) {
    if (!strcmp(c->funcs[i].name, name)) return c->funcs[i].label;
    i++;
  }
  return -1;
}

int get_user_label(ElfCtx *c, const char *name) {
  int i = 0;
  while (i < c->n_user_labels) {
    if (!strcmp(c->user_label_names[i], name)) return c->user_label_ids[i];
    i++;
  }
  if (c->n_user_labels == c->cap_user_labels) {
    int ncap = c->cap_user_labels ? c->cap_user_labels * 2 : 16;
    char **nn = realloc(c->user_label_names, sizeof(char *) * ncap);
    int *ni = realloc(c->user_label_ids, sizeof(int) * ncap);
    if (!nn || !ni) {
      eprintf("Error: out of memory for labels\n", 0, 0, 0, 0);
      exit(1);
    }
    c->user_label_names = nn;
    c->user_label_ids = ni;
    c->cap_user_labels = ncap;
  }
  c->user_label_names[c->n_user_labels] = (char *)name;
  c->user_label_ids[c->n_user_labels] = ctx_new_label(c);
  c->n_user_labels = c->n_user_labels + 1;
  return c->user_label_ids[c->n_user_labels - 1];
}

int emit_builtin_syscall_fallback(ElfCtx *c, const char *name) {
  int sysno = -1;
  int needs_r10 = 0;
  if (!strcmp(name, "read")) sysno = 0;
  else if (!strcmp(name, "write")) sysno = 1;
  else if (!strcmp(name, "close")) sysno = 3;
  else if (!strcmp(name, "mmap")) { sysno = 9; needs_r10 = 1; }
  else if (!strcmp(name, "munmap")) sysno = 11;
  else if (!strcmp(name, "open")) sysno = 2;
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
      int flbl = -1;
      if (node->sym) {
        if (node->sym->is_array) emit_lea_rax_local(c, node->sym->offset);
        else emit_mov_rax_local(c, node->sym->offset);
      } else if (node->name && lookup_global_info(node->name, &goff, NULL, NULL, &garr, NULL)) {
        if (garr) emit_lea_rax_global(c, goff);
        else emit_mov_rax_global(c, goff);
      } else if (node->name && (flbl = find_func_label(c, node->name)) >= 0) {
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8d); bb_emit1(&c->code, 0x05); /* lea rax,[rip+rel32] */
        emit_rel32_fixup(c, flbl);
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
      const char *direct = (node->name && *node->name) ? node->name : NULL;
      if (direct && (!strcmp(direct, "va_start") || !strcmp(direct, "__builtin_va_start"))) {
        Node *ap_arg = node->args;
        if (ap_arg && ap_arg->kind == ND_ID && ap_arg->sym) {
          emit_lea_rax_local(c, ap_arg->sym->offset);
          bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc7); // mov rdi,rax
        } else if (ap_arg) {
          emit_expr_elf(c, ap_arg);
          emit_pop_rdi(c);
        } else {
          bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xff); // xor edi,edi
        }
        if (elf_cur_fn_is_variadic) {
          if (elf_cur_fn_fixed_args < 6) {
            int off = elf_cur_fn_arg_home_base + elf_cur_fn_fixed_args * 8;
            emit_lea_rax_local(c, off);
          } else {
            int disp = 16 + (elf_cur_fn_fixed_args - 6) * 8;
            bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8d); bb_emit1(&c->code, 0x85); bb_emit4(&c->code, disp); // lea rax,[rbp+disp]
          }
        } else {
          bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xc0); // xor eax,eax
        }
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x07); // mov [rdi],rax
        emit_push_imm32(c, 0);
        return;
      }
      if (direct && (!strcmp(direct, "va_arg") || !strcmp(direct, "__builtin_va_arg"))) {
        Node *ap_arg = node->args;
        if (ap_arg && ap_arg->kind == ND_ID && ap_arg->sym) {
          emit_lea_rax_local(c, ap_arg->sym->offset);
          bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc7); // mov rdi,rax
        } else if (ap_arg) {
          emit_expr_elf(c, ap_arg);
          emit_pop_rdi(c);
        } else {
          bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xff); // xor edi,edi
        }
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8b); bb_emit1(&c->code, 0x07); // mov rax,[rdi]
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc1); // mov rcx,rax
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xe8); bb_emit1(&c->code, 0x08); // sub rax,8
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x07); // mov [rdi],rax
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8b); bb_emit1(&c->code, 0x01); // mov rax,[rcx]
        emit_push_rax(c);
        return;
      }
      if (direct && (!strcmp(direct, "va_end") || !strcmp(direct, "__builtin_va_end"))) {
        Node *ap_arg = node->args;
        if (ap_arg && ap_arg->kind == ND_ID && ap_arg->sym) {
          emit_lea_rax_local(c, ap_arg->sym->offset);
          bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc7); // mov rdi,rax
        } else if (ap_arg) {
          emit_expr_elf(c, ap_arg);
          emit_pop_rdi(c);
        } else {
          bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xff); // xor edi,edi
        }
        bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xc0); // xor eax,eax
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x07); // mov [rdi],rax
        emit_push_imm32(c, 0);
        return;
      }
      if (direct && (!strcmp(direct, "va_copy") || !strcmp(direct, "__builtin_va_copy"))) {
        Node *dst_arg = node->args;
        Node *src_arg = dst_arg ? dst_arg->next : NULL;
        if (dst_arg && dst_arg->kind == ND_ID && dst_arg->sym) {
          emit_lea_rax_local(c, dst_arg->sym->offset);
          bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc7); // mov rdi,rax
        } else if (dst_arg) {
          emit_expr_elf(c, dst_arg);
          emit_pop_rdi(c);
        } else {
          bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xff); // xor edi,edi
        }
        if (src_arg && src_arg->kind == ND_ID && src_arg->sym) {
          emit_mov_rax_local(c, src_arg->sym->offset);
        } else if (src_arg) {
          emit_expr_elf(c, src_arg);
          emit_pop_rax(c);
        } else {
          bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xc0); // xor eax,eax
        }
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x07); // mov [rdi],rax
        emit_push_imm32(c, 0);
        return;
      }

      int argc = 0;
      Node *a = node->args;
      while (a) {
        if (argc > CC_CFG_CALL_ARG_GUARD_MAX) {
          eprintf("Error: too many/cyclic args in call %s\n", (long)(direct ? direct : "(indirect)"), 0, 0, 0);
          exit(1);
        }
        emit_expr_elf(c, a);
        argc++;
        a = a->next;
      }
      int j = argc - 1;
      while (j >= 0) {
        if (j == 0) bb_emit1(&c->code, 0x5f);          // pop rdi
        else if (j == 1) bb_emit1(&c->code, 0x5e);     // pop rsi
        else if (j == 2) bb_emit1(&c->code, 0x5a);     // pop rdx
        else if (j == 3) bb_emit1(&c->code, 0x59);     // pop rcx
        else if (j == 4) { bb_emit1(&c->code, 0x41); bb_emit1(&c->code, 0x58); } // pop r8
        else if (j == 5) { bb_emit1(&c->code, 0x41); bb_emit1(&c->code, 0x59); } // pop r9
        else bb_emit1(&c->code, 0x58);                 // pop rax discard
        j--;
      }
      if (!direct) {
        if (node->lhs && node->lhs->kind == ND_DEREF && node->lhs->lhs && node->lhs->lhs->ptr_level > 0)
          emit_expr_elf(c, node->lhs->lhs);
        else
          emit_expr_elf(c, node->lhs);
        emit_pop_rax(c);
        bb_emit1(&c->code, 0xff); bb_emit1(&c->code, 0xd0); /* call rax */
      } else {
        int lbl = find_func_label(c, direct);
        if (c->emit_rel_obj) {
          if (lbl >= 0) emit_call_label(c, lbl);
          else emit_call_extern(c, direct);
        } else {
          const char *abi = kernel_abi_symbol(direct);
          int abi_call_lbl = find_func_label(c, "kernel_abi_call");
          if (lbl < 0 && abi && strcmp(abi, direct)) {
            /* Avoid mapping builtins back to the wrapper currently being emitted. */
            if (!elf_cur_fn_name || strcmp(elf_cur_fn_name, abi)) lbl = find_func_label(c, abi);
          }
          if (lbl >= 0) emit_call_label(c, lbl);
          else if (emit_builtin_syscall_fallback(c, direct)) {}
          else if (abi_call_lbl >= 0 && (!elf_cur_fn_name || strcmp(elf_cur_fn_name, "kernel_abi_call"))) {
            const char *dyn_name = (abi && *abi) ? abi : direct;
            /* Build args[0..5] on stack then call kernel_abi_call(name, args, argc). */
            bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xec); bb_emit1(&c->code, 0x30); /* sub rsp,48 */
            bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x3c); bb_emit1(&c->code, 0x24);             /* mov [rsp],rdi */
            bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x74); bb_emit1(&c->code, 0x24); bb_emit1(&c->code, 0x08); /* mov [rsp+8],rsi */
            bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x54); bb_emit1(&c->code, 0x24); bb_emit1(&c->code, 0x10); /* mov [rsp+16],rdx */
            bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x4c); bb_emit1(&c->code, 0x24); bb_emit1(&c->code, 0x18); /* mov [rsp+24],rcx */
            bb_emit1(&c->code, 0x4c); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x44); bb_emit1(&c->code, 0x24); bb_emit1(&c->code, 0x20); /* mov [rsp+32],r8 */
            bb_emit1(&c->code, 0x4c); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x4c); bb_emit1(&c->code, 0x24); bb_emit1(&c->code, 0x28); /* mov [rsp+40],r9 */
            emit_push_string_addr(c, dyn_name);
            emit_pop_rdi(c);                                                                                                         /* rdi=name */
            bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8d); bb_emit1(&c->code, 0x34); bb_emit1(&c->code, 0x24);               /* lea rsi,[rsp] */
            bb_emit1(&c->code, 0xba); bb_emit4(&c->code, argc);                                                                      /* mov edx,argc */
            emit_call_label(c, abi_call_lbl);
            bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xc4); bb_emit1(&c->code, 0x30);               /* add rsp,48 */
          } else { bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xc0); } /* xor eax,eax */
        }
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
      int step = node->lhs ? pointee_size(node->lhs->type_name, node->lhs->ptr_level) : 1;
      if (node->lhs && node->lhs->ptr_level <= 0) step = 1;
      if (step < 1) step = 1;
      if (node->lhs && node->lhs->kind == ND_DEREF) {
        emit_expr_elf(c, node->lhs->lhs);
        emit_pop_rdi(c);
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x8b); bb_emit1(&c->code, 0x07); // mov rax,[rdi]
        if (post) emit_push_rax(c);
        if (inc > 0) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xc0); bb_emit1(&c->code, step); }
        else { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xe8); bb_emit1(&c->code, step); }
        bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0x07); // mov [rdi],rax
        if (!post) emit_push_rax(c);
        return;
      }
      if (node->lhs && node->lhs->sym) {
        int off = node->lhs->sym->offset;
        emit_mov_rax_local(c, off);
        if (post) emit_push_rax(c);
        if (inc > 0) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xc0); bb_emit1(&c->code, step); }
        else { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xe8); bb_emit1(&c->code, step); }
        emit_mov_local_rax(c, off);
        if (!post) emit_push_rax(c);
        return;
      }
      if (node->lhs && node->lhs->name) {
        int goff = 0;
        if (lookup_global_info(node->lhs->name, &goff, NULL, NULL, NULL, NULL)) {
          emit_mov_rax_global(c, goff);
          if (post) emit_push_rax(c);
          if (inc > 0) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xc0); bb_emit1(&c->code, step); }
          else { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xe8); bb_emit1(&c->code, step); }
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
    case ND_SUB:
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x29); bb_emit1(&c->code, 0xf8);
      if (node->lhs && node->rhs && node->lhs->ptr_level > 0 && node->rhs->ptr_level > 0) {
        int step = pointee_size(node->lhs->type_name, node->lhs->ptr_level);
        if (step > 1) {
          bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0xc7); bb_emit1(&c->code, 0xc1); bb_emit4(&c->code, step); // mov rcx,step
          bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x99); // cqo
          bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0xf7); bb_emit1(&c->code, 0xf9); // idiv rcx
        }
      }
      break;
    case ND_MUL: bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x0f); bb_emit1(&c->code, 0xaf); bb_emit1(&c->code, 0xc7); break;
    case ND_DIV: bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x99); bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0xf7); bb_emit1(&c->code, 0xff); break;
    case ND_MOD:
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x99);
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0xf7); bb_emit1(&c->code, 0xff);
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xd0);
      break;
    case ND_POW: {
      int l_loop = ctx_new_label(c);
      int l_end = ctx_new_label(c);
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0xc7); bb_emit1(&c->code, 0xc1); bb_emit4(&c->code, 1);
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc3);
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xfa);
      ctx_place_label(c, l_loop);
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xfa); bb_emit1(&c->code, 0x00);
      emit_je_label(c, l_end);
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x0f); bb_emit1(&c->code, 0xaf); bb_emit1(&c->code, 0xcb);
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x83); bb_emit1(&c->code, 0xea); bb_emit1(&c->code, 0x01);
      emit_jmp_label(c, l_loop);
      ctx_place_label(c, l_end);
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc8);
      break;
    }
    case ND_EQ:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x94); emit_movzx_rax_al(c); break;
    case ND_NE:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x95); emit_movzx_rax_al(c); break;
    case ND_LT:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x9c); emit_movzx_rax_al(c); break;
    case ND_LE:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x9e); emit_movzx_rax_al(c); break;
    case ND_GT:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x9f); emit_movzx_rax_al(c); break;
    case ND_GE:  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x39); bb_emit1(&c->code, 0xf8); emit_setcc_al(c, 0x9d); emit_movzx_rax_al(c); break;
    case ND_BITAND:
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x21); bb_emit1(&c->code, 0xf8);
      break;
    case ND_BITOR:
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x09); bb_emit1(&c->code, 0xf8);
      break;
    case ND_BITXOR:
      bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xf8);
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
      c->break_labels[c->break_top] = l_end;
      c->break_top = c->break_top + 1;
      c->continue_labels[c->continue_top] = l_begin;
      c->continue_top = c->continue_top + 1;
      ctx_place_label(c, l_begin);
      emit_expr_elf(c, node->cond);
      emit_pop_rax(c);
      emit_cmp_rax_zero(c);
      emit_je_label(c, l_end);
      emit_stmt_elf(c, node->body);
      emit_jmp_label(c, l_begin);
      ctx_place_label(c, l_end);
      c->continue_top = c->continue_top - 1;
      c->break_top = c->break_top - 1;
      return;
    }
    case ND_DO: {
      int l_begin = ctx_new_label(c);
      int l_cont = ctx_new_label(c);
      int l_end = ctx_new_label(c);
      c->break_labels[c->break_top] = l_end;
      c->break_top = c->break_top + 1;
      c->continue_labels[c->continue_top] = l_cont;
      c->continue_top = c->continue_top + 1;
      ctx_place_label(c, l_begin);
      emit_stmt_elf(c, node->body);
      ctx_place_label(c, l_cont);
      emit_expr_elf(c, node->cond);
      emit_pop_rax(c);
      emit_cmp_rax_zero(c);
      bb_emit1(&c->code, 0x0f); bb_emit1(&c->code, 0x85);
      emit_rel32_fixup(c, l_begin);
      ctx_place_label(c, l_end);
      c->continue_top = c->continue_top - 1;
      c->break_top = c->break_top - 1;
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
      c->break_labels[c->break_top] = l_end;
      c->break_top = c->break_top + 1;
      c->continue_labels[c->continue_top] = l_cont;
      c->continue_top = c->continue_top + 1;
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
      c->continue_top = c->continue_top - 1;
      c->break_top = c->break_top - 1;
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
      c->break_labels[c->break_top] = l_end;
      c->break_top = c->break_top + 1;

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
      c->break_top = c->break_top - 1;
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
    case ND_GOTO:
      if (node->name) emit_jmp_label(c, get_user_label(c, node->name));
      return;
    case ND_LABEL:
      if (node->name) ctx_place_label(c, get_user_label(c, node->name));
      emit_stmt_elf(c, node->body);
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
  elf_cur_fn_fixed_args = (fn && fn->fn) ? fn->fn->fixed_args : 0;
  elf_cur_fn_is_variadic = (fn && fn->fn) ? fn->fn->is_variadic : 0;
  elf_cur_fn_name = (fn && fn->name) ? fn->name : 0;
  c->n_user_labels = 0;
  ctx_place_label(c, fn->label);
  bb_emit1(&c->code, 0x55);                               // push rbp
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xe5); // mov rbp,rsp

  int frame = fn->fn ? fn->fn->val : 0;
  if (parse_verbose) eprintf("[v] elf: emit_function_elf frame0=%d\n", frame, 0, 0, 0);
  if (frame < 0) frame = 0;
  /*
   * We always spill ABI argument registers to [rbp-8..rbp-48].
   * Reserve a dedicated arg-home area beyond locals so we never overlap
   * user local variables (e.g. va_list objects).
   */
  if (frame < 64) frame = 64;
  {
    int local_frame = frame;
    frame = local_frame + 64;
    frame = (frame + 15) & ~15;
    elf_cur_fn_arg_home_base = local_frame + 8;
  }
  frame = (frame + 15) & ~15;
  if (parse_verbose) eprintf("[v] elf: fn=%s frame=%d fixed=%d var=%d\n",
                              (long)(fn->name ? fn->name : "?"), frame,
                              elf_cur_fn_fixed_args, elf_cur_fn_is_variadic);
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

  /* Always spill ABI arg registers into canonical home slots.
   * This keeps argc/argv-style accesses stable even when param metadata
   * is partially missing in the minimal frontend.
   */
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xf8); // mov rax,rdi
  emit_mov_local_rax(c, elf_cur_fn_arg_home_base + 0);
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xf0); // mov rax,rsi
  emit_mov_local_rax(c, elf_cur_fn_arg_home_base + 8);
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xd0); // mov rax,rdx
  emit_mov_local_rax(c, elf_cur_fn_arg_home_base + 16);
  bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc8); // mov rax,rcx
  emit_mov_local_rax(c, elf_cur_fn_arg_home_base + 24);
  bb_emit1(&c->code, 0x4c); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc0); // mov rax,r8
  emit_mov_local_rax(c, elf_cur_fn_arg_home_base + 32);
  bb_emit1(&c->code, 0x4c); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc8); // mov rax,r9
  emit_mov_local_rax(c, elf_cur_fn_arg_home_base + 40);

  int pi = 0;
  Node *p = fn->fn->args;
  while (p && pi < 6) {
    if (pi == 0) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xf8); }      // mov rax,rdi
    else if (pi == 1) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xf0); } // mov rax,rsi
    else if (pi == 2) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xd0); } // mov rax,rdx
    else if (pi == 3) { bb_emit1(&c->code, 0x48); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc8); } // mov rax,rcx
    else if (pi == 4) { bb_emit1(&c->code, 0x4c); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc0); } // mov rax,r8
    else if (pi == 5) { bb_emit1(&c->code, 0x4c); bb_emit1(&c->code, 0x89); bb_emit1(&c->code, 0xc8); } // mov rax,r9
    if (p->sym) emit_mov_local_rax(c, p->sym->offset);
    p = p->next;
    pi++;
  }
  c->ret_label = ctx_new_label(c);
  emit_stmt_elf(c, fn->fn->body);
  bb_emit1(&c->code, 0x31); bb_emit1(&c->code, 0xc0); // xor eax,eax default return
  ctx_place_label(c, c->ret_label);
  bb_emit1(&c->code, 0xc9); // leave
  bb_emit1(&c->code, 0xc3); // ret
  elf_cur_fn_fixed_args = 0;
  elf_cur_fn_is_variadic = 0;
  elf_cur_fn_arg_home_base = 8;
  elf_cur_fn_name = 0;
}

int patch_fixups(ElfCtx *c) {
  int i = 0;
  while (i < c->nfixups) {
    int lbl = c->fixups[i].label;
    if (lbl < 0 || lbl >= c->nlabels || !c->label_set[lbl]) return 1;
    int64_t from = (int64_t)c->fixups[i].at + 4;
    int64_t to = (int64_t)c->label_pos[lbl];
    int64_t rel = to - from;
    if (rel < INT32_MIN || rel > INT32_MAX) return 1;
    bb_patch4(&c->code, c->fixups[i].at, (int32_t)rel);
    i++;
  }
  return 0;
}

void put_u16_le(unsigned char *p, uint16_t v) {
  p[0] = (unsigned char)(v & 0xff);
  p[1] = (unsigned char)((v / 256) & 0xff);
}

void put_u32_le(unsigned char *p, uint32_t v) {
  int i = 0;
  uint32_t x = v;
  while (i < 4) {
    p[i] = (unsigned char)(x & 0xff);
    x = x / 256;
    i++;
  }
}

void put_u64_le(unsigned char *p, uint64_t v) {
  int i = 0;
  uint64_t x = v;
  while (i < 8) {
    p[i] = (unsigned char)(x & 0xff);
    x = x / 256;
    i++;
  }
}

static size_t align_up_sz(size_t v, size_t a) {
  if (a == 0) return v;
  return (v + (a - 1)) & ~(a - 1);
}

size_t align_up_size(size_t x, size_t a) {
  if (a == 0) return x;
  return (x + a - 1) & ~(a - 1);
}

void put_sym64(unsigned char *p, uint32_t st_name, unsigned char st_info, unsigned char st_other,
               uint16_t st_shndx, uint64_t st_value, uint64_t st_size) {
  put_u32_le(p + 0, st_name);
  p[4] = st_info;
  p[5] = st_other;
  put_u16_le(p + 6, st_shndx);
  put_u64_le(p + 8, st_value);
  put_u64_le(p + 16, st_size);
}

void copy_cstr(unsigned char *dst, size_t *off, const char *s) {
  while (*s) dst[(*off)++] = (unsigned char)*s++;
  dst[(*off)++] = 0;
}

uint32_t shstr_name_off(const char *tab, size_t size, const char *name) {
  size_t i = 0;
  while (i < size) {
    if (tab[i] && !strcmp(tab + i, name)) return (uint32_t)i;
    while (i < size && tab[i]) i++;
    i++;
  }
  return 0;
}

unsigned char *build_debug_info(const char *name, const char *comp_dir, size_t *out_size) {
  const char *producer = "c99";
  size_t producer_len = strlen(producer) + 1;
  size_t name_len = strlen(name) + 1;
  size_t comp_dir_len = strlen(comp_dir) + 1;
  size_t die_size = 1 + producer_len + 2 + name_len + comp_dir_len;
  size_t size = 4 + 2 + 4 + 1 + die_size;
  unsigned char *buf = calloc(1, size);
  size_t off = 0;
  if (!buf) return NULL;

  put_u32_le(buf + off, (uint32_t)(size - 4)); off += 4; /* unit_length */
  put_u16_le(buf + off, 4); off += 2;                    /* DWARF v4 */
  put_u32_le(buf + off, 0); off += 4;                    /* .debug_abbrev offset */
  buf[off++] = 8;                                        /* address size */
  buf[off++] = 1;                                        /* abbrev code: compile unit */
  memcpy(buf + off, producer, producer_len); off += producer_len;
  put_u16_le(buf + off, 0x000c); off += 2;               /* DW_LANG_C99 */
  memcpy(buf + off, name, name_len); off += name_len;
  memcpy(buf + off, comp_dir, comp_dir_len); off += comp_dir_len;

  *out_size = off;
  return buf;
}

int write_elf_rel_obj_fd(ElfCtx *c, int out_fd) {
  static const char shstrtab[] = "\0.text\0.shstrtab\0.strtab\0.symtab\0.rela.text\0.debug_abbrev\0.debug_info\0.debug_str\0";
  static const unsigned char debug_abbrev[] = {
    1, 0x11, 0,          /* compile_unit, no children */
    0x25, 0x08,          /* DW_AT_producer, DW_FORM_string */
    0x13, 0x05,          /* DW_AT_language, DW_FORM_data2 */
    0x03, 0x08,          /* DW_AT_name, DW_FORM_string */
    0x1b, 0x08,          /* DW_AT_comp_dir, DW_FORM_string */
    0, 0,                /* end attributes */
    0                    /* end abbrev table */
  };
  const uint32_t sh_name_text = shstr_name_off(shstrtab, sizeof(shstrtab), ".text");
  const uint32_t sh_name_shstrtab = shstr_name_off(shstrtab, sizeof(shstrtab), ".shstrtab");
  const uint32_t sh_name_strtab = shstr_name_off(shstrtab, sizeof(shstrtab), ".strtab");
  const uint32_t sh_name_symtab = shstr_name_off(shstrtab, sizeof(shstrtab), ".symtab");
  const uint32_t sh_name_rela_text = shstr_name_off(shstrtab, sizeof(shstrtab), ".rela.text");
  const uint32_t sh_name_debug_abbrev = shstr_name_off(shstrtab, sizeof(shstrtab), ".debug_abbrev");
  const uint32_t sh_name_debug_info = shstr_name_off(shstrtab, sizeof(shstrtab), ".debug_info");
  const uint32_t sh_name_debug_str = shstr_name_off(shstrtab, sizeof(shstrtab), ".debug_str");
  const size_t shstrtab_size = sizeof(shstrtab);
  const size_t ehdr_size = 64;
  const size_t shent_size = 64;
  const int debug_enabled = elf_debug_enabled ? 1 : 0;
  const int shnum = debug_enabled ? 9 : 6;
  int nsyms = 2 + c->nfuncs + c->n_ext_syms;
  size_t symtab_size = (size_t)nsyms * 24;
  size_t rela_size = (size_t)c->n_ext_fixups * 24;
  size_t debug_abbrev_size = sizeof(debug_abbrev);
  size_t debug_info_size = 0;
  size_t debug_str_size = debug_enabled ? 1 : 0;
  size_t strtab_size = 1;
  size_t text_off = ehdr_size;
  size_t shstr_off;
  size_t strtab_off;
  size_t symtab_off;
  size_t rela_off;
  size_t debug_abbrev_off = 0;
  size_t debug_info_off = 0;
  size_t debug_str_off = 0;
  size_t shoff;
  size_t file_size;
  unsigned char *img;
  unsigned char *debug_info = NULL;
  size_t off;
  int *name_offs_def;
  int *name_offs_ext;
  int i;

  if (c->nfuncs < 0) return 1;
  for (i = 0; i < c->nfuncs; i++) {
    const char *name = c->funcs[i].name ? c->funcs[i].name : "";
    strtab_size += strlen(name) + 1;
  }
  for (i = 0; i < c->n_ext_syms; i++) {
    const char *name = c->ext_syms[i] ? c->ext_syms[i] : "";
    strtab_size += strlen(name) + 1;
  }
  if (debug_enabled) {
    const char *debug_name = elf_debug_source_path ? elf_debug_source_path : "";
    const char *debug_dir = elf_debug_comp_dir ? elf_debug_comp_dir : ".";
    debug_info = build_debug_info(debug_name, debug_dir, &debug_info_size);
    if (!debug_info) return 1;
  }

  shstr_off = align_up_size(text_off + c->code.len, 1);
  strtab_off = align_up_size(shstr_off + shstrtab_size, 1);
  symtab_off = align_up_size(strtab_off + strtab_size, 8);
  rela_off = align_up_size(symtab_off + symtab_size, 8);
  if (debug_enabled) {
    debug_abbrev_off = align_up_size(rela_off + rela_size, 1);
    debug_info_off = align_up_size(debug_abbrev_off + debug_abbrev_size, 1);
    debug_str_off = align_up_size(debug_info_off + debug_info_size, 1);
    shoff = align_up_size(debug_str_off + debug_str_size, 8);
  } else {
    shoff = align_up_size(rela_off + rela_size, 8);
  }
  file_size = shoff + (size_t)shnum * shent_size;

  img = calloc(1, file_size);
  if (!img) {
    if (debug_info) free(debug_info);
    return 1;
  }
  name_offs_def = malloc(sizeof(int) * (c->nfuncs > 0 ? c->nfuncs : 1));
  name_offs_ext = malloc(sizeof(int) * (c->n_ext_syms > 0 ? c->n_ext_syms : 1));
  if (!name_offs_def || !name_offs_ext) {
    if (name_offs_def) free(name_offs_def);
    if (name_offs_ext) free(name_offs_ext);
    if (debug_info) free(debug_info);
    free(img);
    return 1;
  }

  img[0] = 0x7f; img[1] = 'E'; img[2] = 'L'; img[3] = 'F';
  img[4] = 2;
  img[5] = 1;
  img[6] = 1;
  put_u16_le(img + 16, 1);   // ET_REL
  put_u16_le(img + 18, 62);  // x86_64
  put_u32_le(img + 20, 1);
  put_u64_le(img + 40, (uint64_t)shoff);
  put_u16_le(img + 52, (uint16_t)ehdr_size);
  put_u16_le(img + 54, 0);
  put_u16_le(img + 56, 0);
  put_u16_le(img + 58, (uint16_t)shent_size);
  put_u16_le(img + 60, (uint16_t)shnum);
  put_u16_le(img + 62, 2);   // .shstrtab index

  if (c->code.len > 0) memcpy(img + text_off, c->code.data, c->code.len);
  memcpy(img + shstr_off, shstrtab, shstrtab_size);
  if (debug_enabled) {
    memcpy(img + debug_abbrev_off, debug_abbrev, debug_abbrev_size);
    memcpy(img + debug_info_off, debug_info, debug_info_size);
  }

  {
    size_t st_off = strtab_off;
    img[st_off++] = 0;
    for (i = 0; i < c->nfuncs; i++) {
      const char *name = c->funcs[i].name ? c->funcs[i].name : "";
      name_offs_def[i] = (int)(st_off - strtab_off);
      copy_cstr(img, &st_off, name);
    }
    for (i = 0; i < c->n_ext_syms; i++) {
      const char *name = c->ext_syms[i] ? c->ext_syms[i] : "";
      name_offs_ext[i] = (int)(st_off - strtab_off);
      copy_cstr(img, &st_off, name);
    }
  }

  {
    unsigned char *sym = img + symtab_off;
    size_t sy = 0;
    put_sym64(sym + sy, 0, 0, 0, 0, 0, 0);
    sy += 24;
    put_sym64(sym + sy, 0, 0x03, 0, 1, 0, 0); // STB_LOCAL|STT_SECTION, .text
    sy += 24;
    for (i = 0; i < c->nfuncs; i++) {
      uint64_t sval = 0;
      if (c->funcs[i].label < 0 || c->funcs[i].label >= c->nlabels || !c->label_set[c->funcs[i].label]) {
        free(name_offs_def);
        free(name_offs_ext);
        if (debug_info) free(debug_info);
        free(img);
        return 1;
      }
      sval = (uint64_t)c->label_pos[c->funcs[i].label];
      put_sym64(sym + sy, (uint32_t)name_offs_def[i], 0x12, 0, 1, sval, 0); // STB_GLOBAL|STT_FUNC
      sy += 24;
    }
    for (i = 0; i < c->n_ext_syms; i++) {
      put_sym64(sym + sy, (uint32_t)name_offs_ext[i], 0x10, 0, 0, 0, 0); // STB_GLOBAL|STT_NOTYPE undefined
      sy += 24;
    }
  }

  {
    size_t ro = rela_off;
    for (i = 0; i < c->n_ext_fixups; i++) {
      uint64_t symidx = (uint64_t)(2 + c->nfuncs + c->ext_fixups[i].sym);
      uint64_t r_info = (symidx << 32) | 4; // R_X86_64_PLT32
      put_u64_le(img + ro + 0, (uint64_t)c->ext_fixups[i].at);
      put_u64_le(img + ro + 8, r_info);
      put_u64_le(img + ro + 16, (uint64_t)(int64_t)-4);
      ro += 24;
    }
  }

  {
    unsigned char *sh = img + shoff;
    memset(sh, 0, (size_t)shnum * shent_size);

    // .text
    put_u32_le(sh + shent_size * 1 + 0, sh_name_text);
    put_u32_le(sh + shent_size * 1 + 4, 1);
    put_u64_le(sh + shent_size * 1 + 8, 7);
    put_u64_le(sh + shent_size * 1 + 24, (uint64_t)text_off);
    put_u64_le(sh + shent_size * 1 + 32, (uint64_t)c->code.len);
    put_u64_le(sh + shent_size * 1 + 48, 16);

    // .shstrtab
    put_u32_le(sh + shent_size * 2 + 0, sh_name_shstrtab);
    put_u32_le(sh + shent_size * 2 + 4, 3);
    put_u64_le(sh + shent_size * 2 + 24, (uint64_t)shstr_off);
    put_u64_le(sh + shent_size * 2 + 32, (uint64_t)shstrtab_size);
    put_u64_le(sh + shent_size * 2 + 48, 1);

    // .strtab
    put_u32_le(sh + shent_size * 3 + 0, sh_name_strtab);
    put_u32_le(sh + shent_size * 3 + 4, 3);
    put_u64_le(sh + shent_size * 3 + 24, (uint64_t)strtab_off);
    put_u64_le(sh + shent_size * 3 + 32, (uint64_t)strtab_size);
    put_u64_le(sh + shent_size * 3 + 48, 1);

    // .symtab
    put_u32_le(sh + shent_size * 4 + 0, sh_name_symtab);
    put_u32_le(sh + shent_size * 4 + 4, 2);
    put_u64_le(sh + shent_size * 4 + 24, (uint64_t)symtab_off);
    put_u64_le(sh + shent_size * 4 + 32, (uint64_t)symtab_size);
    put_u32_le(sh + shent_size * 4 + 40, 3);
    put_u32_le(sh + shent_size * 4 + 44, 2);
    put_u64_le(sh + shent_size * 4 + 48, 8);
    put_u64_le(sh + shent_size * 4 + 56, 24);

    // .rela.text
    put_u32_le(sh + shent_size * 5 + 0, sh_name_rela_text);
    put_u32_le(sh + shent_size * 5 + 4, 4);
    put_u64_le(sh + shent_size * 5 + 24, (uint64_t)rela_off);
    put_u64_le(sh + shent_size * 5 + 32, (uint64_t)rela_size);
    put_u32_le(sh + shent_size * 5 + 40, 4); // link .symtab
    put_u32_le(sh + shent_size * 5 + 44, 1); // info .text
    put_u64_le(sh + shent_size * 5 + 48, 8);
    put_u64_le(sh + shent_size * 5 + 56, 24);

    if (debug_enabled) {
      // .debug_abbrev
      put_u32_le(sh + shent_size * 6 + 0, sh_name_debug_abbrev);
      put_u32_le(sh + shent_size * 6 + 4, 1);
      put_u64_le(sh + shent_size * 6 + 24, (uint64_t)debug_abbrev_off);
      put_u64_le(sh + shent_size * 6 + 32, (uint64_t)debug_abbrev_size);
      put_u64_le(sh + shent_size * 6 + 48, 1);

      // .debug_info
      put_u32_le(sh + shent_size * 7 + 0, sh_name_debug_info);
      put_u32_le(sh + shent_size * 7 + 4, 1);
      put_u64_le(sh + shent_size * 7 + 24, (uint64_t)debug_info_off);
      put_u64_le(sh + shent_size * 7 + 32, (uint64_t)debug_info_size);
      put_u64_le(sh + shent_size * 7 + 48, 1);

      // .debug_str
      put_u32_le(sh + shent_size * 8 + 0, sh_name_debug_str);
      put_u32_le(sh + shent_size * 8 + 4, 1);
      put_u64_le(sh + shent_size * 8 + 24, (uint64_t)debug_str_off);
      put_u64_le(sh + shent_size * 8 + 32, (uint64_t)debug_str_size);
      put_u64_le(sh + shent_size * 8 + 48, 1);
    }
  }

  off = 0;
  while (off < file_size) {
    long n = write(out_fd, img + off, file_size - off);
    if (n <= 0) {
      free(name_offs_def);
      free(name_offs_ext);
      if (debug_info) free(debug_info);
      free(img);
      return 1;
    }
    off += (size_t)n;
  }

  free(name_offs_def);
  free(name_offs_ext);
  if (debug_info) free(debug_info);
  free(img);
  return 0;
}

int write_ar_single_member_fd(int out_fd, const char *member_name, const unsigned char *data, size_t size) {
  size_t off = 0;
  size_t nlen = strlen(member_name);
  size_t d_off = 0;
  unsigned char magic[8];
  int i;

  magic[0] = '!';
  magic[1] = '<';
  magic[2] = 'a';
  magic[3] = 'r';
  magic[4] = 'c';
  magic[5] = 'h';
  magic[6] = '>';
  magic[7] = 10;
  while (off < 8) {
    long n = write(out_fd, magic + off, 8 - off);
    if (n <= 0) return 1;
    off += (size_t)n;
  }

  if (nlen > 15) nlen = 15;
  for (i = 0; i < (int)nlen; i++) {
    char ch = member_name[i];
    if (write(out_fd, &ch, 1) != 1) return 1;
  }
  {
    char slash = '/';
    if (write(out_fd, &slash, 1) != 1) return 1;
  }
  for (i = (int)nlen + 1; i < 16; i++) {
    char sp = ' ';
    if (write(out_fd, &sp, 1) != 1) return 1;
  }

  // mtime (12), uid (6), gid (6): "0" + spaces
  {
    char z = '0';
    if (write(out_fd, &z, 1) != 1) return 1;
  }
  for (i = 1; i < 12; i++) { char sp = ' '; if (write(out_fd, &sp, 1) != 1) return 1; }
  {
    char z = '0';
    if (write(out_fd, &z, 1) != 1) return 1;
  }
  for (i = 1; i < 6; i++) { char sp = ' '; if (write(out_fd, &sp, 1) != 1) return 1; }
  {
    char z = '0';
    if (write(out_fd, &z, 1) != 1) return 1;
  }
  for (i = 1; i < 6; i++) { char sp = ' '; if (write(out_fd, &sp, 1) != 1) return 1; }

  // mode (8): "100644" + spaces
  {
    const char *mode = "100644";
    for (i = 0; i < 6; i++) if (write(out_fd, mode + i, 1) != 1) return 1;
  }
  for (i = 6; i < 8; i++) { char sp = ' '; if (write(out_fd, &sp, 1) != 1) return 1; }

  // size (10): decimal left-justified + spaces
  {
    char dec[32];
    int nd = 0;
    size_t t = size;
    if (t == 0) dec[nd++] = '0';
    else {
      char rev[32];
      int nr = 0;
      while (t > 0 && nr < 32) {
        size_t q = t / 10;
        size_t rem = t - q * 10;
        rev[nr++] = (char)('0' + rem);
        t = q;
      }
      while (nr > 0) dec[nd++] = rev[--nr];
    }
    if (nd > 10) nd = 10;
    for (i = 0; i < nd; i++) if (write(out_fd, dec + i, 1) != 1) return 1;
    for (i = nd; i < 10; i++) { char sp = ' '; if (write(out_fd, &sp, 1) != 1) return 1; }
  }

  {
    char end1 = '`';
    char end2 = 10;
    if (write(out_fd, &end1, 1) != 1) return 1;
    if (write(out_fd, &end2, 1) != 1) return 1;
  }

  while (d_off < size) {
    long n = write(out_fd, data + d_off, size - d_off);
    if (n <= 0) return 1;
    d_off += (size_t)n;
  }
  if (size & 1) {
    char pad = 10;
    if (write(out_fd, &pad, 1) != 1) return 1;
  }
  return 0;
}

int write_elf_exec_fd(BinBuf *code, size_t entry_off, int out_fd) {
  static const char shstrtab[] = "\0.text\0.shstrtab\0.debug_abbrev\0.debug_info\0.debug_str\0";
  static const unsigned char debug_abbrev[] = {
    1, 0x11, 0,
    0x25, 0x08,
    0x13, 0x05,
    0x03, 0x08,
    0x1b, 0x08,
    0, 0,
    0
  };
  const uint64_t base = 0x400000;
  const size_t code_off = 0x1000;
  const size_t shent_size = 64;
  const int debug_enabled = elf_debug_enabled ? 1 : 0;
  const int shnum = debug_enabled ? 6 : 0;
  size_t load_size = code_off + code->len;
  size_t shstrtab_size = sizeof(shstrtab);
  size_t debug_abbrev_size = sizeof(debug_abbrev);
  size_t debug_info_size = 0;
  size_t debug_str_size = debug_enabled ? 1 : 0;
  size_t shstr_off = 0;
  size_t debug_abbrev_off = 0;
  size_t debug_info_off = 0;
  size_t debug_str_off = 0;
  size_t shoff = 0;
  size_t file_size = load_size;
  size_t off = 0;
  unsigned char *img;
  unsigned char *debug_info = NULL;

  if (debug_enabled) {
    const char *debug_name = elf_debug_source_path ? elf_debug_source_path : "";
    const char *debug_dir = elf_debug_comp_dir ? elf_debug_comp_dir : ".";
    debug_info = build_debug_info(debug_name, debug_dir, &debug_info_size);
    if (!debug_info) return 1;
    shstr_off = align_up_size(load_size, 1);
    debug_abbrev_off = align_up_size(shstr_off + shstrtab_size, 1);
    debug_info_off = align_up_size(debug_abbrev_off + debug_abbrev_size, 1);
    debug_str_off = align_up_size(debug_info_off + debug_info_size, 1);
    shoff = align_up_size(debug_str_off + debug_str_size, 8);
    file_size = shoff + (size_t)shnum * shent_size;
  }

  if (parse_verbose) eprintf("[v] elf: write begin code_len=%d file_size=%d\n", (int)code->len, (int)file_size, 0, 0);
  img = (unsigned char *)calloc(1, file_size);
  if (!img) {
    if (debug_info) free(debug_info);
    return 1;
  }
  if (parse_verbose) eprintf("[v] elf: write calloc ok ptr=%ld\n", (long)img, 0, 0, 0);

  img[0] = 0x7f; img[1] = 'E'; img[2] = 'L'; img[3] = 'F';
  img[4] = 2;   // 64-bit
  img[5] = 1;   // little endian
  img[6] = 1;   // version
  put_u16_le(img + 16, 2);   // ET_EXEC
  put_u16_le(img + 18, 62);  // x86_64
  put_u32_le(img + 20, 1);
  put_u64_le(img + 24, base + code_off + entry_off);
  put_u64_le(img + 32, 64);  // e_phoff
  if (debug_enabled) put_u64_le(img + 40, (uint64_t)shoff);
  put_u16_le(img + 52, 64);  // e_ehsize
  put_u16_le(img + 54, 56);  // e_phentsize
  put_u16_le(img + 56, 1);   // e_phnum
  if (debug_enabled) {
    put_u16_le(img + 58, (uint16_t)shent_size);
    put_u16_le(img + 60, (uint16_t)shnum);
    put_u16_le(img + 62, 2);
  }

  // Program header starts at offset 64.
  put_u32_le(img + 64 + 0, 1);                // PT_LOAD
  put_u32_le(img + 64 + 4, 7);                // PF_R|PF_W|PF_X
  put_u64_le(img + 64 + 8, 0);                // p_offset
  put_u64_le(img + 64 + 16, base);            // p_vaddr
  put_u64_le(img + 64 + 24, base);            // p_paddr
  put_u64_le(img + 64 + 32, (uint64_t)load_size);
  put_u64_le(img + 64 + 40, (uint64_t)load_size);
  put_u64_le(img + 64 + 48, 0x1000);

  {
    size_t ci = 0;
    while (ci < code->len) {
      img[code_off + ci] = code->data[ci];
      ci = ci + 1;
    }
  }
  if (debug_enabled) {
    unsigned char *sh;
    memcpy(img + shstr_off, shstrtab, shstrtab_size);
    memcpy(img + debug_abbrev_off, debug_abbrev, debug_abbrev_size);
    memcpy(img + debug_info_off, debug_info, debug_info_size);

    sh = img + shoff;
    memset(sh, 0, (size_t)shnum * shent_size);

    // .text
    put_u32_le(sh + shent_size * 1 + 0, shstr_name_off(shstrtab, shstrtab_size, ".text"));
    put_u32_le(sh + shent_size * 1 + 4, 1);
    put_u64_le(sh + shent_size * 1 + 8, 6);
    put_u64_le(sh + shent_size * 1 + 16, base + code_off);
    put_u64_le(sh + shent_size * 1 + 24, (uint64_t)code_off);
    put_u64_le(sh + shent_size * 1 + 32, (uint64_t)code->len);
    put_u64_le(sh + shent_size * 1 + 48, 16);

    // .shstrtab
    put_u32_le(sh + shent_size * 2 + 0, shstr_name_off(shstrtab, shstrtab_size, ".shstrtab"));
    put_u32_le(sh + shent_size * 2 + 4, 3);
    put_u64_le(sh + shent_size * 2 + 24, (uint64_t)shstr_off);
    put_u64_le(sh + shent_size * 2 + 32, (uint64_t)shstrtab_size);
    put_u64_le(sh + shent_size * 2 + 48, 1);

    // .debug_abbrev
    put_u32_le(sh + shent_size * 3 + 0, shstr_name_off(shstrtab, shstrtab_size, ".debug_abbrev"));
    put_u32_le(sh + shent_size * 3 + 4, 1);
    put_u64_le(sh + shent_size * 3 + 24, (uint64_t)debug_abbrev_off);
    put_u64_le(sh + shent_size * 3 + 32, (uint64_t)debug_abbrev_size);
    put_u64_le(sh + shent_size * 3 + 48, 1);

    // .debug_info
    put_u32_le(sh + shent_size * 4 + 0, shstr_name_off(shstrtab, shstrtab_size, ".debug_info"));
    put_u32_le(sh + shent_size * 4 + 4, 1);
    put_u64_le(sh + shent_size * 4 + 24, (uint64_t)debug_info_off);
    put_u64_le(sh + shent_size * 4 + 32, (uint64_t)debug_info_size);
    put_u64_le(sh + shent_size * 4 + 48, 1);

    // .debug_str
    put_u32_le(sh + shent_size * 5 + 0, shstr_name_off(shstrtab, shstrtab_size, ".debug_str"));
    put_u32_le(sh + shent_size * 5 + 4, 1);
    put_u64_le(sh + shent_size * 5 + 24, (uint64_t)debug_str_off);
    put_u64_le(sh + shent_size * 5 + 32, (uint64_t)debug_str_size);
    put_u64_le(sh + shent_size * 5 + 48, 1);
  }
  if (parse_verbose) eprintf("[v] elf: write copy code done\n", 0, 0, 0, 0);

  while (off < file_size) {
    long n = write(out_fd, img + off, file_size - off);
    if (n <= 0) {
      if (debug_info) free(debug_info);
      free(img);
      return 1;
    }
    off += (size_t)n;
  }
  if (parse_verbose) eprintf("[v] elf: write bytes done off=%d\n", (int)off, 0, 0, 0);
  if (debug_info) free(debug_info);
  free(img);
  return 0;
}

void apply_user_global_initializers(ElfCtx *c, size_t globals_pos) {
  const uint64_t base = 0x400000;
  const uint64_t code_off = 0x1000;
  int gs = global_storage_size();
  for (GlobalSymbol *g = user_globals; g; g = g->next) {
    size_t pos;
    int sz;
    uint64_t v;
    if (!g->has_init) continue;
    if (g->offset < 0 || g->offset >= gs) continue;
    pos = globals_pos + (size_t)g->offset;
    if (pos >= c->code.len) continue;
    v = (uint64_t)g->init_value;
    if (g->init_ref) {
      GlobalSymbol *ref = find_user_global(g->init_ref);
      if (!ref) continue;
      v = base + code_off + globals_pos + (uint64_t)ref->offset;
    }
    sz = g->ptr_level > 0 ? 8 : type_size_from_name(g->type_name);
    if (sz <= 1 && pos + 1 <= c->code.len) {
      c->code.data[pos] = (unsigned char)v;
    } else if (sz == 2 && pos + 2 <= c->code.len) {
      put_u16_le(c->code.data + pos, (uint16_t)v);
    } else if (sz == 4 && pos + 4 <= c->code.len) {
      put_u32_le(c->code.data + pos, (uint32_t)v);
    } else if (pos + 8 <= c->code.len) {
      put_u64_le(c->code.data + pos, v);
    }
  }
}

int compile_to_elf_source_fd(char *source, int out_fd) {
  reset_string_literals();
  if (parse_verbose) eprintf("[v] elf: start compile_to_elf_source_fd\n", 0, 0, 0, 0);

  Node *funcs = parse_program_functions(source);
  if (parse_verbose) eprintf("[v] elf: parse_program_functions done\n", 0, 0, 0, 0);
  int nfunc = 0;
  for (Node *f = funcs; f; f = f->next) {
    if (f->kind == ND_FUNC && f->name && f->body) nfunc++;
  }
  if (parse_verbose) eprintf("[v] elf: nfunc=%d\n", nfunc, 0, 0, 0);
  if (nfunc == 0) {
    free(source);
    reset_string_literals();
    eprintf("Error: no functions compiled from source\n", 0, 0, 0, 0);
    return 1;
  }

  ElfCtx c;
  int break_labels_buf[CC_CFG_ELF_LOOP_NEST_MAX];
  int continue_labels_buf[CC_CFG_ELF_LOOP_NEST_MAX];
  StrLit str_lits_buf[CC_CFG_ELF_STR_LIT_MAX];
  memset(&c, 0, sizeof(ElfCtx));
  c.break_top = 0;
  c.break_labels = break_labels_buf;
  c.continue_top = 0;
  c.continue_labels = continue_labels_buf;
  c.str_lits = str_lits_buf;
  {
    size_t funcs_bytes = (size_t)nfunc * CC_CFG_ELF_FUNC_SLOT_BYTES;
    c.funcs = (FnLabel *)malloc(funcs_bytes);
    if (c.funcs) memset(c.funcs, 0, funcs_bytes);
  }
  if (parse_verbose) eprintf("[v] elf: calloc funcs ptr=%ld\n", (long)c.funcs, 0, 0, 0);
  if (!c.funcs) {
    eprintf("Error: out of memory allocating function labels\n", 0, 0, 0, 0);
    free(source);
    reset_string_literals();
    return 1;
  }
  c.nfuncs = nfunc;
  if (parse_verbose) eprintf("[v] elf: before globals label\n", 0, 0, 0, 0);
  c.globals_label = ctx_new_label(&c);
  if (parse_verbose) eprintf("[v] elf: globals label=%d\n", c.globals_label, 0, 0, 0);

  int idx = 0;
  for (Node *f = funcs; f; f = f->next) {
    if (f->kind == ND_FUNC && f->name && f->body) {
      c.funcs[idx].name = f->name;
      c.funcs[idx].fn = f;
      c.funcs[idx].label = ctx_new_label(&c);
      idx++;
    }
  }
  if (parse_verbose) eprintf("[v] elf: collected funcs idx=%d\n", idx, 0, 0, 0);

  int main_lbl = find_func_label(&c, "main");
  if (main_lbl < 0) {
    free(c.funcs);
    free(c.user_label_names);
    free(c.user_label_ids);
    free(source);
    reset_string_literals();
    eprintf("Error: main() not compiled from source\n", 0, 0, 0, 0);
    return 1;
  }

  int start_lbl = ctx_new_label(&c);
  if (parse_verbose) eprintf("[v] elf: labels start=%d main=%d\n", start_lbl, main_lbl, 0, 0);
  ctx_place_label(&c, start_lbl);
  bb_emit1(&c.code, 0x48); bb_emit1(&c.code, 0x8b); bb_emit1(&c.code, 0x3c); bb_emit1(&c.code, 0x24);          // mov rdi,[rsp]
  bb_emit1(&c.code, 0x48); bb_emit1(&c.code, 0x8d); bb_emit1(&c.code, 0x74); bb_emit1(&c.code, 0x24); bb_emit1(&c.code, 0x08); // lea rsi,[rsp+8]
  emit_call_label(&c, main_lbl);                             // call main
  bb_emit1(&c.code, 0x89); bb_emit1(&c.code, 0xc7);         // mov edi,eax
  bb_emit1(&c.code, 0xb8); bb_emit4(&c.code, 60);           // mov eax,60
  bb_emit1(&c.code, 0x0f); bb_emit1(&c.code, 0x05);         // syscall

  {
    int i = 0;
    while (i < c.nfuncs) {
      emit_function_elf(&c, &c.funcs[i]);
      i++;
    }
  }
  if (parse_verbose) eprintf("[v] elf: emitted all functions\n", 0, 0, 0, 0);
  emit_string_literals(&c);
  if (parse_verbose) eprintf("[v] elf: emitted string literals n=%d\n", c.n_str_lits, 0, 0, 0);
  ctx_place_label(&c, c.globals_label);
  {
    int i = 0;
    size_t globals_pos = c.code.len;
    /* ast.c computes this from CompilerGlobalState via sizeof/offsetof. */
    int gs = global_storage_size();
    while (i < gs) {
      bb_emit1(&c.code, 0);
      i++;
    }
    apply_user_global_initializers(&c, globals_pos);
  }

  if (patch_fixups(&c) != 0) {
    eprintf("Error: unresolved labels in codegen\n", 0, 0, 0, 0);
    free(c.code.data); free(c.label_pos); free(c.label_set); free(c.fixups); free(c.funcs); free(c.user_label_names); free(c.user_label_ids); free(source);
    reset_string_literals();
    return 1;
  }
  if (parse_verbose) eprintf("[v] elf: fixups patched n=%d\n", c.nfixups, 0, 0, 0);

  int rc = write_elf_exec_fd(&c.code, c.label_pos[start_lbl], out_fd);
  if (parse_verbose) eprintf("[v] elf: write_elf rc=%d code_len=%d\n", rc, c.code.len, 0, 0);
  free(c.code.data);
  free(c.label_pos);
  free(c.label_set);
  free(c.fixups);
  free(c.funcs);
  free(c.user_label_names);
  free(c.user_label_ids);
  free(source);
  reset_string_literals();
  if (rc != 0) {
    eprintf("Error: cannot write ELF output\n", 0, 0, 0, 0);
    return 1;
  }
  return 0;
}

int compile_to_elf() {
  char *source = read_stdin_source();
  if (!source) {
    eprintf("Error: cannot read stdin\n", 0, 0, 0, 0);
    return 1;
  }
  return compile_to_elf_source_fd(source, 1);
}

int compile_to_elf_source_path(char *source, const char *out_path) {
  int fd = open(out_path, O_WRONLY + O_CREAT + O_TRUNC, 493);
  int rc;
  if (fd < 0) {
    eprintf("Error: cannot open output file: %s\n", (long)out_path, 0, 0, 0);
    return 1;
  }
  rc = compile_to_elf_source_fd(source, fd);
  close(fd);
  return rc;
}

int compile_to_object_source_fd(char *source, int out_fd) {
  reset_string_literals();
  if (parse_verbose) eprintf("[v] elf: start compile_to_object_source_fd\n", 0, 0, 0, 0);

  Node *funcs = parse_program_functions(source);
  int nfunc = 0;
  for (Node *f = funcs; f; f = f->next) {
    if (f->kind == ND_FUNC && f->name && f->body) nfunc++;
  }
  if (nfunc == 0) {
    free(source);
    reset_string_literals();
    eprintf("Error: no functions compiled from source\n", 0, 0, 0, 0);
    return 1;
  }

  ElfCtx c = {0};
  int break_labels_buf[CC_CFG_ELF_LOOP_NEST_MAX];
  int continue_labels_buf[CC_CFG_ELF_LOOP_NEST_MAX];
  StrLit str_lits_buf[CC_CFG_ELF_STR_LIT_MAX];
  c.code.data = NULL;
  c.code.len = 0;
  c.code.cap = 0;
  c.label_pos = NULL;
  c.label_set = NULL;
  c.nlabels = 0;
  c.caplabels = 0;
  c.fixups = NULL;
  c.nfixups = 0;
  c.capfixups = 0;
  c.funcs = NULL;
  c.nfuncs = 0;
  c.break_top = 0;
  c.break_labels = break_labels_buf;
  c.continue_top = 0;
  c.continue_labels = continue_labels_buf;
  c.ret_label = 0;
  c.globals_label = 0;
  c.str_lits = str_lits_buf;
  c.n_str_lits = 0;
  c.ext_syms = NULL;
  c.n_ext_syms = 0;
  c.cap_ext_syms = 0;
  c.ext_fixups = NULL;
  c.n_ext_fixups = 0;
  c.cap_ext_fixups = 0;
  c.emit_rel_obj = 1;
  {
    size_t funcs_bytes = (size_t)nfunc * CC_CFG_ELF_FUNC_SLOT_BYTES;
    c.funcs = (FnLabel *)malloc(funcs_bytes);
    if (c.funcs) memset(c.funcs, 0, funcs_bytes);
  }
  if (!c.funcs) {
    eprintf("Error: out of memory allocating function labels\n", 0, 0, 0, 0);
    free(source);
    reset_string_literals();
    return 1;
  }
  c.nfuncs = nfunc;
  c.globals_label = ctx_new_label(&c);

  {
    int idx = 0;
    for (Node *f = funcs; f; f = f->next) {
      if (f->kind == ND_FUNC && f->name && f->body) {
        c.funcs[idx].name = f->name;
        c.funcs[idx].fn = f;
        c.funcs[idx].label = ctx_new_label(&c);
        idx++;
      }
    }
  }

  {
    int i = 0;
    while (i < c.nfuncs) {
      emit_function_elf(&c, &c.funcs[i]);
      i++;
    }
  }
  emit_string_literals(&c);
  ctx_place_label(&c, c.globals_label);
  {
    int i = 0;
    int gs = global_storage_size();
    while (i < gs) {
      bb_emit1(&c.code, 0);
      i++;
    }
  }

  if (patch_fixups(&c) != 0) {
    eprintf("Error: unresolved labels in codegen\n", 0, 0, 0, 0);
    free(c.code.data); free(c.label_pos); free(c.label_set); free(c.fixups); free(c.funcs); free(c.ext_syms); free(c.ext_fixups); free(c.user_label_names); free(c.user_label_ids); free(source);
    reset_string_literals();
    return 1;
  }

  {
    int rc = write_elf_rel_obj_fd(&c, out_fd);
    free(c.code.data);
    free(c.label_pos);
    free(c.label_set);
    free(c.fixups);
    free(c.funcs);
    free(c.ext_syms);
    free(c.ext_fixups);
    free(c.user_label_names);
    free(c.user_label_ids);
    free(source);
    reset_string_literals();
    if (rc != 0) {
      eprintf("Error: cannot write ELF object output\n", 0, 0, 0, 0);
      return 1;
    }
    return 0;
  }
}

int compile_to_object_source_path(char *source, const char *out_path) {
  int fd = open(out_path, O_WRONLY + O_CREAT + O_TRUNC, 493);
  int rc;
  if (fd < 0) {
    eprintf("Error: cannot open output file: %s\n", (long)out_path, 0, 0, 0);
    return 1;
  }
  rc = compile_to_object_source_fd(source, fd);
  close(fd);
  return rc;
}

int read_binary_file(const char *path, unsigned char **out_buf, size_t *out_len) {
  int fd = open(path, O_RDONLY, 0);
  size_t cap = CC_CFG_IO_BUFFER_INIT;
  size_t len = 0;
  unsigned char *buf;
  if (fd < 0) return 1;

  buf = malloc(cap);
  if (!buf) {
    close(fd);
    return 1;
  }

  while (1) {
    long n;
    if (len == cap) {
      unsigned char *nb;
      cap *= 2;
      nb = realloc(buf, cap);
      if (!nb) {
        free(buf);
        close(fd);
        return 1;
      }
      buf = nb;
    }
    n = read(fd, buf + len, cap - len);
    if (n < 0) {
      free(buf);
      close(fd);
      return 1;
    }
    if (n == 0) break;
    len += (size_t)n;
  }

  close(fd);
  *out_buf = buf;
  *out_len = len;
  return 0;
}

int compile_to_archive_source_path(char *source, const char *out_path, const char *member_name) {
  const char *tmp_obj_path = "/tmp/cc-posix-archive-member.o";
  unsigned char *obj_data = NULL;
  size_t obj_size = 0;
  int out_fd;

  if (compile_to_object_source_path(source, tmp_obj_path) != 0) return 1;
  if (read_binary_file(tmp_obj_path, &obj_data, &obj_size) != 0) {
    eprintf("Error: cannot read temporary object for archive\n", 0, 0, 0, 0);
    return 1;
  }

  out_fd = open(out_path, O_WRONLY + O_CREAT + O_TRUNC, 493);
  if (out_fd < 0) {
    free(obj_data);
    eprintf("Error: cannot open output file: %s\n", (long)out_path, 0, 0, 0);
    return 1;
  }
  if (write_ar_single_member_fd(out_fd, member_name, obj_data, obj_size) != 0) {
    close(out_fd);
    free(obj_data);
    eprintf("Error: cannot write POSIX archive output\n", 0, 0, 0, 0);
    return 1;
  }
  close(out_fd);
  free(obj_data);
  return 0;
}
