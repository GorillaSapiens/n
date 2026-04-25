//! @file compiler/compile_support.c
//! @brief Implements shared compiler support routines for the n65 compiler.
//! @ingroup compiler

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>

#include "ast.h"
#include "compile.h"
#include "compile_internal.h"
#include "compile_lvalue.h"
#include "compile_literal.h"
#include "compile_overload.h"
#include "compile_stmt.h"
#include "compile_support.h"
#include "compile_type.h"
#include "emit.h"
#include "float.h"
#include "integer.h"
#include "memname.h"
#include "messages.h"
#include "pair.h"
#include "set.h"
#include "typename.h"
#include "xform.h"
#include "xray.h"
#include "lextern.h"

ContextEntry *ctx_lookup(Context *ctx, const char *name) {
   return ctx ? (ContextEntry *) set_get(ctx->vars, name) : NULL;
}


const ASTNode *global_decl_lookup(const char *name) {
   const void *value;
   if (!globals || !name) {
      return NULL;
   }
   value = set_get(globals, name);
   if (!value || (uintptr_t) value < 4096) {
      return NULL;
   }
   return (const ASTNode *) value;
}

static const ASTNode *decl_subitem_declarator(const ASTNode *node) {
   if (!node) {
      return NULL;
   }
   if (strcmp(node->name, "decl_subitem") || node->count <= 0) {
      return node;
   }
   return node->children[0];
}

static const ASTNode *decl_subitem_address_spec(const ASTNode *node) {
   if (!node || strcmp(node->name, "decl_subitem") || node->count <= 1) {
      return NULL;
   }
   return node->children[1];
}

const ASTNode *decl_node_declarator(const ASTNode *node) {
   if (!node || node->count <= 2) {
      return NULL;
   }
   return decl_subitem_declarator(node->children[2]);
}

static const ASTNode *decl_node_address_spec(const ASTNode *node) {
   if (!node || node->count <= 2) {
      return NULL;
   }
   return decl_subitem_address_spec(node->children[2]);
}

static const char *address_spec_read_expr(const ASTNode *node) {
   if (!node || is_empty(node)) {
      return NULL;
   }
   if (!strcmp(node->name, "rw_addr_spec")) {
      return (node->count > 0 && node->children[0] && !is_empty(node->children[0])) ? node->children[0]->strval : NULL;
   }
   return node->strval;
}

static const char *address_spec_write_expr(const ASTNode *node) {
   if (!node || is_empty(node)) {
      return NULL;
   }
   if (!strcmp(node->name, "rw_addr_spec")) {
      return (node->count > 1 && node->children[1] && !is_empty(node->children[1])) ? node->children[1]->strval : NULL;
   }
   return node->strval;
}

bool entry_has_read_address(const ContextEntry *entry) {
   return entry && entry->is_absolute_ref && entry->read_expr && *entry->read_expr;
}

bool entry_has_write_address(const ContextEntry *entry) {
   return entry && entry->is_absolute_ref && entry->write_expr && *entry->write_expr;
}

bool entry_is_absolute_ref(const ContextEntry *entry) {
   return entry && entry->is_absolute_ref;
}

bool init_context_entry_from_global_decl(ContextEntry *entry, const char *name, const ASTNode *g) {
   const ASTNode *modifiers;
   const ASTNode *type;
   const ASTNode *declarator;
   const ASTNode *addrspec;

   if (!entry || !g || g->count < 3) {
      return false;
   }

   modifiers = g->children[0];
   type = g->children[1];
   declarator = decl_node_declarator(g);
   addrspec = decl_node_address_spec(g);
   if (!type || !declarator) {
      return false;
   }

   memset(entry, 0, sizeof(*entry));
   entry->name = name;
   entry->type = type;
   entry->declarator = declarator;
   entry->is_static = false;
   entry->is_zeropage = modifiers_imply_zeropage((ASTNode *) modifiers);
   entry->is_global = true;
   entry->is_ref = false;
   entry->is_absolute_ref = has_modifier((ASTNode *) modifiers, "ref") && addrspec != NULL;
   entry->read_expr = address_spec_read_expr(addrspec);
   entry->write_expr = address_spec_write_expr(addrspec);
   entry->offset = 0;
   entry->size = declarator_storage_size(type, declarator);
   return true;
}

bool entry_symbol_name(Context *ctx, const ContextEntry *entry, char *buf, size_t bufsize) {
   if (!entry || !entry->name || !buf || bufsize < 8) {
      return false;
   }
   if (entry->is_absolute_ref) {
      return false;
   }
   if (entry->is_global) {
      return format_user_asm_symbol(entry->name, buf, bufsize);
   }
   if (entry->is_static || entry->is_zeropage) {
      char raw[256];
      snprintf(raw, sizeof(raw), "%s$%s", ctx && ctx->name ? ctx->name : "", entry->name);
      return format_user_asm_symbol(raw, buf, bufsize);
   }
   return false;
}

void emit_copy_fp_to_symbol_offset(const char *symbol, int symbol_offset, int src_offset, int size) {
   bool src_direct = src_offset >= 0 && src_offset + size <= 256;
   if (!src_direct) {
      emit_prepare_fp_ptr(1, src_offset);
   }
   for (int i = 0; i < size; i++) {
      emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + i) : i);
      emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr1)");
      emit(&es_code, "    ldy #%d\n", symbol_offset + i);
      emit(&es_code, "    sta %s,y\n", symbol);
   }
}

void emit_copy_fp_to_symbol(const char *symbol, int src_offset, int size) {
   emit_copy_fp_to_symbol_offset(symbol, 0, src_offset, size);
}

void emit_load_a_from_expr_address(const char *expr, int addend) {
   char expr_buf[256];
   const char *asm_expr = assembler_address_expr(expr, expr_buf, sizeof(expr_buf));

   if (addend == 0) {
      emit(&es_code, "    lda  %s\n", asm_expr);
   }
   else {
      emit(&es_code, "    lda  %s + %d\n", asm_expr, addend);
   }
}

void emit_store_a_to_expr_address(const char *expr, int addend) {
   char expr_buf[256];
   const char *asm_expr = assembler_address_expr(expr, expr_buf, sizeof(expr_buf));

   if (addend == 0) {
      emit(&es_code, "    sta  %s\n", asm_expr);
   }
   else {
      emit(&es_code, "    sta  %s + %d\n", asm_expr, addend);
   }
}

static bool absolute_ref_supports_direct_access(const LValueRef *lv) {
   return lv && lv->is_absolute_ref && !lv->is_bitfield && !lv->indirect && !lv->needs_runtime_address;
}


bool emit_copy_lvalue_to_symbol(Context *ctx, const char *symbol, int symbol_offset, const LValueRef *src, int size) {
   int copy_size = size < src->size ? size : src->size;

   if (src && src->is_bitfield) {
      return false;
   }
   if (absolute_ref_supports_direct_access(src)) {
      const char *read_expr = src->read_expr;

      if (!read_expr || !*read_expr) {
         return false;
      }
      for (int i = 0; i < copy_size; i++) {
         emit_load_a_from_expr_address(read_expr, src->offset + i);
         emit(&es_code, "    ldy #%d\n", symbol_offset + i);
         emit(&es_code, "    sta %s,y\n", symbol);
      }
      return true;
   }
   if (copy_size <= 0) {
      return true;
   }
   if (!emit_prepare_lvalue_ptr(ctx, src, LVALUE_ACCESS_READ)) {
      return false;
   }
   for (int i = 0; i < copy_size; i++) {
      emit(&es_code, "    ldy #%d\n", i);
      emit(&es_code, "    lda (ptr0),y\n");
      emit(&es_code, "    ldy #%d\n", symbol_offset + i);
      emit(&es_code, "    sta %s,y\n", symbol);
   }
   return true;
}









void emit_runtime_fill_ptr1(int count, unsigned char value) {
   const char *helper;

   if (count <= 0) {
      return;
   }

   helper = value == 0 ? "zeroN" : "setN";
   remember_runtime_import(helper);
   emit(&es_code, "    lda #$%02x\n", count & 0xff);
   emit(&es_code, "    sta arg0\n");
   if (value != 0) {
      emit(&es_code, "    lda #$%02x\n", value);
      emit(&es_code, "    sta arg1\n");
   }
   emit(&es_code, "    jsr _%s\n", helper);
}

const char *runtime_copy_convert_helper_name(int dst_size, const ASTNode *dst_type, int src_size, const ASTNode *src_type) {
   bool src_big_endian = type_is_big_endian(src_type);
   bool dst_big_endian = type_is_big_endian(dst_type);
   bool is_signed = type_is_signed_integer(src_type);

   if (dst_size <= 0 || src_size <= 0 || dst_size == src_size || src_big_endian != dst_big_endian) {
      return NULL;
   }
   return is_signed ? (src_big_endian ? "copysxNbe" : "copysxNle")
                    : (src_big_endian ? "copyzxNbe" : "copyzxNle");
}

void emit_runtime_copy_ptr0_to_ptr1(const char *helper, int src_size, int dst_size) {
   if (!helper || src_size <= 0 || dst_size <= 0) {
      return;
   }

   remember_runtime_import(helper);
   emit(&es_code, "    lda #$%02x\n", src_size & 0xff);
   emit(&es_code, "    sta arg0\n");
   if (!strcmp(helper, "cpyN")) {
      emit(&es_code, "    jsr _cpyN\n");
      return;
   }
   emit(&es_code, "    lda #$%02x\n", dst_size & 0xff);
   emit(&es_code, "    sta arg1\n");
   emit(&es_code, "    jsr _%s\n", helper);
}

void emit_fill_fp_bytes(int dst_offset, int start, int count, unsigned char value) {
   if (count <= 0) {
      return;
   }

   emit_prepare_fp_ptr(1, dst_offset + start);
   emit_runtime_fill_ptr1(count, value);
}

static void emit_sign_fill_from_masked_a(void) {
   const char *zero_label = next_label("signext_zero");
   const char *done_label = next_label("signext_done");

   emit(&es_code, "    beq %s\n", zero_label);
   emit(&es_code, "    lda #$ff\n");
   emit(&es_code, "    bne %s\n", done_label);
   emit(&es_code, "%s:\n", zero_label);
   emit(&es_code, "    lda #$00\n");
   emit(&es_code, "%s:\n", done_label);
}

void emit_copy_fp_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, int src_offset, int src_size, const ASTNode *src_type) {
   bool src_big_endian = type_is_big_endian(src_type);
   bool dst_big_endian = type_is_big_endian(dst_type);
   bool is_signed = type_is_signed_integer(src_type);
   bool dst_direct;
   bool src_direct;
   int sign_src_mem;
   const char *helper;

   if (dst_size <= 0 || src_size <= 0) {
      return;
   }

   dst_direct = dst_offset >= 0 && dst_offset + dst_size <= 256;
   src_direct = src_offset >= 0 && src_offset + src_size <= 256;
   sign_src_mem = endian_mem_index_for_significance(src_size, src_big_endian, src_size - 1);
   helper = runtime_copy_convert_helper_name(dst_size, dst_type, src_size, src_type);

   if (helper) {
      emit_prepare_fp_ptr(0, src_offset);
      emit_prepare_fp_ptr(1, dst_offset);
      emit_runtime_copy_ptr0_to_ptr1(helper, src_size, dst_size);
      return;
   }

   if (!src_direct) {
      emit_prepare_fp_ptr(0, src_offset);
   }
   if (!dst_direct) {
      emit_prepare_fp_ptr(1, dst_offset);
   }

   if (dst_offset == src_offset) {
      for (int j = dst_size - 1; j >= 0; j--) {
         int sig = dst_big_endian ? (dst_size - 1 - j) : j;
         if (sig < src_size) {
            int src_mem = endian_mem_index_for_significance(src_size, src_big_endian, sig);
            emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + src_mem) : src_mem);
            emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr0)");
         }
         else if (is_signed) {
            emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + sign_src_mem) : sign_src_mem);
            emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr0)");
            emit(&es_code, "    and #$80\n");
            emit_sign_fill_from_masked_a();
         }
         else {
            emit(&es_code, "    lda #$00\n");
         }
         emit(&es_code, "    pha\n");
      }
      for (int j = 0; j < dst_size; j++) {
         emit(&es_code, "    pla\n");
         emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + j) : j);
         emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
      }
      return;
   }

   for (int j = 0; j < dst_size; j++) {
      int sig = dst_big_endian ? (dst_size - 1 - j) : j;
      if (sig < src_size) {
         int src_mem = endian_mem_index_for_significance(src_size, src_big_endian, sig);
         emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + src_mem) : src_mem);
         emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr0)");
      }
      else if (is_signed) {
         emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + sign_src_mem) : sign_src_mem);
         emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr0)");
         emit(&es_code, "    and #$80\n");
         emit_sign_fill_from_masked_a();
      }
      else {
         emit(&es_code, "    lda #$00\n");
      }
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + j) : j);
      emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
   }
}

void emit_copy_symbol_to_fp_convert_offset(int dst_offset, int dst_size, const ASTNode *dst_type, const char *symbol, int src_offset, int src_size, const ASTNode *src_type) {
   bool src_big_endian = type_is_big_endian(src_type);
   bool dst_big_endian = type_is_big_endian(dst_type);
   bool is_signed = type_is_signed_integer(src_type);
   bool dst_direct;
   int sign_src_mem;
   const char *helper;

   if (dst_size <= 0 || src_size <= 0) {
      return;
   }

   dst_direct = dst_offset >= 0 && dst_offset + dst_size <= 256;
   sign_src_mem = endian_mem_index_for_significance(src_size, src_big_endian, src_size - 1);
   helper = runtime_copy_convert_helper_name(dst_size, dst_type, src_size, src_type);
   if (helper) {
      emit_load_address_to_ptr(0, symbol, src_offset);
      emit_prepare_fp_ptr(1, dst_offset);
      emit_runtime_copy_ptr0_to_ptr1(helper, src_size, dst_size);
      return;
   }
   if (!dst_direct) {
      emit_prepare_fp_ptr(1, dst_offset);
   }

   for (int j = 0; j < dst_size; j++) {
      int sig = dst_big_endian ? (dst_size - 1 - j) : j;
      if (sig < src_size) {
         int src_mem = endian_mem_index_for_significance(src_size, src_big_endian, sig);
         emit(&es_code, "    ldy #%d\n", src_offset + src_mem);
         emit(&es_code, "    lda %s,y\n", symbol);
      }
      else if (is_signed) {
         emit(&es_code, "    ldy #%d\n", src_offset + sign_src_mem);
         emit(&es_code, "    lda %s,y\n", symbol);
         emit(&es_code, "    and #$80\n");
         emit_sign_fill_from_masked_a();
      }
      else {
         emit(&es_code, "    lda #$00\n");
      }
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + j) : j);
      emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
   }
}

void emit_copy_symbol_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, const char *symbol, int src_size, const ASTNode *src_type) {
   emit_copy_symbol_to_fp_convert_offset(dst_offset, dst_size, dst_type, symbol, 0, src_size, src_type);
}

void remember_runtime_import(const char *name) {
   if (!runtime_imports) {
      runtime_imports = new_set();
   }
   if (!set_get(runtime_imports, name)) {
      set_add(runtime_imports, strdup(name), (void *)1);
      emit(&es_import, ".import _%s\n", name);
   }
}

void remember_symbol_import(const char *name) {
   if (!imported_symbols) {
      imported_symbols = new_set();
   }
   if (!set_get(imported_symbols, name)) {
      set_add(imported_symbols, strdup(name), (void *)1);
      emit(&es_import, ".import %s\n", name);
   }
}

void remember_symbol_import_mode(const char *name, bool is_zeropage) {
   char key[320];

   if (!name) {
      return;
   }
   if (!imported_symbols) {
      imported_symbols = new_set();
   }

   snprintf(key, sizeof(key), "%c:%s", is_zeropage ? 'Z' : 'A', name);
   if (!set_get(imported_symbols, key)) {
      set_add(imported_symbols, strdup(key), (void *)1);
      emit(&es_import,
           is_zeropage ? ".zpimport %s\n" : ".import %s\n",
           name);
   }
}



void ctx_push(Context *ctx, const ASTNode *type, const char *name) {
   ContextEntry *entry = (ContextEntry *) set_get(ctx->vars, name);
   if (entry != NULL) {
      error_user("[%s:%d.%d] duplicate symbol '%s' first defined at [%s:%d.%d]",
            type->file, type->line, type->column,
            name,
            entry->type->file, entry->type->line, entry->type->column);
   }

   entry = (ContextEntry *) malloc(sizeof(ContextEntry));
   entry->name = strdup(name);
   entry->is_static = false;
   entry->is_zeropage = false;
   entry->is_global = false;
   entry->is_ref = false;
   entry->is_absolute_ref = false;
   entry->read_expr = NULL;
   entry->write_expr = NULL;
   entry->type = type;
   entry->declarator = NULL;
   entry->size = get_size(type_name_from_node(type));
   entry->offset = ctx->locals;
   ctx->locals += entry->size;
   debug("[%s:%d] ctx_push(%s, %s, %d, %d)", __FILE__, __LINE__, type->strval, name, entry->size, entry->offset);
   set_add(ctx->vars, strdup(name), entry);
}

void ctx_resize_last_push(Context *ctx, const ASTNode *type, const ASTNode *declarator, const char *name) {
   ContextEntry *entry = (ContextEntry *) set_get(ctx->vars, name);
   int base_size;
   int value_size;

   if (!entry || !type) {
      return;
   }

   base_size = get_size(type_name_from_node(type));
   value_size = declarator_value_size(type, declarator);
   entry->size = value_size;
   entry->declarator = declarator;
   ctx->locals += (value_size - base_size);
}


void ctx_static(Context *ctx, const ASTNode *type, const char *name) {
   ContextEntry *entry = (ContextEntry *) set_get(ctx->vars, name);
   if (entry != NULL) {
      error_user("[%s:%d.%d] duplicate symbol '%s' first defined at [%s:%d.%d]",
            type->file, type->line, type->column,
            name,
            entry->type->file, entry->type->line, entry->type->column);
   }

   entry = (ContextEntry *) malloc(sizeof(ContextEntry));
   entry->name = strdup(name);
   entry->is_static = true;
   entry->is_zeropage = false;
   entry->is_global = false;
   entry->is_ref = false;
   entry->is_absolute_ref = false;
   entry->read_expr = NULL;
   entry->write_expr = NULL;
   entry->type = type;
   entry->declarator = NULL;
   entry->size = get_size(type_name_from_node(type));
   entry->offset = 0;
   debug("[%s:%d] ctx_static(%s, %s$%s, %d, %d)", __FILE__, __LINE__, type->strval, ctx->name, name, entry->size, entry->offset);
   set_add(ctx->vars, strdup(name), entry);
}

void ctx_zeropage(Context *ctx, const ASTNode *type, const char *name) {
   ContextEntry *entry = (ContextEntry *) set_get(ctx->vars, name);
   if (entry != NULL) {
      error_user("[%s:%d.%d] duplicate symbol '%s' first defined at [%s:%d.%d]",
            type->file, type->line, type->column,
            name,
            entry->type->file, entry->type->line, entry->type->column);
   }

   entry = (ContextEntry *) malloc(sizeof(ContextEntry));
   entry->name = strdup(name);
   entry->is_static = false;
   entry->is_zeropage = true;
   entry->is_global = false;
   entry->is_ref = false;
   entry->is_absolute_ref = false;
   entry->read_expr = NULL;
   entry->write_expr = NULL;
   entry->type = type;
   entry->declarator = NULL;
   entry->size = get_size(type_name_from_node(type));
   entry->offset = 0;
   debug("[%s:%d] ctx_zeropage(%s, %s$%s, %d, %d)", __FILE__, __LINE__, type->strval, ctx->name, name, entry->size, entry->offset);
   set_add(ctx->vars, strdup(name), entry);
}

// caution, returns pointer to static buffer overwritten w/ each call





void emit_prepare_fp_ptr(int ptrno, int offset) {
   static const char *plus_helpers[] = { "fp2ptr0p", "fp2ptr1p", "fp2ptr2p", "fp2ptr3p" };
   static const char *minus_helpers[] = { "fp2ptr0m", "fp2ptr1m", "fp2ptr2m", "fp2ptr3m" };
   const char *helper;
   int abs_offset = offset < 0 ? -offset : offset;

   if (ptrno < 0 || ptrno > 3) {
      ptrno = 0;
   }

   emit(&es_code, "    lda #$%02x\n", abs_offset & 0xff);
   emit(&es_code, "    sta arg0\n");

   helper = offset < 0 ? minus_helpers[ptrno] : plus_helpers[ptrno];
   remember_runtime_import(helper);
   emit(&es_code, "    jsr _%s\n", helper);
}

void emit_load_address_to_ptr(int ptrno, const char *symbol, int addend) {
   emit(&es_code, "    lda #<(%s + %d)\n", symbol, addend);
   emit(&es_code, "    sta ptr%d\n", ptrno);
   emit(&es_code, "    lda #>(%s + %d)\n", symbol, addend);
   emit(&es_code, "    sta ptr%d+1\n", ptrno);
}

const char *assembler_address_expr(const char *expr, char *buf, size_t buf_size) {
   const char *p = expr;
   bool neg = false;

   if (!expr || !*expr) {
      if (buf_size > 0) {
         buf[0] = '\0';
      }
      return expr;
   }

   if (*p == '-') {
      neg = true;
      p++;
   }

   if (*p >= '0' && *p <= '9') {
      if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
         snprintf(buf, buf_size, "%s$%s", neg ? "-" : "", p + 2);
         return buf;
      }
      if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
         snprintf(buf, buf_size, "%s%%%s", neg ? "-" : "", p + 2);
         return buf;
      }
      snprintf(buf, buf_size, "%s%s", neg ? "-" : "", p);
      return buf;
   }

   return expr;
}

void emit_load_expr_address_to_ptr(int ptrno, const char *expr, int addend) {
   char expr_buf[256];
   const char *asm_expr = assembler_address_expr(expr, expr_buf, sizeof(expr_buf));

   emit(&es_code, "    lda #<(%s + %d)\n", asm_expr, addend);
   emit(&es_code, "    sta ptr%d\n", ptrno);
   emit(&es_code, "    lda #>(%s + %d)\n", asm_expr, addend);
   emit(&es_code, "    sta ptr%d+1\n", ptrno);
}

void emit_load_ptr_from_symbol(int ptrno, const char *symbol, int addend) {
   emit(&es_code, "    ldy #0\n");
   emit(&es_code, "    lda %s + %d,y\n", symbol, addend);
   emit(&es_code, "    sta ptr%d\n", ptrno);
   emit(&es_code, "    iny\n");
   emit(&es_code, "    lda %s + %d,y\n", symbol, addend);
   emit(&es_code, "    sta ptr%d+1\n", ptrno);
}

void emit_deref_ptr(int ptrno) {
   emit(&es_code, "    ldy #0\n");
   emit(&es_code, "    lda (ptr%d),y\n", ptrno);
   emit(&es_code, "    sta arg0\n");
   emit(&es_code, "    iny\n");
   emit(&es_code, "    lda (ptr%d),y\n", ptrno);
   emit(&es_code, "    sta arg1\n");
   emit(&es_code, "    lda arg0\n");
   emit(&es_code, "    sta ptr%d\n", ptrno);
   emit(&es_code, "    lda arg1\n");
   emit(&es_code, "    sta ptr%d+1\n", ptrno);
}

void emit_add_fp_to_ptr(int ptrno, int src_offset, int src_size) {
   bool direct = src_offset >= 0 && src_offset + src_size <= 256;
   int src_ptr = ptrno == 0 ? 1 : 0;
   int ptr_size = get_size("*");

   if (!direct) {
      emit_prepare_fp_ptr(src_ptr, src_offset);
   }

   emit(&es_code, "    clc\n");
   for (int i = 0; i < ptr_size; i++) {
      emit(&es_code, "    lda ptr%d%s\n", ptrno, i == 0 ? "" : "+1");
      if (i < src_size) {
         emit(&es_code, "    ldy #%d\n", direct ? (src_offset + i) : i);
         emit(&es_code, "    adc %s,y\n", direct ? "(fp)" : (src_ptr == 0 ? "(ptr0)" : "(ptr1)"));
      }
      else {
         emit(&es_code, "    adc #0\n");
      }
      emit(&es_code, "    sta ptr%d%s\n", ptrno, i == 0 ? "" : "+1");
   }
}

void emit_store_immediate_to_fp(int offset, const unsigned char *bytes, int size) {
   if (offset >= 0 && offset + size <= 256) {
      for (int i = 0; i < size; i++) {
         emit(&es_code, "    ldy #%d\n", offset + i);
         emit(&es_code, "    lda #$%02x\n", bytes[i]);
         emit(&es_code, "    sta (fp),y\n");
      }
      return;
   }

   emit_prepare_fp_ptr(0, offset);
   for (int i = 0; i < size; i++) {
      emit(&es_code, "    ldy #%d\n", i);
      emit(&es_code, "    lda #$%02x\n", bytes[i]);
      emit(&es_code, "    sta (ptr0),y\n");
   }
}
