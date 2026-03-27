#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "compile.h"
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

EmitSink es_header = EMIT_INIT;
EmitSink es_import = EMIT_INIT;
EmitSink es_export = EMIT_INIT;
EmitSink es_code   = EMIT_INIT;
EmitSink es_rodata = EMIT_INIT;
EmitSink es_data   = EMIT_INIT;
EmitSink es_bss    = EMIT_INIT;
EmitSink es_zp     = EMIT_INIT;
EmitSink es_zpdata = EMIT_INIT;

Pair *typesizes = NULL;

Set *globals = NULL;
Set *functions = NULL;
Set *runtime_imports = NULL;
static int label_counter = 0;
static const char *loop_break_stack[128];
static const char *loop_continue_stack[128];
static int loop_depth = 0;
static const char *named_loop_names[128];
static const char *named_loop_break_stack[128];
static const char *named_loop_continue_stack[128];
static int named_loop_depth = 0;
static const char *pending_loop_label_name = NULL;

typedef struct ContextEntry {
   const char *name;
   const ASTNode *type;
   const ASTNode *declarator;
   bool is_static;
   bool is_quick;
   bool is_global;
   int offset;
   int size;
} ContextEntry;

typedef struct LValueRef {
   const char *name;
   const ASTNode *type;
   const ASTNode *declarator;
   bool is_static;
   bool is_quick;
   bool is_global;
   bool indirect;
   int offset;
   int size;
   int ptr_adjust;
} LValueRef;

typedef struct Context {
   const char *name;
   int locals;
   int params;
   Set *vars;
   const char *break_label;
   const char *continue_label;
} Context;

static const ASTNode *global_decl_lookup(const char *name);
static bool entry_symbol_name(Context *ctx, const ContextEntry *entry, char *buf, size_t bufsize);

static void remember_function(const ASTNode *node, const char *name);
static void predeclare_top_level_functions(ASTNode *program);
static int declarator_storage_size(const ASTNode *type, const ASTNode *declarator);
static bool compile_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst);
static void compile_statement_list(ASTNode *node, Context *ctx);
static bool compile_condition_branch_false(ASTNode *expr, Context *ctx, const char *false_label);
static const char *next_label(const char *prefix);
static void compile_if_stmt(ASTNode *node, Context *ctx);
static void compile_while_stmt(ASTNode *node, Context *ctx);
static void compile_for_stmt(ASTNode *node, Context *ctx);
static void compile_break_stmt(ASTNode *node, Context *ctx);
static void compile_continue_stmt(ASTNode *node, Context *ctx);
static void compile_do_stmt(ASTNode *node, Context *ctx);
static void compile_label_stmt(ASTNode *node, Context *ctx);
static void compile_goto_stmt(ASTNode *node, Context *ctx);
static void compile_switch_stmt(ASTNode *node, Context *ctx);
static void compile_return_stmt(ASTNode *node, Context *ctx);
static void compile_expr(ASTNode *node, Context *ctx);
static const ASTNode *function_return_type(const ASTNode *fn);
static const ASTNode *function_declarator_node(const ASTNode *fn);
static bool declarator_is_function(const ASTNode *declarator);
static bool resolve_lvalue(Context *ctx, ASTNode *node, LValueRef *out);
static void calculate_struct_union_sizes(ASTNode *program);
static bool compile_initializer_to_fp(const ASTNode *init, Context *ctx, const ASTNode *type, const ASTNode *declarator, int base_offset, int total_size);
static bool build_initializer_bytes(unsigned char *buf, int buf_size, int base_offset, const ASTNode *init, const ASTNode *type, const ASTNode *declarator, int total_size);
static const ASTNode *expr_value_type(ASTNode *expr, Context *ctx);
static int expr_value_size(ASTNode *expr, Context *ctx);
static const ASTNode *expr_value_declarator(ASTNode *expr, Context *ctx);
static void emit_prepare_fp_ptr(int ptrno, int offset);
static int expr_byte_index(const ASTNode *type, int size, int i);

static ContextEntry *ctx_lookup(Context *ctx, const char *name) {
   return ctx ? (ContextEntry *) set_get(ctx->vars, name) : NULL;
}


static const ASTNode *global_decl_lookup(const char *name) {
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

static bool entry_symbol_name(Context *ctx, const ContextEntry *entry, char *buf, size_t bufsize) {
   if (!entry || !entry->name || !buf || bufsize < 8) {
      return false;
   }
   if (entry->is_global) {
      snprintf(buf, bufsize, "_%s", entry->name);
      return true;
   }
   if (entry->is_static || entry->is_quick) {
      snprintf(buf, bufsize, "_%s$%s", ctx && ctx->name ? ctx->name : "", entry->name);
      return true;
   }
   return false;
}

static void emit_copy_symbol_to_fp(int dst_offset, const char *symbol, int size) {
   bool dst_direct = dst_offset >= 0 && dst_offset + size <= 256;
   if (!dst_direct) {
      emit_prepare_fp_ptr(1, dst_offset);
   }
   for (int i = 0; i < size; i++) {
      emit(&es_code, "    ldy #%d\n", i);
      emit(&es_code, "    lda %s,y\n", symbol);
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + i) : i);
      emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
   }
}

static void emit_copy_fp_to_symbol(const char *symbol, int src_offset, int size) {
   bool src_direct = src_offset >= 0 && src_offset + size <= 256;
   if (!src_direct) {
      emit_prepare_fp_ptr(1, src_offset);
   }
   for (int i = 0; i < size; i++) {
      emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + i) : i);
      emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr1)");
      emit(&es_code, "    ldy #%d\n", i);
      emit(&es_code, "    sta %s,y\n", symbol);
   }
}

static void emit_bytes_direct(EmitSink *es, const unsigned char *bytes, int size) {
   if (!es || !bytes || size <= 0) {
      return;
   }
   emit(es, "	.byte $%02x", bytes[0]);
   for (int i = 1; i < size; i++) {
      emit(es, ", $%02x", bytes[i]);
   }
   emit(es, "\n");
}

static void emit_add_immediate_to_symbol(const ASTNode *type, const char *symbol, const unsigned char *bytes, int size) {
   emit(&es_code, "    clc\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", j);
      emit(&es_code, "    lda %s,y\n", symbol);
      emit(&es_code, "    adc #$%02x\n", bytes[j]);
      emit(&es_code, "    sta %s,y\n", symbol);
   }
}

static void emit_sub_immediate_from_symbol(const ASTNode *type, const char *symbol, const unsigned char *bytes, int size) {
   emit(&es_code, "    sec\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", j);
      emit(&es_code, "    lda %s,y\n", symbol);
      emit(&es_code, "    sbc #$%02x\n", bytes[j]);
      emit(&es_code, "    sta %s,y\n", symbol);
   }
}

static void emit_add_symbol_to_symbol(const ASTNode *type, const char *dst_symbol, const char *src_symbol, int size) {
   emit(&es_code, "    clc\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", j);
      emit(&es_code, "    lda %s,y\n", dst_symbol);
      emit(&es_code, "    adc %s,y\n", src_symbol);
      emit(&es_code, "    sta %s,y\n", dst_symbol);
   }
}

static void emit_sub_symbol_from_symbol(const ASTNode *type, const char *dst_symbol, const char *src_symbol, int size) {
   emit(&es_code, "    sec\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", j);
      emit(&es_code, "    lda %s,y\n", dst_symbol);
      emit(&es_code, "    sbc %s,y\n", src_symbol);
      emit(&es_code, "    sta %s,y\n", dst_symbol);
   }
}

static void remember_runtime_import(const char *name) {
   if (!runtime_imports) {
      runtime_imports = new_set();
   }
   if (!set_get(runtime_imports, name)) {
      set_add(runtime_imports, strdup(name), (void *)1);
      emit(&es_import, ".import _%s\n", name);
   }
}

static void remember_symbol_import(const char *name) {
   if (!globals) {
      globals = new_set();
   }
   if (!set_get(globals, name)) {
      set_add(globals, strdup(name), (void *)1);
      emit(&es_import, ".import _%s\n", name);
   }
}

static void push_loop_labels(const char *break_label, const char *continue_label) {
   if (loop_depth < (int)(sizeof(loop_break_stack) / sizeof(loop_break_stack[0]))) {
      loop_break_stack[loop_depth] = break_label;
      loop_continue_stack[loop_depth] = continue_label;
      loop_depth++;
   }
}

static void pop_loop_labels(void) {
   if (loop_depth > 0) {
      loop_depth--;
      loop_break_stack[loop_depth] = NULL;
      loop_continue_stack[loop_depth] = NULL;
   }
}

static const char *current_break_label(void) {
   return loop_depth > 0 ? loop_break_stack[loop_depth - 1] : NULL;
}

static const char *current_continue_label(void) {
   return loop_depth > 0 ? loop_continue_stack[loop_depth - 1] : NULL;
}

static void push_named_loop_labels(const char *name, const char *break_label, const char *continue_label) {
   if (!name) {
      return;
   }
   if (named_loop_depth < (int)(sizeof(named_loop_names) / sizeof(named_loop_names[0]))) {
      named_loop_names[named_loop_depth] = name;
      named_loop_break_stack[named_loop_depth] = break_label;
      named_loop_continue_stack[named_loop_depth] = continue_label;
      named_loop_depth++;
   }
}

static void pop_named_loop_labels(void) {
   if (named_loop_depth > 0) {
      named_loop_depth--;
      named_loop_names[named_loop_depth] = NULL;
      named_loop_break_stack[named_loop_depth] = NULL;
      named_loop_continue_stack[named_loop_depth] = NULL;
   }
}

static const char *lookup_named_break_label(const char *name) {
   if (!name) {
      return NULL;
   }
   for (int i = named_loop_depth - 1; i >= 0; i--) {
      if (named_loop_names[i] && !strcmp(named_loop_names[i], name)) {
         return named_loop_break_stack[i];
      }
   }
   return NULL;
}

static const char *lookup_named_continue_label(const char *name) {
   if (!name) {
      return NULL;
   }
   for (int i = named_loop_depth - 1; i >= 0; i--) {
      if (named_loop_names[i] && !strcmp(named_loop_names[i], name)) {
         return named_loop_continue_stack[i];
      }
   }
   return NULL;
}

static const char *type_name_from_node(const ASTNode *type) {
   if (!type) {
      return "int";
   }
   if (type->strval) {
      return type->strval;
   }
   if (type->count > 0 && type->children[0] && type->children[0]->strval) {
      return type->children[0]->strval;
   }
   return "int";
}

// for parameterless flags (e.g. "$signed")
// also for complete flags (e.g. "$endian:little")
static bool has_flag(const char *type, const char *flag) {
   const ASTNode *node = get_typename_node(type);
   if (!node || node->count < 2 || is_empty(node->children[1])) {
      return false;
   }

   const ASTNode *flags = node->children[1];
   for (int i = 0; i < flags->count; i++) {
      if (!strcmp(flags->children[i]->strval, flag)) {
         return true;
      }
   }
   return false;
}

static bool has_modifier(ASTNode *node, const char *modifier) {
   if (!node || is_empty(node)) {
      return false;
   }

   for (int i = 0; i < node->count; i++) {
      if (!strcmp(modifier, node->children[i]->strval)) {
         return true;
      }
   }
   return false;
}

static int get_size(const char *type) {
   const ASTNode *node;

   if (!type) {
      error("[%s:%d] internal could not find NULL type", __FILE__, __LINE__);
   }

   if (typesizes && pair_exists(typesizes, type)) {
      return (int)(intptr_t) pair_get(typesizes, type);
   }

   node = get_typename_node(type);
   if (!node) {
      error("[%s:%d] internal could not find '%s'", __FILE__, __LINE__, type);
   }

   if (!strcmp(node->name, "type_decl_stmt")) {
      if (node->count < 2 || is_empty(node->children[1])) {
         error("[%s:%d] internal could not find '%s'", __FILE__, __LINE__, type);
      }

      const ASTNode *flags = node->children[1];
      for (int i = 0; i < flags->count; i++) {
         if (!strncmp(flags->children[i]->strval, "$size:", 6)) {
            return atoi(flags->children[i]->strval + 6);
         }
      }
   }
   else if (!strcmp(node->name, "struct_decl_stmt") || !strcmp(node->name, "union_decl_stmt")) {
      calculate_struct_union_sizes(root);
      if (typesizes && pair_exists(typesizes, type)) {
         return (int)(intptr_t) pair_get(typesizes, type);
      }
   }

   error("[%s:%d] internal could not find '%s'", __FILE__, __LINE__, type);
   return -1;
}

#if 0
// decl_stmt is a free floating variable declaration
static void compile_decl_stmt(ASTNode *node) {
   //debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   //parse_dump_node(node);

   ASTNode *modifiers    = node->children[0];
   const char *type      = node->children[1]->strval;
   const char *name      = node->children[2]->strval;
   //ASTNode *dimension    = node->children[3];
   //const char *location  = node->children[4]->strval;
   ASTNode *expression   = node->children[5];

   if (!globals) {
      globals = new_set();
   }

   const ASTNode *value = set_get(globals, name);
   if (value != NULL) {
      error("[%s:%d.%d] duplicate symbol '%s' first defined at [%s:%d.%d]",
            node->file, node->line, node->column,
            name,
            value->file, value->line, value->column);
   }
   set_add(globals, name, node);

   bool is_extern = has_modifier(modifiers, "extern");
   bool is_const  = has_modifier(modifiers, "const");
   bool is_static = has_modifier(modifiers, "static");
   bool is_quick  = has_modifier(modifiers, "quick");
   bool is_ref    = has_modifier(modifiers, "ref");

   if (is_ref) {
      error("[%s:%d.%d] 'ref' not allowed in decl_stmt",
            node->file, node->line, node->column);
   }

#if 0
   printf("=");
   if (is_extern) {
      printf("extern ");
   }
   if (is_const) {
      printf("const ");
   }
   if (is_static) {
      printf("static ");
   }
   if (is_quick) {
      printf("quick ");
   }
   if (is_ref) {
      printf("ref ");
   }
   printf("%s %s %p @%s %p\n", type, name, dimension, location, expression);
#endif

   int size = get_size(type);

   if (is_extern) {
      if (is_static) {
         error("[%s:%d.%d] 'extern' and 'static' don't mix",
               node->file, node->line, node->column);
      }

      if (is_quick) {
         emit(&es_import, ".zpimport _%s\n", name);
      }
      else {
         emit(&es_import, ".import _%s\n", name);
      }
   }
   else {
      if (!is_static) {
         if (is_quick) {
            emit(&es_export, ".zpexport _%s\n", name);
         }
         else {
            emit(&es_export, ".export _%s\n", name);
         }
      }
      if (expression == NULL) {
         if (is_const) {
            error("[%s:%d.%d] 'const' missing initializer",
                  node->file, node->line, node->column);
         }
         if (is_quick) {
            emit(&es_zp, "_%s:\n", name);
            // TODO FIX multiply "size" by "dimension" if necessary
            emit(&es_zp, "\t.res %d\n", size);
         }
         else {
            emit(&es_bss, "_%s:\n", name);
            // TODO FIX multiply "size" by "dimension" if necessary
            emit(&es_bss, "\t.res %d\n", size);
         }
      }
      else {
         EmitSink *es;
         if (is_const) {
            es = &es_rodata;
         }
         else {
            if (is_quick) {
               es = &es_zpdata;
            }
            else {
               es = &es_data;
            }
         }
         emit(es, "_%s:\n", name);
         // TODO FIX multiply "size" by "dimension" if necessary
         if (has_flag(type, "$signed") || has_flag(type, "$unsigned")) {
            expression = expression->children[0];
            // an integer
            bool neg = false;
            if (!strcmp(expression->name, "-")) {
               if (has_flag(type, "$unsigned")) {
                  warning("[%s:%d.%d] negative initializer for unsigned symbol",
                        node->file, node->line, node->column);
               }
               neg = true;
               expression = expression->children[0];
            }
            if (!strcmp(expression->name, "int")) {
               unsigned char *bytes = (unsigned char *) malloc(sizeof(unsigned char) * size);

               if (has_flag(type, "$endian:big")) {
                  make_be_int(expression->strval, bytes, size); // TODO FIX check return value
                  if (bytes[size - 1] & 0x80) {
                     warning("[%s:%d.%d] possible overflow of unsigned initializer",
                           node->file, node->line, node->column);
                  }
                  if (neg) {
                     negate_be_int(bytes, size);
                  }
               }
               else {
                  make_le_int(expression->strval, bytes, size); // TODO FIX check return value
                  if (bytes[size - 1] & 0x80) {
                     warning("[%s:%d.%d] possible overflow of unsigned initializer",
                           node->file, node->line, node->column);
                  }
                  if (neg) {
                     negate_le_int(bytes, size);
                  }
               }
               emit(es, "\t.byte $%02x", bytes[0]);
               for (int i = 1; i < size; i++) {
                  emit(es, ", $%02x", bytes[i]);
               }
               emit(es, " ; integer\n");
            }
            else {
               warning("[%s:%d] complex initializers not implemented (yet)", __FILE__, __LINE__);
               emit(es, "\t.res %d, $00 ; integer\n", size); // TODO FIX change to initializer
            }
         }
         else if (has_flag(type, "$float")) {
            expression = expression->children[0];
            // a float
            bool neg = false;
            if (!strcmp(expression->name, "-")) {
               neg = true;
               expression = expression->children[0];
            }
            if (!strcmp(expression->name, "float")) {
               unsigned char *bytes = (unsigned char *) malloc(sizeof(unsigned char) * size);

               if (has_flag(type, "$endian:big")) {
                  make_be_float(expression->strval, bytes, size); // TODO FIX check return value
                  if (neg) {
                     negate_be_float(bytes, size);
                  }
               }
               else {
                  make_le_float(expression->strval, bytes, size); // TODO FIX check return value
                  if (neg) {
                     negate_le_float(bytes, size);
                  }
               }

               emit(es, "\t.byte $%02x", bytes[0]);
               for (int i = 1; i < size; i++) {
                  emit(es, ", $%02x", bytes[i]);
               }
               emit(es, " ; float\n");
            }
            else {
               warning("[%s:%d] complex initializers not implemented (yet)", __FILE__, __LINE__);
               emit(es, "\t.res %d, $00 ; float\n", size); // TODO FIX change to initializer
            }
         }
         else {
            emit(es, "\t.res %d, $00 ; huh?\n", size); // TODO FIX change to initializer
         }
      }
   }

   return;
}
#endif

#if 0
static bool function_prototype_match(const ASTNode *a, const ASTNode *b) {
   // constness must match
   if (has_modifier(a->children[0], "const") !=
         has_modifier(b->children[0], "const")) {
      return false;
   }
   // return type must match
   if (strcmp(a->children[1]->strval, b->children[1]->strval)) {
      return false;
   }
   // param types must match
   a = a->children[3];
   b = b->children[3];
   while (a && b) {
      if (a->kind != b-> kind) {
         return false;
      }
      if (a->children[0] && b->children[0]) {
         if (has_modifier(a->children[0]->children[0], "const") !=
               has_modifier(b->children[0]->children[0], "const")) {
            return false;
         }
         if (has_modifier(a->children[0]->children[0], "static") !=
               has_modifier(b->children[0]->children[0], "static")) {
            return false;
         }
         if (strcmp(a->children[0]->children[1]->strval,
                  b->children[0]->children[1]->strval)) {
            return false;
         }
      }
      else {
         return false;
      }
      a = a->children[1];
      b = b->children[1];
   }

   return true;
}
#endif

static void ctx_shove(Context *ctx, const ASTNode *type, const char *name) {
   ContextEntry *entry = (ContextEntry *) set_get(ctx->vars, name);
   if (entry != NULL) {
      error("[%s:%d.%d] duplicate symbol '%s' first defined at [%s:%d.%d]",
            type->file, type->line, type->column,
            name,
            entry->type->file, entry->type->line, entry->type->column);
   }

   entry = (ContextEntry *) malloc(sizeof(ContextEntry));
   entry->name = strdup(name);
   entry->is_static = false;
   entry->is_quick = false;
   entry->is_global = false;
   entry->type = type;
   entry->declarator = NULL;
   entry->size = get_size(type_name_from_node(type));
   ctx->params -= entry->size;
   entry->offset = ctx->params;
   debug("[%s:%d] ctx_shove(%s, %s, %d, %d)", __FILE__, __LINE__, type->strval, name, entry->size, entry->offset);
   set_add(ctx->vars, strdup(name), entry);
}

static void ctx_push(Context *ctx, const ASTNode *type, const char *name) {
   ContextEntry *entry = (ContextEntry *) set_get(ctx->vars, name);
   if (entry != NULL) {
      error("[%s:%d.%d] duplicate symbol '%s' first defined at [%s:%d.%d]",
            type->file, type->line, type->column,
            name,
            entry->type->file, entry->type->line, entry->type->column);
   }

   entry = (ContextEntry *) malloc(sizeof(ContextEntry));
   entry->name = strdup(name);
   entry->is_static = false;
   entry->is_quick = false;
   entry->is_global = false;
   entry->type = type;
   entry->declarator = NULL;
   entry->size = get_size(type_name_from_node(type));
   entry->offset = ctx->locals;
   ctx->locals += entry->size;
   debug("[%s:%d] ctx_push(%s, %s, %d, %d)", __FILE__, __LINE__, type->strval, name, entry->size, entry->offset);
   set_add(ctx->vars, strdup(name), entry);

   // TODO FIX increment the stack pointer.
}


static void ctx_static(Context *ctx, const ASTNode *type, const char *name, bool bss) {
   ContextEntry *entry = (ContextEntry *) set_get(ctx->vars, name);
   if (entry != NULL) {
      error("[%s:%d.%d] duplicate symbol '%s' first defined at [%s:%d.%d]",
            type->file, type->line, type->column,
            name,
            entry->type->file, entry->type->line, entry->type->column);
   }

   entry = (ContextEntry *) malloc(sizeof(ContextEntry));
   entry->name = strdup(name);
   entry->is_static = true;
   entry->is_quick = false;
   entry->is_global = false;
   entry->type = type;
   entry->declarator = NULL;
   entry->size = get_size(type_name_from_node(type));
   entry->offset = 0;
   debug("[%s:%d] ctx_static(%s, %s$%s, %d, %d)", __FILE__, __LINE__, type->strval, ctx->name, name, entry->size, entry->offset);
   set_add(ctx->vars, strdup(name), entry);

   if (bss) {
      emit(&es_bss, "_%s$%s:\n", ctx->name, name);
      // TODO FIX multiply "size" by "dimension" if necessary
      emit(&es_bss, "\t.res %d\n", entry->size);
   }
   else {
      // TODO FIX allocate storage
      // TDO FIX how do we initialize it ???
      emit(&es_data, "_%s$%s:\n", ctx->name, name);
      // TODO FIX multiply "size" by "dimension" if necessary
      emit(&es_data, "\t.res %d\n", entry->size); // TODO FIX this is wrong!
   }
}

static void ctx_quick(Context *ctx, const ASTNode *type, const char *name, bool bss) {
   ContextEntry *entry = (ContextEntry *) set_get(ctx->vars, name);
   if (entry != NULL) {
      error("[%s:%d.%d] duplicate symbol '%s' first defined at [%s:%d.%d]",
            type->file, type->line, type->column,
            name,
            entry->type->file, entry->type->line, entry->type->column);
   }

   entry = (ContextEntry *) malloc(sizeof(ContextEntry));
   entry->name = strdup(name);
   entry->is_static = false;
   entry->is_quick = true;
   entry->is_global = false;
   entry->type = type;
   entry->declarator = NULL;
   entry->size = get_size(type_name_from_node(type));
   entry->offset = 0;
   debug("[%s:%d] ctx_quick(%s, %s$%s, %d, %d)", __FILE__, __LINE__, type->strval, ctx->name, name, entry->size, entry->offset);
   set_add(ctx->vars, strdup(name), entry);

   if (bss) {
      emit(&es_bss, "_%s$%s:\n", ctx->name, name);
      // TODO FIX multiply "size" by "dimension" if necessary
      emit(&es_bss, "\t.res %d\n", entry->size);
   }
   else {
      // TODO FIX allocate storage
      // TDO FIX how do we initialize it ???
      emit(&es_data, "_%s$%s:\n", ctx->name, name);
      // TODO FIX multiply "size" by "dimension" if necessary
      emit(&es_data, "\t.res %d\n", entry->size); // TODO FIX this is wrong!
   }
}

// caution, returns pointer to static buffer overwritten w/ each call
static const char *missing_argname(int i) {
   static char ret[16];
   sprintf(ret, "$%d", i);
   return ret;
}

static const ASTNode *parameter_decl_specifiers(const ASTNode *parameter) {
   return parameter->count > 0 ? parameter->children[0] : NULL;
}

static const ASTNode *parameter_decl_item(const ASTNode *parameter) {
   return parameter->count > 1 ? parameter->children[1] : NULL;
}

static const ASTNode *parameter_type(const ASTNode *parameter) {
   const ASTNode *decl_specs = parameter_decl_specifiers(parameter);
   return (decl_specs && decl_specs->count > 1) ? decl_specs->children[1] : NULL;
}

static const ASTNode *parameter_declarator(const ASTNode *parameter) {
   const ASTNode *decl_item = parameter_decl_item(parameter);
   return (decl_item && decl_item->count > 0) ? decl_item->children[0] : NULL;
}

static const char *parameter_name(const ASTNode *parameter, int i) {
   const ASTNode *declarator = parameter_declarator(parameter);
   if (!declarator || declarator->count < 2 || is_empty(declarator->children[1])) {
      return missing_argname(i);
   }
   return declarator->children[1]->strval;
}

static bool parameter_is_void(const ASTNode *parameter) {
   const ASTNode *type = parameter_type(parameter);
   const ASTNode *declarator = parameter_declarator(parameter);

   if (!type || strcmp(type->strval, "void")) {
      return false;
   }

   if (!declarator || declarator->count < 2 || !is_empty(declarator->children[1])) {
      return false;
   }

   return true;
}

static void build_function_context(const ASTNode *node, Context *ctx) {
   const ASTNode *declarator = node->children[1];
   const ASTNode *params = (declarator->count > 2) ? declarator->children[2] : NULL;
   int i = 0;

   if (params && !is_empty(params)) {
      for (int j = 0; j < params->count; j++) {
         const ASTNode *parameter = params->children[j];
         const ASTNode *type = parameter_type(parameter);
         const char *name = parameter_name(parameter, i);
         const ASTNode *decl_specs = parameter_decl_specifiers(parameter);
         const ASTNode *param_decl = parameter_declarator(parameter);
         int size;

         if (!type || parameter_is_void(parameter)) {
            continue;
         }

         size = declarator_storage_size(type, param_decl);
         if (has_modifier((ASTNode *) decl_specs->children[0], "static")) {
            ctx_static(ctx, type, name, true);
            ((ContextEntry *) set_get(ctx->vars, name))->size = size;
            ((ContextEntry *) set_get(ctx->vars, name))->declarator = param_decl;
         }
         else if (has_modifier((ASTNode *) decl_specs->children[0], "quick")) {
            ctx_quick(ctx, type, name, true);
            ((ContextEntry *) set_get(ctx->vars, name))->size = size;
            ((ContextEntry *) set_get(ctx->vars, name))->declarator = param_decl;
         }
         else {
            ctx_shove(ctx, type, name);
            ((ContextEntry *) set_get(ctx->vars, name))->size = size;
            ((ContextEntry *) set_get(ctx->vars, name))->declarator = param_decl;
            ((ContextEntry *) set_get(ctx->vars, name))->offset = ctx->params + get_size(type_name_from_node(type)) - size;
            ctx->params -= (size - get_size(type_name_from_node(type)));
         }
         i++;
      }
   }

   ctx_shove(ctx, node->children[0]->children[1], "$$");
}

static void emit_prepare_fp_ptr(int ptrno, int offset) {
   int abs_offset = offset < 0 ? -offset : offset;

   emit(&es_code, "    lda #$%02x\n", abs_offset & 0xff);
   emit(&es_code, "    sta arg0\n");

   if (offset < 0) {
      remember_runtime_import(ptrno == 0 ? "fp2ptr0m" : "fp2ptr1m");
      emit(&es_code, "    jsr _%s\n", ptrno == 0 ? "fp2ptr0m" : "fp2ptr1m");
   }
   else {
      remember_runtime_import(ptrno == 0 ? "fp2ptr0p" : "fp2ptr1p");
      emit(&es_code, "    jsr _%s\n", ptrno == 0 ? "fp2ptr0p" : "fp2ptr1p");
   }
}

static void emit_store_immediate_to_fp(int offset, const unsigned char *bytes, int size) {
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

static void emit_copy_fp_to_fp(int dst_offset, int src_offset, int size) {
   bool dst_direct = dst_offset >= 0 && dst_offset + size <= 256;
   bool src_direct = src_offset >= 0 && src_offset + size <= 256;

   if (!src_direct) {
      emit_prepare_fp_ptr(0, src_offset);
   }
   if (!dst_direct) {
      emit_prepare_fp_ptr(1, dst_offset);
   }

   for (int i = 0; i < size; i++) {
      emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + i) : i);
      emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr0)");
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + i) : i);
      emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
   }
}

static const ASTNode *unwrap_expr_node(const ASTNode *expr) {
   while (expr && expr->count == 1 &&
          (!strcmp(expr->name, "expr") ||
           !strcmp(expr->name, "assign_expr") ||
           !strcmp(expr->name, "conditional_expr") ||
           !strcmp(expr->name, "initializer") ||
           !strcmp(expr->name, "opt_expr"))) {
      expr = expr->children[0];
   }
   return expr;
}

static int expr_byte_index(const ASTNode *type, int size, int i) {
   if (has_flag(type_name_from_node(type), "$endian:big")) {
      return size - 1 - i;
   }
   return i;
}

static void emit_add_immediate_to_fp(const ASTNode *type, int offset, const unsigned char *bytes, int size) {
   bool direct = offset >= 0 && offset + size <= 256;

   if (!direct) {
      emit_prepare_fp_ptr(0, offset);
   }

   emit(&es_code, "    clc\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", direct ? (offset + j) : j);
      emit(&es_code, "    lda %s,y\n", direct ? "(fp)" : "(ptr0)");
      emit(&es_code, "    adc #$%02x\n", bytes[j]);
      emit(&es_code, "    sta %s,y\n", direct ? "(fp)" : "(ptr0)");
   }
}

static void emit_sub_immediate_from_fp(const ASTNode *type, int offset, const unsigned char *bytes, int size) {
   bool direct = offset >= 0 && offset + size <= 256;

   if (!direct) {
      emit_prepare_fp_ptr(0, offset);
   }

   emit(&es_code, "    sec\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", direct ? (offset + j) : j);
      emit(&es_code, "    lda %s,y\n", direct ? "(fp)" : "(ptr0)");
      emit(&es_code, "    sbc #$%02x\n", bytes[j]);
      emit(&es_code, "    sta %s,y\n", direct ? "(fp)" : "(ptr0)");
   }
}

static void emit_add_fp_to_fp(const ASTNode *type, int dst_offset, int src_offset, int size) {
   bool dst_direct = dst_offset >= 0 && dst_offset + size <= 256;
   bool src_direct = src_offset >= 0 && src_offset + size <= 256;

   if (!dst_direct) {
      emit_prepare_fp_ptr(0, dst_offset);
   }
   if (!src_direct) {
      emit_prepare_fp_ptr(1, src_offset);
   }

   emit(&es_code, "    clc\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + j) : j);
      emit(&es_code, "    lda %s,y\n", dst_direct ? "(fp)" : "(ptr0)");
      emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + j) : j);
      emit(&es_code, "    adc %s,y\n", src_direct ? "(fp)" : "(ptr1)");
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + j) : j);
      emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr0)");
   }
}

static void emit_sub_fp_from_fp(const ASTNode *type, int dst_offset, int src_offset, int size) {
   bool dst_direct = dst_offset >= 0 && dst_offset + size <= 256;
   bool src_direct = src_offset >= 0 && src_offset + size <= 256;

   if (!dst_direct) {
      emit_prepare_fp_ptr(0, dst_offset);
   }
   if (!src_direct) {
      emit_prepare_fp_ptr(1, src_offset);
   }

   emit(&es_code, "    sec\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + j) : j);
      emit(&es_code, "    lda %s,y\n", dst_direct ? "(fp)" : "(ptr0)");
      emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + j) : j);
      emit(&es_code, "    sbc %s,y\n", src_direct ? "(fp)" : "(ptr1)");
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + j) : j);
      emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr0)");
   }
}


static int declarator_pointer_depth(const ASTNode *declarator) {
   if (!declarator || declarator->count == 0 || !declarator->children[0] || !declarator->children[0]->strval) {
      return 0;
   }
   return atoi(declarator->children[0]->strval);
}

static int declarator_array_multiplier_from(const ASTNode *declarator, int start_child) {
   int mult = 1;
   if (!declarator || declarator_is_function(declarator)) {
      return 1;
   }
   for (int i = start_child; i < declarator->count; i++) {
      if (declarator->children[i] && declarator->children[i]->kind == AST_INTEGER) {
         mult *= atoi(declarator->children[i]->strval);
      }
   }
   return mult;
}

static int declarator_array_count(const ASTNode *declarator) {
   int count = 0;
   if (!declarator || declarator_is_function(declarator)) {
      return 0;
   }
   for (int i = 2; i < declarator->count; i++) {
      if (declarator->children[i] && declarator->children[i]->kind == AST_INTEGER) {
         count++;
      }
   }
   return count;
}

static int declarator_first_element_size(const ASTNode *type, const ASTNode *declarator) {
   if (declarator_pointer_depth(declarator) > 0) {
      return get_size(type_name_from_node(type));
   }
   return get_size(type_name_from_node(type)) * declarator_array_multiplier_from(declarator, 3);
}

static bool find_aggregate_member(const ASTNode *type, const char *member, const ASTNode **member_type, const ASTNode **member_declarator, int *member_offset) {
   const ASTNode *agg;
   int offset = 0;
   bool is_union = false;
   if (!type || !type_name_from_node(type)) {
      return false;
   }
   agg = get_typename_node(type_name_from_node(type));
   if (!agg || agg->count < 2) {
      return false;
   }
   is_union = !strcmp(agg->name, "union_decl_stmt");
   for (int i = 1; i < agg->count; i++) {
      const ASTNode *field = agg->children[i];
      const ASTNode *ftype;
      const ASTNode *fdecl;
      const char *fname;
      int fsize;
      if (!field || field->count < 3) {
         continue;
      }
      ftype = field->children[1];
      fdecl = field->children[2];
      if (!fdecl || fdecl->count < 2 || !fdecl->children[1] || !fdecl->children[1]->strval) {
         continue;
      }
      fname = fdecl->children[1]->strval;
      fsize = declarator_storage_size(ftype, fdecl);
      if (!strcmp(fname, member)) {
         if (member_type) *member_type = ftype;
         if (member_declarator) *member_declarator = fdecl;
         if (member_offset) *member_offset = is_union ? 0 : offset;
         return true;
      }
      if (!is_union) {
         offset += fsize;
      }
   }
   return false;
}

static void emit_load_ptr_from_fpvar(int ptrno, int src_offset) {
   bool direct = src_offset >= 0 && src_offset + 2 <= 256;
   if (!direct) {
      emit_prepare_fp_ptr(ptrno == 0 ? 1 : 0, src_offset);
   }
   for (int i = 0; i < 2; i++) {
      emit(&es_code, "    ldy #%d\n", direct ? (src_offset + i) : i);
      emit(&es_code, "    lda %s,y\n", direct ? "(fp)" : (ptrno == 0 ? "(ptr1)" : "(ptr0)"));
      emit(&es_code, "    sta ptr%d%s\n", ptrno, i == 0 ? "" : "+1");
   }
}

static void emit_add_immediate_to_ptr(int ptrno, int adjust) {
   if (adjust == 0) {
      return;
   }
   emit(&es_code, "    clc\n");
   emit(&es_code, "    lda ptr%d\n", ptrno);
   emit(&es_code, "    adc #$%02x\n", adjust & 0xff);
   emit(&es_code, "    sta ptr%d\n", ptrno);
   emit(&es_code, "    lda ptr%d+1\n", ptrno);
   emit(&es_code, "    adc #$%02x\n", (adjust >> 8) & 0xff);
   emit(&es_code, "    sta ptr%d+1\n", ptrno);
}

static void emit_store_ptr_to_fp(int dst_offset, int ptrno, int size) {
   bool direct = dst_offset >= 0 && dst_offset + size <= 256;

   if (size <= 0) {
      return;
   }

   if (!direct) {
      emit_prepare_fp_ptr(ptrno == 0 ? 1 : 0, dst_offset);
   }

   for (int i = 0; i < size; i++) {
      if (i < get_size("*")) {
         emit(&es_code, "    lda ptr%d%s\n", ptrno, i == 0 ? "" : "+1");
      }
      else {
         emit(&es_code, "    lda #0\n");
      }
      emit(&es_code, "    ldy #%d\n", direct ? (dst_offset + i) : i);
      emit(&es_code, "    sta %s,y\n", direct ? "(fp)" : (ptrno == 0 ? "(ptr1)" : "(ptr0)"));
   }
}

static void emit_load_lowbyte_fp_to_arg1(int src_offset) {
   bool direct = src_offset >= 0 && src_offset + 1 <= 256;
   if (!direct) {
      emit_prepare_fp_ptr(0, src_offset);
      emit(&es_code, "    ldy #0\n");
      emit(&es_code, "    lda (ptr0),y\n");
   }
   else {
      emit(&es_code, "    ldy #%d\n", src_offset);
      emit(&es_code, "    lda (fp),y\n");
   }
   emit(&es_code, "    sta arg1\n");
}

static void emit_runtime_binary_fp_fp(const char *helper, int dst_offset, int lhs_offset, int rhs_offset, int size) {
   emit_prepare_fp_ptr(0, lhs_offset);
   emit_prepare_fp_ptr(1, rhs_offset);
   emit_prepare_fp_ptr(2, dst_offset);
   emit(&es_code, "    lda #$%02x\n", size & 0xff);
   emit(&es_code, "    sta arg0\n");
   remember_runtime_import(helper);
   emit(&es_code, "    jsr _%s\n", helper);
}

static void emit_runtime_shift_fp(const char *helper, int value_offset, int scratch_offset, int count_offset, int size) {
   emit_prepare_fp_ptr(0, value_offset);
   emit_prepare_fp_ptr(1, scratch_offset);
   emit_load_lowbyte_fp_to_arg1(count_offset);
   emit(&es_code, "    lda #$%02x\n", size & 0xff);
   emit(&es_code, "    sta arg0\n");
   remember_runtime_import(helper);
   emit(&es_code, "    jsr _%s\n", helper);
}

static void emit_add_fp_to_symbol(const ASTNode *type, const char *dst_symbol, int src_offset, int size) {
   bool src_direct = src_offset >= 0 && src_offset + size <= 256;
   if (!src_direct) {
      emit_prepare_fp_ptr(0, src_offset);
   }
   emit(&es_code, "    clc\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", j);
      emit(&es_code, "    lda %s,y\n", dst_symbol);
      emit(&es_code, "    adc %s,y\n", src_direct ? "(fp)" : "(ptr0)");
      emit(&es_code, "    sta %s,y\n", dst_symbol);
   }
}

static void emit_sub_fp_from_symbol(const ASTNode *type, const char *dst_symbol, int src_offset, int size) {
   bool src_direct = src_offset >= 0 && src_offset + size <= 256;
   if (!src_direct) {
      emit_prepare_fp_ptr(0, src_offset);
   }
   emit(&es_code, "    sec\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", j);
      emit(&es_code, "    lda %s,y\n", dst_symbol);
      emit(&es_code, "    sbc %s,y\n", src_direct ? "(fp)" : "(ptr0)");
      emit(&es_code, "    sta %s,y\n", dst_symbol);
   }
}

static void emit_add_symbol_to_fp(const ASTNode *type, int dst_offset, const char *src_symbol, int size) {
   bool dst_direct = dst_offset >= 0 && dst_offset + size <= 256;
   if (!dst_direct) {
      emit_prepare_fp_ptr(0, dst_offset);
   }
   emit(&es_code, "    clc\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + j) : j);
      emit(&es_code, "    lda %s,y\n", dst_direct ? "(fp)" : "(ptr0)");
      emit(&es_code, "    ldy #%d\n", j);
      emit(&es_code, "    adc %s,y\n", src_symbol);
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + j) : j);
      emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr0)");
   }
}

static void emit_sub_symbol_from_fp(const ASTNode *type, int dst_offset, const char *src_symbol, int size) {
   bool dst_direct = dst_offset >= 0 && dst_offset + size <= 256;
   if (!dst_direct) {
      emit_prepare_fp_ptr(0, dst_offset);
   }
   emit(&es_code, "    sec\n");
   for (int i = 0; i < size; i++) {
      int j = expr_byte_index(type, size, i);
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + j) : j);
      emit(&es_code, "    lda %s,y\n", dst_direct ? "(fp)" : "(ptr0)");
      emit(&es_code, "    ldy #%d\n", j);
      emit(&es_code, "    sbc %s,y\n", src_symbol);
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + j) : j);
      emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr0)");
   }
}

static void emit_copy_lvalue_to_fp(int dst_offset, const LValueRef *src, int size) {
   int copy_size = size < src->size ? size : src->size;
   bool dst_direct = dst_offset >= 0 && dst_offset + copy_size <= 256;
   if (src->indirect) {
      emit_load_ptr_from_fpvar(0, src->offset);
      emit_add_immediate_to_ptr(0, src->ptr_adjust);
      if (!dst_direct) {
         emit_prepare_fp_ptr(1, dst_offset);
      }
      for (int i = 0; i < copy_size; i++) {
         emit(&es_code, "    ldy #%d\n", i);
         emit(&es_code, "    lda (ptr0),y\n");
         emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + i) : i);
         emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
      }
      return;
   }
   emit_copy_fp_to_fp(dst_offset, src->offset, copy_size);
}

static void emit_copy_fp_to_lvalue(const LValueRef *dst, int src_offset, int size) {
   int copy_size = size < dst->size ? size : dst->size;
   bool src_direct = src_offset >= 0 && src_offset + copy_size <= 256;
   if (dst->indirect) {
      emit_load_ptr_from_fpvar(0, dst->offset);
      emit_add_immediate_to_ptr(0, dst->ptr_adjust);
      if (!src_direct) {
         emit_prepare_fp_ptr(1, src_offset);
      }
      for (int i = 0; i < copy_size; i++) {
         emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + i) : i);
         emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr1)");
         emit(&es_code, "    ldy #%d\n", i);
         emit(&es_code, "    sta (ptr0),y\n");
      }
      return;
   }
   emit_copy_fp_to_fp(dst->offset, src_offset, copy_size);
}

static bool resolve_lvalue_suffixes(Context *ctx, const ASTNode *suffixes, LValueRef *out) {
   (void) ctx;
   if (!suffixes || is_empty(suffixes)) {
      return true;
   }
   if (suffixes->count > 0 && !resolve_lvalue_suffixes(ctx, suffixes->children[0], out)) {
      return false;
   }
   if (!strcmp(suffixes->name, "[")) {
      const ASTNode *idx = unwrap_expr_node(suffixes->children[1]);
      int elem_size = declarator_first_element_size(out->type, out->declarator);
      if (!idx || idx->kind != AST_INTEGER) {
         return false;
      }
      if (declarator_pointer_depth(out->declarator) > 0) {
         out->indirect = true;
         out->size = elem_size;
         out->ptr_adjust += atoi(idx->strval) * elem_size;
         out->declarator = NULL;
         return true;
      }
      if (declarator_array_count(out->declarator) <= 0) {
         return false;
      }
      out->offset += atoi(idx->strval) * elem_size;
      out->size = elem_size;
      out->declarator = NULL;
      return true;
   }
   if (!strcmp(suffixes->name, ".") || !strcmp(suffixes->name, "->")) {
      const ASTNode *member_type = NULL;
      const ASTNode *member_decl = NULL;
      int member_offset = 0;
      if (!find_aggregate_member(out->type, suffixes->children[1]->strval, &member_type, &member_decl, &member_offset)) {
         return false;
      }
      if (!strcmp(suffixes->name, "->")) {
         if (declarator_pointer_depth(out->declarator) <= 0) {
            return false;
         }
         out->indirect = true;
         out->ptr_adjust += member_offset;
      }
      else if (out->indirect) {
         out->ptr_adjust += member_offset;
      }
      else {
         out->offset += member_offset;
      }
      out->type = member_type;
      out->declarator = member_decl;
      out->size = declarator_storage_size(member_type, member_decl);
      return true;
   }
   return true;
}

static bool resolve_lvalue(Context *ctx, ASTNode *node, LValueRef *out) {
   ASTNode *base;
   ContextEntry *entry;

   if (!node || strcmp(node->name, "lvalue") || node->count == 0 || !out) {
      return false;
   }

   memset(out, 0, sizeof(*out));
   base = node->children[0];
   if (!base) {
      return false;
   }

   if (!strcmp(base->name, "lvalue_base")) {
      if (base->count == 0 || base->children[0]->kind != AST_IDENTIFIER) {
         return false;
      }
      entry = ctx_lookup(ctx, base->children[0]->strval);
      if (!entry) {
         const ASTNode *g = global_decl_lookup(base->children[0]->strval);
         if (g && g->count >= 3) {
            static ContextEntry gtmp;
            gtmp.name = base->children[0]->strval;
            gtmp.type = g->children[1];
            gtmp.declarator = g->children[2];
            gtmp.is_static = false;
            gtmp.is_quick = has_modifier((ASTNode *) g->children[0], "quick");
            gtmp.is_global = true;
            gtmp.offset = 0;
            gtmp.size = declarator_storage_size(gtmp.type, gtmp.declarator);
            entry = &gtmp;
         }
      }
      if (!entry) {
         return false;
      }
      out->name = entry->name ? entry->name : base->children[0]->strval;
      out->type = entry->type;
      out->declarator = entry->declarator;
      out->is_static = entry->is_static;
      out->is_quick = entry->is_quick;
      out->is_global = entry->is_global;
      out->offset = entry->offset;
      out->size = entry->size;
   }
   else if (!strcmp(base->name, "*") && base->count > 0) {
      ASTNode *inner = base->children[0];
      if (inner && !strcmp(inner->name, "lvalue_base") && inner->count > 0 && inner->children[0]->kind == AST_IDENTIFIER) {
         entry = ctx_lookup(ctx, inner->children[0]->strval);
      }
      else {
         entry = NULL;
      }
      if (!entry || declarator_pointer_depth(entry->declarator) <= 0) {
         return false;
      }
      out->name = entry->name ? entry->name : inner->children[0]->strval;
      out->type = entry->type;
      out->declarator = NULL;
      out->is_static = entry->is_static;
      out->is_quick = entry->is_quick;
      out->is_global = entry->is_global;
      out->indirect = true;
      out->offset = entry->offset;
      out->size = get_size(type_name_from_node(entry->type));
   }
   else {
      return false;
   }

   return resolve_lvalue_suffixes(ctx, node->children[1], out);
}

static ContextEntry *ctx_lookup_lvalue(Context *ctx, ASTNode *node) {
   static ContextEntry tmp;
   LValueRef lv;

   if (!resolve_lvalue(ctx, node, &lv) || lv.indirect) {
      return NULL;
   }

   tmp.name = lv.name;
   tmp.type = lv.type;
   tmp.declarator = lv.declarator;
   tmp.is_static = lv.is_static;
   tmp.is_quick = lv.is_quick;
   tmp.is_global = lv.is_global;
   tmp.offset = lv.offset;
   tmp.size = lv.size;
   return &tmp;
}


static const ASTNode *function_return_type(const ASTNode *fn) {
   if (!fn) {
      return NULL;
   }
   if (fn->count == 3) {
      return fn->children[0]->children[1];
   }
   if (fn->count == 4) {
      return fn->children[1];
   }
   return NULL;
}

static const ASTNode *function_declarator_node(const ASTNode *fn) {
   if (!fn) {
      return NULL;
   }
   if (fn->count == 3) {
      return fn->children[1];
   }
   if (fn->count == 4) {
      return fn->children[2];
   }
   return NULL;
}

static bool compile_call_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst) {
   if (!expr || strcmp(expr->name, "()") || expr->count < 1) {
      return false;
   }

   ASTNode *callee = expr->children[0];
   ASTNode *args = (expr->count > 1) ? expr->children[1] : NULL;
   const ASTNode *fn = NULL;
   const ASTNode *ret_type = dst ? dst->type : NULL;
   const ASTNode *declarator = NULL;
   int ret_size = dst ? dst->size : 0;
   int arg_total = 0;
   int arg_count = (args && !is_empty(args)) ? args->count : 0;

   if (!callee || callee->kind != AST_IDENTIFIER) {
      return false;
   }

   if (functions) {
      fn = (const ASTNode *) set_get(functions, callee->strval);
   }
   if (fn) {
      const ASTNode *known_ret = function_return_type(fn);
      const ASTNode *params = NULL;
      declarator = function_declarator_node(fn);
      if (known_ret) {
         ret_type = known_ret;
         ret_size = get_size(type_name_from_node(ret_type));
      }
      if (declarator && declarator->count > 2) {
         params = declarator->children[2];
         if (params && !is_empty(params)) {
            int fixed_params = 0;
            for (int i = 0; i < params->count; i++) {
               const ASTNode *parameter = params->children[i];
               const ASTNode *ptype = parameter_type(parameter);
               const ASTNode *pdecl = parameter_declarator(parameter);
               if (!ptype || parameter_is_void(parameter)) {
                  continue;
               }
               fixed_params++;
               arg_total += declarator_storage_size(ptype, pdecl);
            }
            if (fixed_params != arg_count) {
               warning("[%s:%d.%d] call to '%s' argument count mismatch (%d vs %d)",
                       expr->file, expr->line, expr->column,
                       callee->strval, arg_count, fixed_params);
            }
         }
      }
   }
   else if (!dst) {
      ret_size = 0;
   }

   if (ret_size < 0) ret_size = 0;
   int call_size = ret_size + arg_total;

   if (call_size > 0) {
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
   }

   if (fn && declarator && declarator->count > 2) {
      const ASTNode *params = declarator->children[2];
      int arg_offset = ret_size;
      int actual_index = 0;
      if (params && !is_empty(params)) {
         for (int i = 0; i < params->count && actual_index < arg_count; i++) {
            const ASTNode *parameter = params->children[i];
            const ASTNode *ptype = parameter_type(parameter);
            const ASTNode *pdecl = parameter_declarator(parameter);
            ContextEntry tmp;
            int psz;
            if (!ptype || parameter_is_void(parameter)) {
               continue;
            }
            psz = declarator_storage_size(ptype, pdecl);
            tmp.type = ptype;
            tmp.is_static = false;
            tmp.is_quick = false;
            tmp.offset = ctx->locals + arg_offset;
            tmp.size = psz;
            if (!compile_expr_to_slot(args->children[actual_index], ctx, &tmp)) {
               if (call_size > 0) {
                  remember_runtime_import("popN");
                  emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
                  emit(&es_code, "    sta arg0\n");
                  emit(&es_code, "    jsr _popN\n");
               }
               return false;
            }
            arg_offset += psz;
            actual_index++;
         }
      }
   }
   else if (arg_count > 0) {
      if (call_size > 0) {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
      }
      warning("[%s:%d.%d] call to '%s' has no visible signature yet", expr->file, expr->line, expr->column, callee->strval);
      return false;
   }

   emit(&es_code, "    jsr _%s\n", callee->strval);

   if (dst && ret_size > 0) {
      emit_copy_fp_to_fp(dst->offset, ctx->locals, dst->size < ret_size ? dst->size : ret_size);
   }

   if (call_size > 0) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
   }

   return true;
}

static bool compile_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst) {
   expr = (ASTNode *) unwrap_expr_node(expr);

   if (!expr || is_empty(expr)) {
      return true;
   }

   if (!strcmp(expr->name, "()")) {
      return compile_call_expr_to_slot(expr, ctx, dst);
   }

   if (expr->kind == AST_INTEGER) {
      unsigned char *bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
      if (has_flag(type_name_from_node(dst->type), "$endian:big")) {
         make_be_int(expr->strval, bytes, dst->size);
      }
      else {
         make_le_int(expr->strval, bytes, dst->size);
      }
      emit_store_immediate_to_fp(dst->offset, bytes, dst->size);
      free(bytes);
      return true;
   }

   if (expr->kind == AST_IDENTIFIER) {
      ContextEntry *entry = ctx_lookup(ctx, expr->strval);
      if (entry && !entry->is_static && !entry->is_quick) {
         emit_copy_fp_to_fp(dst->offset, entry->offset, entry->size < dst->size ? entry->size : dst->size);
         return true;
      }
      if (entry) {
         char sym[256];
         if (entry_symbol_name(ctx, entry, sym, sizeof(sym))) {
            emit_copy_symbol_to_fp(dst->offset, sym, entry->size < dst->size ? entry->size : dst->size);
            return true;
         }
      }
      {
         const ASTNode *g = global_decl_lookup(expr->strval);
         if (g && g->count >= 3) {
            char sym[256];
            int gsize = declarator_storage_size(g->children[1], g->children[2]);
            snprintf(sym, sizeof(sym), "_%s", expr->strval);
            emit_copy_symbol_to_fp(dst->offset, sym, gsize < dst->size ? gsize : dst->size);
            return true;
         }
      }
   }

   if (expr->count == 1 && !strcmp(expr->name, "&")) {
      LValueRef lv;
      ASTNode *inner = (ASTNode *) unwrap_expr_node(expr->children[0]);
      if (inner && !strcmp(inner->name, "lvalue") && resolve_lvalue(ctx, inner, &lv)) {
         if (lv.indirect) {
            if (lv.is_static || lv.is_quick || lv.is_global) {
               char sym[256];
               if (!entry_symbol_name(ctx, &(ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_quick = lv.is_quick, .is_global = lv.is_global, .offset = lv.offset, .size = lv.size }, sym, sizeof(sym))) {
                  return false;
               }
               emit(&es_code, "    ldy #0\n");
               emit(&es_code, "    lda %s,y\n", sym);
               emit(&es_code, "    sta ptr0\n");
               emit(&es_code, "    iny\n");
               emit(&es_code, "    lda %s,y\n", sym);
               emit(&es_code, "    sta ptr1\n");
               emit_add_immediate_to_ptr(0, lv.ptr_adjust);
            }
            else {
               emit_load_ptr_from_fpvar(0, lv.offset);
               emit_add_immediate_to_ptr(0, lv.ptr_adjust);
            }
         }
         else if (lv.is_static || lv.is_quick || lv.is_global) {
            char sym[256];
            if (!entry_symbol_name(ctx, &(ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_quick = lv.is_quick, .is_global = lv.is_global, .offset = lv.offset, .size = lv.size }, sym, sizeof(sym))) {
               return false;
            }
            emit(&es_code, "    lda #<(%s + %d)\n", sym, lv.offset + lv.ptr_adjust);
            emit(&es_code, "    sta ptr0\n");
            emit(&es_code, "    lda #>(%s + %d)\n", sym, lv.offset + lv.ptr_adjust);
            emit(&es_code, "    sta ptr1\n");
         }
         else {
            emit_prepare_fp_ptr(0, lv.offset);
         }
         emit_store_ptr_to_fp(dst->offset, 0, dst->size);
         return true;
      }
   }

   if (!strcmp(expr->name, "lvalue") && expr->count > 0 && expr->count >= 3 && expr->children[2] &&
       expr->children[2]->kind == AST_IDENTIFIER &&
       (!strcmp(expr->children[2]->strval, "pre++") || !strcmp(expr->children[2]->strval, "post++") ||
        !strcmp(expr->children[2]->strval, "pre--") || !strcmp(expr->children[2]->strval, "post--"))) {
      LValueRef lv;
      bool inc;
      bool pre;
      int tmp_size;
      ContextEntry tmp;
      unsigned char *one;
      if (!resolve_lvalue(ctx, expr, &lv)) {
         return false;
      }
      inc = !strcmp(expr->children[2]->strval, "pre++") || !strcmp(expr->children[2]->strval, "post++");
      pre = !strcmp(expr->children[2]->strval, "pre++") || !strcmp(expr->children[2]->strval, "pre--");
      tmp_size = lv.size > 0 ? lv.size : dst->size;
      tmp = (ContextEntry){ .name = "$tmp", .type = lv.type, .declarator = lv.declarator, .is_static = false, .is_quick = false, .is_global = false, .offset = ctx->locals, .size = tmp_size };
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      emit_copy_lvalue_to_fp(tmp.offset, &lv, tmp.size);
      if (!pre) {
         emit_copy_fp_to_fp(dst->offset, tmp.offset, tmp.size < dst->size ? tmp.size : dst->size);
      }
      one = (unsigned char *) calloc(tmp.size ? tmp.size : 1, sizeof(unsigned char));
      if (!one) {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return false;
      }
      one[0] = 1;
      if (inc) {
         emit_add_immediate_to_fp(tmp.type, tmp.offset, one, tmp.size);
      }
      else {
         emit_sub_immediate_from_fp(tmp.type, tmp.offset, one, tmp.size);
      }
      free(one);
      emit_copy_fp_to_lvalue(&lv, tmp.offset, tmp.size);
      if (pre) {
         emit_copy_fp_to_fp(dst->offset, tmp.offset, tmp.size < dst->size ? tmp.size : dst->size);
      }
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      return true;
   }

   if (!strcmp(expr->name, "lvalue") && expr->count > 0) {
      LValueRef lv;
      if (resolve_lvalue(ctx, expr, &lv)) {
         if (!lv.is_static && !lv.is_quick && !lv.is_global) {
            emit_copy_lvalue_to_fp(dst->offset, &lv, dst->size);
            return true;
         }
         if (lv.indirect) {
            char sym[256];
            if (!entry_symbol_name(ctx, &(ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_quick = lv.is_quick, .is_global = lv.is_global, .offset = lv.offset, .size = lv.size }, sym, sizeof(sym))) {
               return false;
            }
            emit(&es_code, "    ldy #0\n");
            emit(&es_code, "    lda %s,y\n", sym);
            emit(&es_code, "    sta ptr0\n");
            emit(&es_code, "    iny\n");
            emit(&es_code, "    lda %s,y\n", sym);
            emit(&es_code, "    sta ptr1\n");
            emit_add_immediate_to_ptr(0, lv.ptr_adjust);
            for (int i = 0; i < (lv.size < dst->size ? lv.size : dst->size); i++) {
               emit(&es_code, "    ldy #%d\n", i);
               emit(&es_code, "    lda (ptr0),y\n");
               emit(&es_code, "    ldy #%d\n", dst->offset + i);
               emit(&es_code, "    sta (fp),y\n");
            }
            return true;
         }
         else {
            char sym[256];
            if (!entry_symbol_name(ctx, &(ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_quick = lv.is_quick, .is_global = lv.is_global, .offset = lv.offset, .size = lv.size }, sym, sizeof(sym))) {
               return false;
            }
            emit_copy_symbol_to_fp(dst->offset, sym, lv.size < dst->size ? lv.size : dst->size);
            return true;
         }
      }
   }

   if (!strcmp(expr->name, "comma_expr") && expr->count > 0) {
      for (int i = 0; i < expr->count - 1; i++) {
         compile_expr(expr->children[i], ctx);
      }
      return compile_expr_to_slot(expr->children[expr->count - 1], ctx, dst);
   }

   if (!strcmp(expr->name, "conditional_expr") && expr->count == 4 && expr->children[0] && expr->children[0]->kind == AST_IDENTIFIER && !strcmp(expr->children[0]->strval, "?:")) {
      const char *false_label = next_label("ternary_false");
      const char *end_label = next_label("ternary_end");
      bool ok;
      if (!false_label || !end_label) {
         free((void *) false_label);
         free((void *) end_label);
         return false;
      }
      if (!compile_condition_branch_false(expr->children[1], ctx, false_label)) {
         free((void *) false_label);
         free((void *) end_label);
         return false;
      }
      ok = compile_expr_to_slot(expr->children[2], ctx, dst);
      emit(&es_code, "    jmp %s\n", end_label);
      emit(&es_code, "%s:\n", false_label);
      if (ok) {
         ok = compile_expr_to_slot(expr->children[3], ctx, dst);
      }
      emit(&es_code, "%s:\n", end_label);
      free((void *) false_label);
      free((void *) end_label);
      return ok;
   }

   if (expr->count == 1 && !strcmp(expr->name, "+")) {
      return compile_expr_to_slot(expr->children[0], ctx, dst);
   }

   if (expr->count == 1 && !strcmp(expr->name, "!")) {
      const char *false_label = next_label("not_false");
      const char *end_label = next_label("not_end");
      unsigned char *zeroes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
      unsigned char *ones = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
      bool ok = false;
      if (!false_label || !end_label || !zeroes || !ones) {
         free(zeroes);
         free(ones);
         free((void *) false_label);
         free((void *) end_label);
         return false;
      }
      ones[0] = 1;
      if (!compile_condition_branch_false(expr->children[0], ctx, false_label)) {
         goto unary_not_done;
      }
      emit_store_immediate_to_fp(dst->offset, zeroes, dst->size);
      emit(&es_code, "    jmp %s\n", end_label);
      emit(&es_code, "%s:\n", false_label);
      emit_store_immediate_to_fp(dst->offset, ones, dst->size);
      emit(&es_code, "%s:\n", end_label);
      ok = true;
unary_not_done:
      free(zeroes);
      free(ones);
      free((void *) false_label);
      free((void *) end_label);
      return ok;
   }

   if (expr->count == 1 && !strcmp(expr->name, "~")) {
      if (!compile_expr_to_slot(expr->children[0], ctx, dst)) {
         return false;
      }
      for (int i = 0; i < dst->size; i++) {
         emit(&es_code, "    ldy #%d\n", dst->offset + i);
         emit(&es_code, "    lda (fp),y\n");
         emit(&es_code, "    eor #$ff\n");
         emit(&es_code, "    sta (fp),y\n");
      }
      return true;
   }

   if (expr->count == 1 && !strcmp(expr->name, "-")) {
      if (!compile_expr_to_slot(expr->children[0], ctx, dst)) {
         return false;
      }
      for (int i = 0; i < dst->size; i++) {
         emit(&es_code, "    ldy #%d\n", dst->offset + i);
         emit(&es_code, "    lda (fp),y\n");
         emit(&es_code, "    eor #$ff\n");
         emit(&es_code, "    sta (fp),y\n");
      }
      emit(&es_code, "    clc\n");
      for (int i = 0; i < dst->size; i++) {
         emit(&es_code, "    ldy #%d\n", dst->offset + i);
         emit(&es_code, "    lda (fp),y\n");
         emit(&es_code, "    adc #%d\n", i == 0 ? 1 : 0);
         emit(&es_code, "    sta (fp),y\n");
      }
      return true;
   }

   if (expr->count == 2 && (!strcmp(expr->name, "&&") || !strcmp(expr->name, "||"))) {
      const char *false_label = next_label(!strcmp(expr->name, "&&") ? "and_false" : "or_false");
      const char *end_label = next_label(!strcmp(expr->name, "&&") ? "and_end" : "or_end");
      unsigned char *zeroes;
      unsigned char *ones;

      if (!false_label || !end_label) {
         free((void *) false_label);
         free((void *) end_label);
         return false;
      }

      zeroes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
      ones = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
      if (!zeroes || !ones) {
         free(zeroes);
         free(ones);
         free((void *) false_label);
         free((void *) end_label);
         return false;
      }
      ones[0] = 1;

      if (!strcmp(expr->name, "&&")) {
         if (!compile_condition_branch_false(expr->children[0], ctx, false_label) ||
             !compile_condition_branch_false(expr->children[1], ctx, false_label)) {
            free(zeroes);
            free(ones);
            free((void *) false_label);
            free((void *) end_label);
            return false;
         }
      }
      else {
         const char *rhs_label = next_label("or_rhs");
         if (!rhs_label) {
            free(zeroes);
            free(ones);
            free((void *) false_label);
            free((void *) end_label);
            return false;
         }
         if (!compile_condition_branch_false(expr->children[0], ctx, rhs_label)) {
            free(zeroes);
            free(ones);
            free((void *) rhs_label);
            free((void *) false_label);
            free((void *) end_label);
            return false;
         }
         emit_store_immediate_to_fp(dst->offset, ones, dst->size);
         emit(&es_code, "    jmp %s\n", end_label);
         emit(&es_code, "%s:\n", rhs_label);
         if (!compile_condition_branch_false(expr->children[1], ctx, false_label)) {
            free(zeroes);
            free(ones);
            free((void *) rhs_label);
            free((void *) false_label);
            free((void *) end_label);
            return false;
         }
         free((void *) rhs_label);
         emit_store_immediate_to_fp(dst->offset, ones, dst->size);
         emit(&es_code, "    jmp %s\n", end_label);
         emit(&es_code, "%s:\n", false_label);
         emit_store_immediate_to_fp(dst->offset, zeroes, dst->size);
         emit(&es_code, "%s:\n", end_label);
         free(zeroes);
         free(ones);
         free((void *) false_label);
         free((void *) end_label);
         return true;
      }

      emit_store_immediate_to_fp(dst->offset, ones, dst->size);
      emit(&es_code, "    jmp %s\n", end_label);
      emit(&es_code, "%s:\n", false_label);
      emit_store_immediate_to_fp(dst->offset, zeroes, dst->size);
      emit(&es_code, "%s:\n", end_label);
      free(zeroes);
      free(ones);
      free((void *) false_label);
      free((void *) end_label);
      return true;
   }

   if (expr->count == 2 && (!strcmp(expr->name, "+") || !strcmp(expr->name, "-"))) {
      const ASTNode *rhs = unwrap_expr_node(expr->children[1]);
      const ASTNode *lhs_type = expr_value_type(expr->children[0], ctx);
      const ASTNode *lhs_decl = expr_value_declarator(expr->children[0], ctx);
      int pointer_scale = 1;
      bool scaled_pointer_arith = lhs_decl && declarator_pointer_depth(lhs_decl) > 0;
      if (scaled_pointer_arith) {
         pointer_scale = declarator_first_element_size(lhs_type, lhs_decl);
         if (pointer_scale <= 0) {
            pointer_scale = 1;
         }
      }
      if (!compile_expr_to_slot(expr->children[0], ctx, dst)) {
         return false;
      }

      if (rhs && rhs->kind == AST_INTEGER) {
         unsigned char *bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
         const char *ival = rhs->strval;
         char scaled_buf[64];
         if (scaled_pointer_arith && pointer_scale != 1) {
            long long value = strtoll(rhs->strval, NULL, 0);
            snprintf(scaled_buf, sizeof(scaled_buf), "%lld", value * (long long) pointer_scale);
            ival = scaled_buf;
         }
         if (has_flag(type_name_from_node(dst->type), "$endian:big")) {
            make_be_int(ival, bytes, dst->size);
         }
         else {
            make_le_int(ival, bytes, dst->size);
         }

         if (!strcmp(expr->name, "+")) {
            emit_add_immediate_to_fp(dst->type, dst->offset, bytes, dst->size);
         }
         else {
            emit_sub_immediate_from_fp(dst->type, dst->offset, bytes, dst->size);
         }
         free(bytes);
         return true;
      }

      if (!scaled_pointer_arith && rhs && rhs->kind == AST_IDENTIFIER) {
         ContextEntry *entry = ctx_lookup(ctx, rhs->strval);
         if (entry && !entry->is_static && !entry->is_quick) {
            if (!strcmp(expr->name, "+")) {
               emit_add_fp_to_fp(dst->type, dst->offset, entry->offset, entry->size < dst->size ? entry->size : dst->size);
            }
            else {
               emit_sub_fp_from_fp(dst->type, dst->offset, entry->offset, entry->size < dst->size ? entry->size : dst->size);
            }
            return true;
         }
      }

      if (!scaled_pointer_arith && rhs && !strcmp(rhs->name, "lvalue") && rhs->count > 0) {
         ContextEntry *entry = ctx_lookup_lvalue(ctx, (ASTNode *) rhs);
         if (entry && !entry->is_static && !entry->is_quick) {
            if (!strcmp(expr->name, "+")) {
               emit_add_fp_to_fp(dst->type, dst->offset, entry->offset, entry->size < dst->size ? entry->size : dst->size);
            }
            else {
               emit_sub_fp_from_fp(dst->type, dst->offset, entry->offset, entry->size < dst->size ? entry->size : dst->size);
            }
            return true;
         }
      }

      {
         int rhs_size = scaled_pointer_arith ? dst->size : expr_value_size((ASTNode *) rhs, ctx);
         int tmp_total;
         int rhs_offset = ctx->locals;
         int factor_offset = 0;
         int scaled_offset = 0;
         int value_offset = rhs_offset;
         ContextEntry tmp;
         if (rhs_size <= 0) {
            rhs_size = dst->size;
         }
         tmp_total = rhs_size;
         if (scaled_pointer_arith && pointer_scale != 1) {
            tmp_total += rhs_size * 2;
            factor_offset = ctx->locals + rhs_size;
            scaled_offset = ctx->locals + rhs_size * 2;
         }
         tmp = (ContextEntry){ .name = "$tmp", .type = expr_value_type((ASTNode *) rhs, ctx), .declarator = NULL, .is_static = false, .is_quick = false, .is_global = false, .offset = rhs_offset, .size = rhs_size };
         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         if (!compile_expr_to_slot((ASTNode *) rhs, ctx, &tmp)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         if (scaled_pointer_arith && pointer_scale != 1) {
            unsigned char *factor_bytes = (unsigned char *) calloc(rhs_size ? rhs_size : 1, sizeof(unsigned char));
            char scaled_buf[64];
            snprintf(scaled_buf, sizeof(scaled_buf), "%d", pointer_scale);
            if (has_flag(type_name_from_node(tmp.type), "$endian:big")) {
               make_be_int(scaled_buf, factor_bytes, rhs_size);
            }
            else {
               make_le_int(scaled_buf, factor_bytes, rhs_size);
            }
            emit_store_immediate_to_fp(factor_offset, factor_bytes, rhs_size);
            free(factor_bytes);
            emit_runtime_binary_fp_fp("mulN", scaled_offset, rhs_offset, factor_offset, rhs_size);
            value_offset = scaled_offset;
         }
         if (!strcmp(expr->name, "+")) {
            emit_add_fp_to_fp(dst->type, dst->offset, value_offset, rhs_size < dst->size ? rhs_size : dst->size);
         }
         else {
            emit_sub_fp_from_fp(dst->type, dst->offset, value_offset, rhs_size < dst->size ? rhs_size : dst->size);
         }
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return true;
      }
   }


   if (expr->count == 2 && (!strcmp(expr->name, "&") || !strcmp(expr->name, "|") || !strcmp(expr->name, "^") ||
                            !strcmp(expr->name, "*") || !strcmp(expr->name, "/") || !strcmp(expr->name, "%") ||
                            !strcmp(expr->name, "<<") || !strcmp(expr->name, ">>"))) {
      const char *op = expr->name;
      int op_size = dst->size > 0 ? dst->size : expr_value_size(expr, ctx);
      int tmp_total;
      int rhs_offset;
      int aux_offset;
      unsigned char *zeroes;
      const ASTNode *lhs_type;
      const char *helper = NULL;

      if (op_size <= 0) {
         op_size = get_size("int");
      }
      if (!compile_expr_to_slot(expr->children[0], ctx, dst)) {
         return false;
      }

      tmp_total = op_size;
      if (!strcmp(op, "*")) {
         tmp_total += op_size * 2;
      }
      else if (!strcmp(op, "/") || !strcmp(op, "%")) {
         tmp_total += op_size * 2;
      }
      else if (!strcmp(op, "<<") || !strcmp(op, ">>")) {
         tmp_total += op_size;
      }

      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");

      rhs_offset = ctx->locals;
      aux_offset = ctx->locals + op_size;
      zeroes = (unsigned char *) calloc(op_size ? op_size : 1, sizeof(unsigned char));
      if (!zeroes) {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return false;
      }
      emit_store_immediate_to_fp(rhs_offset, zeroes, op_size);
      free(zeroes);
      if (!compile_expr_to_slot(expr->children[1], ctx, &(ContextEntry){ .name = "$tmp_rhs", .type = expr_value_type(expr->children[1], ctx), .declarator = NULL, .is_static = false, .is_quick = false, .is_global = false, .offset = rhs_offset, .size = op_size })) {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return false;
      }

      if (!strcmp(op, "&")) helper = "bit_andN";
      else if (!strcmp(op, "|")) helper = "bit_orN";
      else if (!strcmp(op, "^")) helper = "bit_xorN";

      if (helper) {
         emit_runtime_binary_fp_fp(helper, dst->offset, dst->offset, rhs_offset, op_size);
      }
      else if (!strcmp(op, "*")) {
         emit_runtime_binary_fp_fp("mulN", aux_offset, dst->offset, rhs_offset, op_size);
         emit_copy_fp_to_fp(dst->offset, aux_offset, op_size);
      }
      else if (!strcmp(op, "/") || !strcmp(op, "%")) {
         int rem_offset = aux_offset + op_size;
         emit_prepare_fp_ptr(0, dst->offset);
         emit_prepare_fp_ptr(1, rhs_offset);
         emit_prepare_fp_ptr(2, aux_offset);
         emit_prepare_fp_ptr(3, rem_offset);
         emit(&es_code, "    lda #$%02x\n", op_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         remember_runtime_import("divN");
         emit(&es_code, "    jsr _divN\n");
         emit_copy_fp_to_fp(dst->offset, !strcmp(op, "/") ? aux_offset : rem_offset, op_size);
      }
      else if (!strcmp(op, "<<") || !strcmp(op, ">>")) {
         lhs_type = expr_value_type(expr->children[0], ctx);
         helper = !strcmp(op, "<<") ? "lslN" : (lhs_type && has_flag(type_name_from_node(lhs_type), "$signed") ? "asrN" : "lsrN");
         emit_runtime_shift_fp(helper, dst->offset, aux_offset, rhs_offset, op_size);
      }

      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      return true;
   }

   return false;
}


static const ASTNode *expr_value_type(ASTNode *expr, Context *ctx) {
   expr = (ASTNode *) unwrap_expr_node(expr);

   if (!expr || is_empty(expr)) {
      return get_typename_node("int");
   }

   if (expr->kind == AST_INTEGER) {
      return get_typename_node("int");
   }

   if (expr->kind == AST_FLOAT) {
      return get_typename_node("float");
   }

   if (expr->kind == AST_STRING) {
      return get_typename_node("*");
   }

   if (expr->kind == AST_IDENTIFIER) {
      ContextEntry *entry = ctx_lookup(ctx, expr->strval);
      if (entry) {
         return entry->type;
      }
      {
         const ASTNode *g = global_decl_lookup(expr->strval);
         if (g && g->count >= 3) {
            return g->children[1];
         }
      }
   }

   if (expr->count == 1 && !strcmp(expr->name, "&")) {
      return get_typename_node("*");
   }

   if (!strcmp(expr->name, "lvalue") && expr->count > 0) {
      LValueRef lv;
      if (resolve_lvalue(ctx, expr, &lv)) {
         return lv.type;
      }
   }

   if (!strcmp(expr->name, "()")) {
      ASTNode *callee = expr->children[0];
      const ASTNode *fn = NULL;
      if (callee && callee->kind == AST_IDENTIFIER && functions) {
         fn = (const ASTNode *) set_get(functions, callee->strval);
      }
      if (fn) {
         const ASTNode *ret = function_return_type(fn);
         if (ret) {
            return ret;
         }
      }
   }

   if (!strcmp(expr->name, "comma_expr") && expr->count > 0) {
      return expr_value_type(expr->children[expr->count - 1], ctx);
   }

   if (!strcmp(expr->name, "conditional_expr") && expr->count == 4 && expr->children[0] && expr->children[0]->kind == AST_IDENTIFIER && !strcmp(expr->children[0]->strval, "?:")) {
      return expr_value_type(expr->children[2], ctx);
   }

   if (expr->count == 1 && (!strcmp(expr->name, "!") || !strcmp(expr->name, "==") || !strcmp(expr->name, "!=") ||
       !strcmp(expr->name, "<") || !strcmp(expr->name, ">") || !strcmp(expr->name, "<=") || !strcmp(expr->name, ">=") ||
       !strcmp(expr->name, "&&") || !strcmp(expr->name, "||"))) {
      return get_typename_node("int");
   }

   if (expr->count >= 1) {
      return expr_value_type(expr->children[0], ctx);
   }

   return get_typename_node("int");
}

static const ASTNode *expr_value_declarator(ASTNode *expr, Context *ctx) {
   expr = (ASTNode *) unwrap_expr_node(expr);

   if (!expr || is_empty(expr)) {
      return NULL;
   }

   if (expr->kind == AST_IDENTIFIER) {
      ContextEntry *entry = ctx_lookup(ctx, expr->strval);
      if (entry) {
         return entry->declarator;
      }
      {
         const ASTNode *g = global_decl_lookup(expr->strval);
         if (g && g->count >= 3) {
            return g->children[2];
         }
      }
   }

   if (!strcmp(expr->name, "lvalue") && expr->count > 0) {
      LValueRef lv;
      if (resolve_lvalue(ctx, expr, &lv)) {
         return lv.declarator;
      }
   }

   if (!strcmp(expr->name, "comma_expr") && expr->count > 0) {
      return expr_value_declarator(expr->children[expr->count - 1], ctx);
   }

   if (!strcmp(expr->name, "conditional_expr") && expr->count == 4 && expr->children[0] && expr->children[0]->kind == AST_IDENTIFIER && !strcmp(expr->children[0]->strval, "?:")) {
      return expr_value_declarator(expr->children[2], ctx);
   }

   return NULL;
}

static int type_size_from_node(const ASTNode *type) {
   if (!type) {
      return get_size("int");
   }
   if (type->strval) {
      return get_size(type_name_from_node(type));
   }
   if (type->count > 0 && type->children[0] && type->children[0]->strval) {
      return get_size(type->children[0]->strval);
   }
   return get_size("int");
}

static int expr_value_size(ASTNode *expr, Context *ctx) {
   const ASTNode *type = expr_value_type(expr, ctx);
   return type_size_from_node(type);
}

static const char *next_label(const char *prefix) {
   char buf[64];
   snprintf(buf, sizeof(buf), "@%s_%d", prefix, label_counter++);
   return strdup(buf);
}

static bool compile_condition_branch_false(ASTNode *expr, Context *ctx, const char *false_label) {
   expr = (ASTNode *) unwrap_expr_node(expr);

   if (!expr || is_empty(expr)) {
      emit(&es_code, "    jmp %s\n", false_label);
      return true;
   }

   if (expr->count == 1 && !strcmp(expr->name, "!")) {
      const char *end_label = next_label("not_cond_end");
      if (!end_label) {
         return false;
      }
      if (!compile_condition_branch_false(expr->children[0], ctx, end_label)) {
         free((void *) end_label);
         return false;
      }
      emit(&es_code, "    jmp %s\n", false_label);
      emit(&es_code, "%s:\n", end_label);
      free((void *) end_label);
      return true;
   }

   if (expr->count == 2 && !strcmp(expr->name, "&&")) {
      if (!compile_condition_branch_false(expr->children[0], ctx, false_label)) {
         return false;
      }
      return compile_condition_branch_false(expr->children[1], ctx, false_label);
   }

   if (expr->count == 2 && !strcmp(expr->name, "||")) {
      const char *rhs_label = next_label("or_rhs");
      const char *end_label = next_label("or_end");
      if (!rhs_label || !end_label) {
         free((void *) rhs_label);
         free((void *) end_label);
         return false;
      }
      if (!compile_condition_branch_false(expr->children[0], ctx, rhs_label)) {
         free((void *) rhs_label);
         free((void *) end_label);
         return false;
      }
      emit(&es_code, "    jmp %s\n", end_label);
      emit(&es_code, "%s:\n", rhs_label);
      if (!compile_condition_branch_false(expr->children[1], ctx, false_label)) {
         free((void *) rhs_label);
         free((void *) end_label);
         return false;
      }
      emit(&es_code, "%s:\n", end_label);
      free((void *) rhs_label);
      free((void *) end_label);
      return true;
   }

   if (expr->count == 2 &&
       (!strcmp(expr->name, "==") || !strcmp(expr->name, "!=") ||
        !strcmp(expr->name, "<")  || !strcmp(expr->name, ">")  ||
        !strcmp(expr->name, "<=") || !strcmp(expr->name, ">="))) {
      const ASTNode *type = expr_value_type(expr->children[0], ctx);
      int size = expr_value_size(expr->children[0], ctx);
      int compare_size;
      ContextEntry lhs;
      ContextEntry rhs;
      const char *helper = NULL;
      bool invert = false;
      bool is_signed;

      if (size <= 0) {
         size = get_size("int");
      }
      compare_size = size * 2;
      lhs = (ContextEntry){ .name = "$lhs", .type = type, .declarator = NULL, .is_static = false, .is_quick = false, .is_global = false, .offset = ctx->locals, .size = size };
      rhs = (ContextEntry){ .name = "$rhs", .type = type, .declarator = NULL, .is_static = false, .is_quick = false, .is_global = false, .offset = ctx->locals + size, .size = size };
      is_signed = type && has_flag(type_name_from_node(type), "$signed");

      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", compare_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");

      if (!compile_expr_to_slot(expr->children[0], ctx, &lhs) ||
          !compile_expr_to_slot(expr->children[1], ctx, &rhs)) {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", compare_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return false;
      }

      if (!strcmp(expr->name, "==")) {
         helper = "eqN";
      }
      else if (!strcmp(expr->name, "!=")) {
         helper = "eqN";
         invert = true;
      }
      else if (!strcmp(expr->name, "<")) {
         helper = is_signed ? "ltNs" : "ltNu";
      }
      else if (!strcmp(expr->name, ">")) {
         helper = is_signed ? "ltNs" : "ltNu";
         ContextEntry t = lhs; lhs = rhs; rhs = t;
      }
      else if (!strcmp(expr->name, "<=")) {
         helper = is_signed ? "leNs" : "leNu";
      }
      else if (!strcmp(expr->name, ">=")) {
         helper = is_signed ? "leNs" : "leNu";
         ContextEntry t = lhs; lhs = rhs; rhs = t;
      }

      emit_prepare_fp_ptr(0, lhs.offset);
      emit_prepare_fp_ptr(1, rhs.offset);
      emit(&es_code, "    lda #$%02x\n", size & 0xff);
      emit(&es_code, "    sta arg0\n");
      remember_runtime_import(helper);
      emit(&es_code, "    jsr _%s\n", helper);

      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", compare_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      emit(&es_code, "    lda arg1\n");
      emit(&es_code, "    %s %s\n", invert ? "bne" : "beq", false_label);
      return true;
   }

   {
      const ASTNode *type = expr_value_type(expr, ctx);
      int size = expr_value_size(expr, ctx);
      ContextEntry tmp;

      if (size <= 0) {
         size = get_size("int");
      }
      tmp = (ContextEntry){ .name = "$tmp", .type = type, .declarator = NULL, .is_static = false, .is_quick = false, .is_global = false, .offset = ctx->locals, .size = size };

      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");

      if (!compile_expr_to_slot(expr, ctx, &tmp)) {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return false;
      }

      emit(&es_code, "    lda #0\n");
      for (int i = 0; i < size; i++) {
         emit(&es_code, "    ldy #%d\n", tmp.offset + i);
         emit(&es_code, "    ora (fp),y\n");
      }
      emit(&es_code, "    sta arg1\n");

      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      emit(&es_code, "    lda arg1\n");
      emit(&es_code, "    beq %s\n", false_label);
      return true;
   }
}

static void compile_if_stmt(ASTNode *node, Context *ctx) {
   const char *false_label = next_label("if_false");
   const char *end_label = next_label("if_end");
   ASTNode *cond = node->children[0];
   ASTNode *then_block = node->children[1];
   ASTNode *else_block = (node->count > 2) ? node->children[2] : NULL;

   if (!compile_condition_branch_false(cond, ctx, false_label)) {
      warning("[%s:%d.%d] if condition not compiled yet", node->file, node->line, node->column);
      free((void *) false_label);
      free((void *) end_label);
      return;
   }

   compile_statement_list(then_block, ctx);
   if (else_block && !is_empty(else_block)) {
      emit(&es_code, "    jmp %s\n", end_label);
   }
   emit(&es_code, "%s:\n", false_label);
   if (else_block && !is_empty(else_block)) {
      compile_statement_list(else_block, ctx);
      emit(&es_code, "%s:\n", end_label);
   }
   free((void *) false_label);
   free((void *) end_label);
}

static void compile_while_stmt(ASTNode *node, Context *ctx) {
   const char *start_label = next_label("while_start");
   const char *end_label = next_label("while_end");
   const char *named_loop = pending_loop_label_name;
   ASTNode *cond = node->children[0];
   ASTNode *body = node->children[1];

   pending_loop_label_name = NULL;

   if (!start_label || !end_label) {
      free((void *) start_label);
      free((void *) end_label);
      warning("[%s:%d.%d] while label generation failed", node->file, node->line, node->column);
      return;
   }

   push_loop_labels(end_label, start_label);
   if (named_loop) {
      push_named_loop_labels(named_loop, end_label, start_label);
   }
   emit(&es_code, "%s:\n", start_label);
   if (!compile_condition_branch_false(cond, ctx, end_label)) {
      warning("[%s:%d.%d] while condition not compiled yet", node->file, node->line, node->column);
      pop_loop_labels();
      if (named_loop) {
         pop_named_loop_labels();
      }
      free((void *) start_label);
      free((void *) end_label);
      return;
   }
   compile_statement_list(body, ctx);
   emit(&es_code, "    jmp %s\n", start_label);
   emit(&es_code, "%s:\n", end_label);
   pop_loop_labels();
   if (named_loop) {
      pop_named_loop_labels();
   }
   free((void *) start_label);
   free((void *) end_label);
}

static void compile_for_stmt(ASTNode *node, Context *ctx) {
   const char *start_label = next_label("for_start");
   const char *step_label = next_label("for_step");
   const char *end_label = next_label("for_end");
   const char *named_loop = pending_loop_label_name;
   ASTNode *init = node->children[0];
   ASTNode *cond = node->children[1];
   ASTNode *step = node->children[2];
   ASTNode *body = node->children[3];

   pending_loop_label_name = NULL;

   if (!start_label || !step_label || !end_label) {
      free((void *) start_label);
      free((void *) step_label);
      free((void *) end_label);
      warning("[%s:%d.%d] for label generation failed", node->file, node->line, node->column);
      return;
   }

   push_loop_labels(end_label, step_label);
   if (named_loop) {
      push_named_loop_labels(named_loop, end_label, step_label);
   }
   if (init && !is_empty(init)) {
      compile_expr(init, ctx);
   }

   emit(&es_code, "%s:\n", start_label);
   if (cond && !is_empty(cond)) {
      if (!compile_condition_branch_false(cond, ctx, end_label)) {
         warning("[%s:%d.%d] for condition not compiled yet", node->file, node->line, node->column);
         pop_loop_labels();
         if (named_loop) {
            pop_named_loop_labels();
         }
         free((void *) start_label);
         free((void *) step_label);
         free((void *) end_label);
         return;
      }
   }
   compile_statement_list(body, ctx);
   emit(&es_code, "%s:\n", step_label);
   if (step && !is_empty(step)) {
      compile_expr(step, ctx);
   }
   emit(&es_code, "    jmp %s\n", start_label);
   emit(&es_code, "%s:\n", end_label);
   pop_loop_labels();
   if (named_loop) {
      pop_named_loop_labels();
   }
   free((void *) start_label);
   free((void *) step_label);
   free((void *) end_label);
}

static bool compile_expr_to_return_slot(ASTNode *expr, Context *ctx, ContextEntry *ret) {
   return compile_expr_to_slot(expr, ctx, ret);
}

static void compile_break_stmt(ASTNode *node, Context *ctx) {
   const char *target = current_break_label();

   (void) ctx;
   if (node->count > 0 && node->children[0] && !is_empty(node->children[0])) {
      target = lookup_named_break_label(node->children[0]->strval);
      if (!target) {
         warning("[%s:%d.%d] labeled break target '%s' not found", node->file, node->line, node->column, node->children[0]->strval);
         return;
      }
   }
   else if (!target) {
      warning("[%s:%d.%d] break used outside loop not compiled", node->file, node->line, node->column);
      return;
   }

   emit(&es_code, "    jmp %s\n", target);
}

static void compile_continue_stmt(ASTNode *node, Context *ctx) {
   const char *target = current_continue_label();

   (void) ctx;
   if (node->count > 0 && node->children[0] && !is_empty(node->children[0])) {
      target = lookup_named_continue_label(node->children[0]->strval);
      if (!target) {
         warning("[%s:%d.%d] labeled continue target '%s' not found", node->file, node->line, node->column, node->children[0]->strval);
         return;
      }
   }
   else if (!target) {
      warning("[%s:%d.%d] continue used outside loop not compiled", node->file, node->line, node->column);
      return;
   }

   emit(&es_code, "    jmp %s\n", target);
}

static void predeclare_local_decl_item(ASTNode *node, Context *ctx) {
   ASTNode *modifiers  = node->children[0];
   ASTNode *type       = node->children[1];
   ASTNode *declarator = node->children[2];
   const char *name    = declarator->children[1]->strval;
   ASTNode *expression = node->children[node->count - 1];
   int size            = declarator_storage_size(type, declarator);
   ContextEntry *entry = (ContextEntry *) set_get(ctx->vars, name);

   if (entry != NULL) {
      return;
   }

   if (has_modifier(modifiers, "static")) {
      ctx_static(ctx, type, name, is_empty(expression));
      entry = (ContextEntry *) set_get(ctx->vars, name);
   }
   else if (has_modifier(modifiers, "quick")) {
      ctx_quick(ctx, type, name, is_empty(expression));
      entry = (ContextEntry *) set_get(ctx->vars, name);
   }
   else {
      ctx_push(ctx, type, name);
      entry = (ContextEntry *) set_get(ctx->vars, name);
   }

   if (entry != NULL) {
      entry->size = size;
      entry->declarator = declarator;
   }
}


static bool type_is_aggregate(const ASTNode *type) {
   const ASTNode *node;
   if (!type) {
      return false;
   }
   node = get_typename_node(type_name_from_node(type));
   return node && (!strcmp(node->name, "struct_decl_stmt") || !strcmp(node->name, "union_decl_stmt"));
}

static bool initializer_is_list(const ASTNode *init) {
   if (!init || is_empty(init)) {
      return false;
   }
   return !strcmp(init->name, "expr_list") || !strcmp(init->name, "named_expr");
}

static int initializer_item_count(const ASTNode *node) {
   if (!node || is_empty(node)) {
      return 0;
   }
   if (!strcmp(node->name, "expr_list")) {
      int total = 0;
      for (int i = 0; i < node->count; i++) {
         total += initializer_item_count(node->children[i]);
      }
      return total;
   }
   return 1;
}

static void initializer_collect_items(const ASTNode *node, const ASTNode **items, int *index) {
   if (!node || is_empty(node)) {
      return;
   }
   if (!strcmp(node->name, "expr_list")) {
      for (int i = 0; i < node->count; i++) {
         initializer_collect_items(node->children[i], items, index);
      }
      return;
   }
   items[(*index)++] = node;
}

static int scalar_storage_size(const ASTNode *type, const ASTNode *declarator, int total_size) {
   if (total_size > 0) {
      return total_size;
   }
   if (declarator) {
      return declarator_storage_size(type, declarator);
   }
   return get_size(type_name_from_node(type));
}

static bool compile_initializer_to_fp(const ASTNode *init, Context *ctx, const ASTNode *type, const ASTNode *declarator, int base_offset, int total_size) {
   const ASTNode *uinit = unwrap_expr_node((ASTNode *) init);
   int size = scalar_storage_size(type, declarator, total_size);

   if (!uinit || is_empty(uinit)) {
      return true;
   }

   if (!initializer_is_list(uinit)) {
      ContextEntry dst = { .name = "$init", .type = type, .declarator = declarator, .is_static = false, .is_quick = false, .is_global = false, .offset = base_offset, .size = size };
      return compile_expr_to_slot((ASTNode *) uinit, ctx, &dst);
   }

   if (declarator && declarator_array_count(declarator) > 0 && declarator_pointer_depth(declarator) == 0) {
      int item_count = initializer_item_count(uinit);
      const ASTNode **items = (const ASTNode **) calloc(item_count ? item_count : 1, sizeof(*items));
      int index = 0;
      int elem_count = atoi(declarator->children[2]->strval);
      int elem_size = declarator_first_element_size(type, declarator);
      bool ok = true;
      initializer_collect_items(uinit, items, &index);
      for (int i = 0; i < index && i < elem_count; i++) {
         const ASTNode *item = items[i];
         if (!item || item->count < 2) {
            continue;
         }
         if (!is_empty(item->children[0])) {
            ok = false;
            break;
         }
         ok = compile_initializer_to_fp(item->children[1], ctx, type, NULL, base_offset + i * elem_size, elem_size);
         if (!ok) {
            break;
         }
      }
      free(items);
      return ok;
   }

   if (type_is_aggregate(type)) {
      const ASTNode *agg = get_typename_node(type_name_from_node(type));
      int item_count = initializer_item_count(uinit);
      const ASTNode **items = (const ASTNode **) calloc(item_count ? item_count : 1, sizeof(*items));
      int index = 0;
      int field_pos = 1;
      bool is_union = agg && !strcmp(agg->name, "union_decl_stmt");
      bool ok = true;

      initializer_collect_items(uinit, items, &index);
      for (int i = 0; i < index; i++) {
         const ASTNode *item = items[i];
         const ASTNode *ftype = NULL;
         const ASTNode *fdecl = NULL;
         int offset = 0;
         if (!item || item->count < 2) {
            continue;
         }
         if (!is_empty(item->children[0])) {
            if (!find_aggregate_member(type, item->children[0]->strval, &ftype, &fdecl, &offset)) {
               ok = false;
               break;
            }
         }
         else {
            for (; agg && field_pos < agg->count; field_pos++) {
               const ASTNode *field = agg->children[field_pos];
               if (!field || field->count < 3) {
                  continue;
               }
               ftype = field->children[1];
               fdecl = field->children[2];
               find_aggregate_member(type, fdecl->children[1]->strval, NULL, NULL, &offset);
               field_pos++;
               break;
            }
            if (!ftype || !fdecl) {
               ok = false;
               break;
            }
         }
         ok = compile_initializer_to_fp(item->children[1], ctx, ftype, fdecl, base_offset + offset, declarator_storage_size(ftype, fdecl));
         if (!ok || is_union) {
            break;
         }
      }
      free(items);
      return ok;
   }

   return false;
}

static bool build_initializer_bytes(unsigned char *buf, int buf_size, int base_offset, const ASTNode *init, const ASTNode *type, const ASTNode *declarator, int total_size) {
   const ASTNode *uinit = unwrap_expr_node((ASTNode *) init);
   int size = scalar_storage_size(type, declarator, total_size);

   if (!uinit || is_empty(uinit)) {
      return true;
   }
   if (base_offset < 0 || base_offset + size > buf_size) {
      return false;
   }

   if (!initializer_is_list(uinit)) {
      if (uinit->kind != AST_INTEGER) {
         return false;
      }
      if (has_flag(type_name_from_node(type), "$endian:big")) {
         make_be_int(uinit->strval, buf + base_offset, size);
      }
      else {
         make_le_int(uinit->strval, buf + base_offset, size);
      }
      return true;
   }

   if (declarator && declarator_array_count(declarator) > 0 && declarator_pointer_depth(declarator) == 0) {
      int item_count = initializer_item_count(uinit);
      const ASTNode **items = (const ASTNode **) calloc(item_count ? item_count : 1, sizeof(*items));
      int index = 0;
      int elem_count = atoi(declarator->children[2]->strval);
      int elem_size = declarator_first_element_size(type, declarator);
      bool ok = true;
      initializer_collect_items(uinit, items, &index);
      for (int i = 0; i < index && i < elem_count; i++) {
         const ASTNode *item = items[i];
         if (!item || item->count < 2) {
            continue;
         }
         if (!is_empty(item->children[0])) {
            ok = false;
            break;
         }
         ok = build_initializer_bytes(buf, buf_size, base_offset + i * elem_size, item->children[1], type, NULL, elem_size);
         if (!ok) {
            break;
         }
      }
      free(items);
      return ok;
   }

   if (type_is_aggregate(type)) {
      const ASTNode *agg = get_typename_node(type_name_from_node(type));
      int item_count = initializer_item_count(uinit);
      const ASTNode **items = (const ASTNode **) calloc(item_count ? item_count : 1, sizeof(*items));
      int index = 0;
      int field_pos = 1;
      bool is_union = agg && !strcmp(agg->name, "union_decl_stmt");
      bool ok = true;

      initializer_collect_items(uinit, items, &index);
      for (int i = 0; i < index; i++) {
         const ASTNode *item = items[i];
         const ASTNode *ftype = NULL;
         const ASTNode *fdecl = NULL;
         int offset = 0;
         if (!item || item->count < 2) {
            continue;
         }
         if (!is_empty(item->children[0])) {
            if (!find_aggregate_member(type, item->children[0]->strval, &ftype, &fdecl, &offset)) {
               ok = false;
               break;
            }
         }
         else {
            for (; agg && field_pos < agg->count; field_pos++) {
               const ASTNode *field = agg->children[field_pos];
               if (!field || field->count < 3) {
                  continue;
               }
               ftype = field->children[1];
               fdecl = field->children[2];
               find_aggregate_member(type, fdecl->children[1]->strval, NULL, NULL, &offset);
               field_pos++;
               break;
            }
            if (!ftype || !fdecl) {
               ok = false;
               break;
            }
         }
         ok = build_initializer_bytes(buf, buf_size, base_offset + offset, item->children[1], ftype, fdecl, declarator_storage_size(ftype, fdecl));
         if (!ok || is_union) {
            break;
         }
      }
      free(items);
      return ok;
   }

   return false;
}

static void predeclare_statement_list(ASTNode *node, Context *ctx) {
   if (!node || is_empty(node)) {
      return;
   }

   for (int i = 0; i < node->count; i++) {
      ASTNode *stmt = node->children[i];
      if (!strcmp(stmt->name, "defdecl_stmt")) {
         ASTNode *list = stmt->children[0];
         for (int j = 0; j < list->count; j++) {
            predeclare_local_decl_item(list->children[j], ctx);
         }
      }
   }
}

static void compile_local_decl_item(ASTNode *node, Context *ctx) {
   ASTNode *type       = node->children[1];
   ASTNode *declarator = node->children[2];
   const char *name    = declarator->children[1]->strval;
   ASTNode *expression = node->children[node->count - 1];
   int size            = declarator_storage_size(type, declarator);
   ContextEntry *entry;

   entry = (ContextEntry *) set_get(ctx->vars, name);
   if (entry == NULL) {
      predeclare_local_decl_item(node, ctx);
      entry = (ContextEntry *) set_get(ctx->vars, name);
   }
   if (entry != NULL) {
      entry->size = size;
      entry->declarator = declarator;
   }

   while (expression && expression->count == 1 && !strcmp(expression->name, "assign_expr")) {
      expression = expression->children[0];
   }

   if (!is_empty(expression) && !entry->is_static && !entry->is_quick) {
      if (initializer_is_list(unwrap_expr_node(expression)) || declarator_array_count(declarator) > 0 || type_is_aggregate(type)) {
         unsigned char *zeroes = (unsigned char *) calloc(size ? size : 1, sizeof(unsigned char));
         if (zeroes) {
            emit_store_immediate_to_fp(entry->offset, zeroes, size);
            free(zeroes);
         }
         if (!compile_initializer_to_fp(expression, ctx, type, declarator, entry->offset, size)) {
            warning("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
         }
      }
      else if (!compile_expr_to_slot(expression, ctx, entry)) {
         warning("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
      }
   }
   else if (!is_empty(expression)) {
      char sym[256];
      unsigned char *bytes;
      EmitSink *sink;
      if (!entry_symbol_name(ctx, entry, sym, sizeof(sym))) {
         warning("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
         return;
      }
      sink = entry->is_quick ? &es_zpdata : &es_data;
      bytes = (unsigned char *) calloc(size ? size : 1, sizeof(unsigned char));
      if (bytes && build_initializer_bytes(bytes, size, 0, expression, type, declarator, size)) {
         emit_bytes_direct(sink, bytes, size);
      }
      else {
         warning("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
         emit(sink, "	.res %d\n", size);
      }
      free(bytes);
   }
}


static void compile_do_stmt(ASTNode *node, Context *ctx) {
   const char *start_label = next_label("do_start");
   const char *cond_label = next_label("do_cond");
   const char *end_label = next_label("do_end");
   const char *named_loop = pending_loop_label_name;
   pending_loop_label_name = NULL;
   if (!start_label || !cond_label || !end_label) {
      free((void *) start_label);
      free((void *) cond_label);
      free((void *) end_label);
      warning("[%s:%d.%d] failed to allocate labels for do statement", node->file, node->line, node->column);
      return;
   }

   emit(&es_code, "%s:\n", start_label);
   push_loop_labels(end_label, cond_label);
   if (named_loop) {
      push_named_loop_labels(named_loop, end_label, cond_label);
   }
   compile_statement_list(node->children[0], ctx);
   emit(&es_code, "%s:\n", cond_label);
   if (!compile_condition_branch_false(node->children[1], ctx, end_label)) {
      warning("[%s:%d.%d] do/while condition not compiled yet", node->file, node->line, node->column);
   }
   emit(&es_code, "    jmp %s\n", start_label);
   emit(&es_code, "%s:\n", end_label);
   pop_loop_labels();
   if (named_loop) {
      pop_named_loop_labels();
   }

   free((void *) start_label);
   free((void *) cond_label);
   free((void *) end_label);
}

static void compile_label_stmt(ASTNode *node, Context *ctx) {
   (void) ctx;
   emit(&es_code, "@user_%s:\n", node->children[0]->strval);
   if (node->count > 1) {
      ASTNode *stmt = node->children[1];
      const char *saved_pending = pending_loop_label_name;
      if (!strcmp(stmt->name, "while_stmt") || !strcmp(stmt->name, "for_stmt") || !strcmp(stmt->name, "do_stmt") || !strcmp(stmt->name, "switch_stmt")) {
         pending_loop_label_name = node->children[0]->strval;
      }
      if (!strcmp(stmt->name, "return_stmt")) {
         compile_return_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "expr") || !strcmp(stmt->name, "assign_expr")) {
         compile_expr(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "if_stmt")) {
         compile_if_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "while_stmt")) {
         compile_while_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "for_stmt")) {
         compile_for_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "do_stmt")) {
         compile_do_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "break_stmt")) {
         compile_break_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "continue_stmt")) {
         compile_continue_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "goto_stmt")) {
         compile_goto_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "switch_stmt")) {
         compile_switch_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "label_stmt")) {
         compile_label_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "defdecl_stmt")) {
         ASTNode *list = stmt->children[0];
         for (int j = 0; j < list->count; j++) {
            compile_local_decl_item(list->children[j], ctx);
         }
      }
      else {
         warning("[%s:%d.%d] labeled statement '%s' not compiled yet", stmt->file, stmt->line, stmt->column, stmt->name);
      }
      pending_loop_label_name = saved_pending;
   }
}

static void compile_goto_stmt(ASTNode *node, Context *ctx) {
   (void) ctx;
   if (node->count > 0 && !is_empty(node->children[0])) {
      emit(&es_code, "    jmp @user_%s\n", node->children[0]->strval);
   }
}

static void compile_switch_stmt(ASTNode *node, Context *ctx) {
   const char *named_loop = pending_loop_label_name;
   ASTNode *expr;
   ASTNode *sections;
   const ASTNode *type;
   int size;
   int compare_size;
   ContextEntry lhs;
   ContextEntry rhs;
   const char *cleanup_label;
   const char *default_label = NULL;
   const char *end_label = NULL;
   const char **case_labels = NULL;
   int section_count;

   pending_loop_label_name = NULL;

   if (!node || node->count < 2) {
      return;
   }

   expr = node->children[0];
   sections = node->children[1];
   if (!sections || is_empty(sections) || sections->count <= 0) {
      return;
   }

   type = expr_value_type(expr, ctx);
   size = expr_value_size(expr, ctx);
   if (size <= 0) {
      size = get_size("int");
   }
   compare_size = size * 2;
   lhs = (ContextEntry){ .name = "$lhs", .type = type, .declarator = NULL, .is_static = false, .is_quick = false, .is_global = false, .offset = ctx->locals, .size = size };
   rhs = (ContextEntry){ .name = "$rhs", .type = type, .declarator = NULL, .is_static = false, .is_quick = false, .is_global = false, .offset = ctx->locals + size, .size = size };
   cleanup_label = next_label("switch_cleanup");
   end_label = next_label("switch_end");
   if (!cleanup_label || !end_label) {
      free((void *) cleanup_label);
      free((void *) end_label);
      warning("[%s:%d.%d] switch label generation failed", node->file, node->line, node->column);
      return;
   }

   section_count = sections->count;
   case_labels = calloc((size_t)section_count, sizeof(*case_labels));
   if (!case_labels) {
      free((void *) cleanup_label);
      free((void *) end_label);
      error("out of memory");
   }

   remember_runtime_import("pushN");
   emit(&es_code, "    lda #$%02x\n", compare_size & 0xff);
   emit(&es_code, "    sta arg0\n");
   emit(&es_code, "    jsr _pushN\n");

   if (!compile_expr_to_slot(expr, ctx, &lhs)) {
      warning("[%s:%d.%d] switch expression not compiled yet", node->file, node->line, node->column);
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", compare_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      free(case_labels);
      free((void *) cleanup_label);
      free((void *) end_label);
      return;
   }

   for (int i = 0; i < section_count; i++) {
      ASTNode *section = sections->children[i];
      case_labels[i] = next_label("case");
      if (!case_labels[i]) {
         warning("[%s:%d.%d] switch case label generation failed", node->file, node->line, node->column);
         default_label = cleanup_label;
         break;
      }
      if (section->children[0] && is_empty(section->children[0])) {
         default_label = case_labels[i];
      }
   }

   for (int i = 0; i < section_count; i++) {
      ASTNode *section = sections->children[i];
      ASTNode *case_expr = section->children[0];
      if (!case_labels[i] || (case_expr && is_empty(case_expr))) {
         continue;
      }
      if (!compile_expr_to_slot(case_expr, ctx, &rhs)) {
         warning("[%s:%d.%d] case expression not compiled yet", section->file, section->line, section->column);
         continue;
      }
      emit_prepare_fp_ptr(0, lhs.offset);
      emit_prepare_fp_ptr(1, rhs.offset);
      emit(&es_code, "    lda #$%02x\n", size & 0xff);
      emit(&es_code, "    sta arg0\n");
      remember_runtime_import("eqN");
      emit(&es_code, "    jsr _eqN\n");
      emit(&es_code, "    lda arg1\n");
      emit(&es_code, "    bne %s\n", case_labels[i]);
   }

   emit(&es_code, "    jmp %s\n", default_label ? default_label : cleanup_label);

   push_loop_labels(cleanup_label, current_continue_label());
   if (named_loop) {
      push_named_loop_labels(named_loop, cleanup_label, current_continue_label());
   }
   for (int i = 0; i < section_count; i++) {
      ASTNode *section = sections->children[i];
      ASTNode *body = (section->count > 1) ? section->children[1] : NULL;
      if (!case_labels[i]) {
         continue;
      }
      emit(&es_code, "%s:\n", case_labels[i]);
      if (body && !is_empty(body)) {
         compile_statement_list(body, ctx);
      }
   }
   pop_loop_labels();
   if (named_loop) {
      pop_named_loop_labels();
   }

   emit(&es_code, "%s:\n", cleanup_label);
   remember_runtime_import("popN");
   emit(&es_code, "    lda #$%02x\n", compare_size & 0xff);
   emit(&es_code, "    sta arg0\n");
   emit(&es_code, "    jsr _popN\n");
   emit(&es_code, "%s:\n", end_label);

   for (int i = 0; i < section_count; i++) {
      free((void *) case_labels[i]);
   }
   free(case_labels);
   free((void *) cleanup_label);
   free((void *) end_label);
}

static void compile_return_stmt(ASTNode *node, Context *ctx) {
   ContextEntry *ret = (ContextEntry *) set_get(ctx->vars, "$$");
   ASTNode *expr = (node->count > 0) ? node->children[0] : NULL;

   if (!ret) {
      error("[%s:%d.%d] internal missing return slot", node->file, node->line, node->column);
   }

   if (!expr || is_empty(expr)) {
      emit(&es_code, "    jmp @fini\n");
      return;
   }

   if (!compile_expr_to_return_slot(expr, ctx, ret)) {
      warning("[%s:%d.%d] return expression not compiled yet", node->file, node->line, node->column);
   }
   emit(&es_code, "    jmp @fini\n");
}

static void compile_expr(ASTNode *node, Context *ctx) {
   if (!node || is_empty(node)) {
      return;
   }

   node = (ASTNode *) unwrap_expr_node(node);

   if (!strcmp(node->name, "()")) {
      if (!compile_call_expr_to_slot(node, ctx, NULL)) {
         warning("[%s:%d.%d] call expression not compiled yet", node->file, node->line, node->column);
      }
      return;
   }

   if (!strcmp(node->name, "lvalue") && node->count >= 3 && node->children[2] &&
       node->children[2]->kind == AST_IDENTIFIER &&
       (!strcmp(node->children[2]->strval, "pre++") ||
        !strcmp(node->children[2]->strval, "post++") ||
        !strcmp(node->children[2]->strval, "pre--") ||
        !strcmp(node->children[2]->strval, "post--"))) {
      ContextEntry *dst = ctx_lookup_lvalue(ctx, node);
      unsigned char *one;
      bool inc;
      if (!dst) {
         warning("[%s:%d.%d] increment/decrement target not compiled yet", node->file, node->line, node->column);
         return;
      }
      {
         int step = 1;
         if (dst->declarator && declarator_pointer_depth(dst->declarator) > 0) {
            step = declarator_first_element_size(dst->type, dst->declarator);
            if (step <= 0) {
               step = 1;
            }
         }
         if (dst->is_static || dst->is_quick || dst->is_global) {
            char sym[256];
            char step_buf[64];
            if (!entry_symbol_name(ctx, dst, sym, sizeof(sym))) {
               warning("[%s:%d.%d] increment/decrement target not compiled yet", node->file, node->line, node->column);
               return;
            }
            one = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
            if (!one) {
               return;
            }
            snprintf(step_buf, sizeof(step_buf), "%d", step);
            if (has_flag(type_name_from_node(dst->type), "$endian:big")) {
               make_be_int(step_buf, one, dst->size);
            }
            else {
               make_le_int(step_buf, one, dst->size);
            }
            inc = !strcmp(node->children[2]->strval, "pre++") || !strcmp(node->children[2]->strval, "post++");
            if (inc) {
               emit_add_immediate_to_symbol(dst->type, sym, one, dst->size);
            }
            else {
               emit_sub_immediate_from_symbol(dst->type, sym, one, dst->size);
            }
            free(one);
            return;
         }
         one = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
         if (!one) {
            return;
         }
         {
            char step_buf[64];
            snprintf(step_buf, sizeof(step_buf), "%d", step);
            if (has_flag(type_name_from_node(dst->type), "$endian:big")) {
               make_be_int(step_buf, one, dst->size);
            }
            else {
               make_le_int(step_buf, one, dst->size);
            }
         }
         inc = !strcmp(node->children[2]->strval, "pre++") || !strcmp(node->children[2]->strval, "post++");
         if (inc) {
            emit_add_immediate_to_fp(dst->type, dst->offset, one, dst->size);
         }
         else {
            emit_sub_immediate_from_fp(dst->type, dst->offset, one, dst->size);
         }
         free(one);
         return;
      }
   }

   if (!node || strcmp(node->name, "assign_expr") || node->count != 3) {
      const ASTNode *type = expr_value_type(node, ctx);
      int size = expr_value_size(node, ctx);
      if (size <= 0) {
         size = get_size("int");
      }
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      if (!compile_expr_to_slot(node, ctx, &(ContextEntry){ .name = "$tmp", .type = type, .declarator = NULL, .is_static = false, .is_quick = false, .is_global = false, .offset = ctx->locals, .size = size })) {
         remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
         warning("[%s:%d.%d] expression not compiled yet", node->file, node->line, node->column);
         return;
      }
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      return;
   }

   LValueRef lv;
   ContextEntry dst_store;
   ContextEntry *dst;
   const char *op = node->children[0] ? node->children[0]->strval : NULL;
   ASTNode *rhs = node->children[2];
   if (!resolve_lvalue(ctx, node->children[1], &lv)) {
      warning("[%s:%d.%d] assignment target not compiled yet", node->file, node->line, node->column);
      return;
   }
   dst_store = (ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_quick = lv.is_quick, .is_global = lv.is_global, .offset = lv.offset, .size = lv.size };
   dst = &dst_store;


   if (!op || !strcmp(op, ":=")) {
      if (dst->is_static || dst->is_quick || dst->is_global) {
         char sym[256];
         if (!entry_symbol_name(ctx, dst, sym, sizeof(sym))) {
            warning("[%s:%d.%d] assignment target not compiled yet", node->file, node->line, node->column);
            return;
         }
         if (!compile_expr_to_slot(rhs, ctx, &(ContextEntry){ .name = "$tmp", .type = dst->type, .declarator = NULL, .is_static = false, .is_quick = false, .is_global = false, .offset = ctx->locals, .size = dst->size })) {
            warning("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
            return;
         }
         emit_copy_fp_to_symbol(sym, ctx->locals, dst->size);
         return;
      }
      if (lv.indirect) {
         int tmp_size = dst->size > 0 ? dst->size : get_size("int");
         ContextEntry tmp = { .name = "$tmp", .type = dst->type, .declarator = NULL, .is_static = false, .is_quick = false, .is_global = false, .offset = ctx->locals, .size = tmp_size };
         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         if (!compile_expr_to_slot(rhs, ctx, &tmp)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            warning("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
            return;
         }
         emit_copy_fp_to_lvalue(&lv, tmp.offset, tmp.size);
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
      }
      else if (!compile_expr_to_slot(rhs, ctx, dst)) {
         warning("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
      }
      return;
   }

   rhs = (ASTNode *) unwrap_expr_node(rhs);
   if (!rhs) {
      warning("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
      return;
   }

   if (!strcmp(op, "+=") || !strcmp(op, "-=") || !strcmp(op, "&=") || !strcmp(op, "|=") ||
       !strcmp(op, "^=") || !strcmp(op, "*=") || !strcmp(op, "/=") || !strcmp(op, "%=") ||
       !strcmp(op, "<<=") || !strcmp(op, ">>=")) {
      char dst_sym[256];
      bool dst_symbol = (dst->is_static || dst->is_quick || dst->is_global) && entry_symbol_name(ctx, dst, dst_sym, sizeof(dst_sym));
      bool scaled_pointer_assign = dst->declarator && declarator_pointer_depth(dst->declarator) > 0 && (!strcmp(op, "+=") || !strcmp(op, "-="));
      int pointer_scale = 1;
      if (scaled_pointer_assign) {
         pointer_scale = declarator_first_element_size(dst->type, dst->declarator);
         if (pointer_scale <= 0) {
            pointer_scale = 1;
         }
      }
      if ((!strcmp(op, "+=") || !strcmp(op, "-=")) && !scaled_pointer_assign) {
      if (rhs->kind == AST_INTEGER) {
         unsigned char *bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
         if (!bytes) {
            return;
         }
         if (has_flag(type_name_from_node(dst->type), "$endian:big")) {
            make_be_int(rhs->strval, bytes, dst->size);
         }
         else {
            make_le_int(rhs->strval, bytes, dst->size);
         }
         if (!strcmp(op, "+=")) {
            if (dst_symbol) emit_add_immediate_to_symbol(dst->type, dst_sym, bytes, dst->size);
            else emit_add_immediate_to_fp(dst->type, dst->offset, bytes, dst->size);
         }
         else {
            if (dst_symbol) emit_sub_immediate_from_symbol(dst->type, dst_sym, bytes, dst->size);
            else emit_sub_immediate_from_fp(dst->type, dst->offset, bytes, dst->size);
         }
         free(bytes);
         return;
      }

      if (rhs->kind == AST_IDENTIFIER) {
         ContextEntry *src = ctx_lookup(ctx, rhs->strval);
         if (src && !src->is_static && !src->is_quick) {
            if (!strcmp(op, "+=")) {
               if (dst_symbol) emit_add_fp_to_symbol(dst->type, dst_sym, src->offset, src->size < dst->size ? src->size : dst->size);
               else emit_add_fp_to_fp(dst->type, dst->offset, src->offset, src->size < dst->size ? src->size : dst->size);
            }
            else {
               if (dst_symbol) emit_sub_fp_from_symbol(dst->type, dst_sym, src->offset, src->size < dst->size ? src->size : dst->size);
               else emit_sub_fp_from_fp(dst->type, dst->offset, src->offset, src->size < dst->size ? src->size : dst->size);
            }
            return;
         }
         if (src) {
            char src_sym[256];
            if (entry_symbol_name(ctx, src, src_sym, sizeof(src_sym))) {
               if (!strcmp(op, "+=")) {
                  if (dst_symbol) emit_add_symbol_to_symbol(dst->type, dst_sym, src_sym, src->size < dst->size ? src->size : dst->size);
                  else emit_add_symbol_to_fp(dst->type, dst->offset, src_sym, src->size < dst->size ? src->size : dst->size);
               }
               else {
                  if (dst_symbol) emit_sub_symbol_from_symbol(dst->type, dst_sym, src_sym, src->size < dst->size ? src->size : dst->size);
                  else emit_sub_symbol_from_fp(dst->type, dst->offset, src_sym, src->size < dst->size ? src->size : dst->size);
               }
               return;
            }
         }
         {
            const ASTNode *g = global_decl_lookup(rhs->strval);
            if (g && g->count >= 3) {
               char src_sym[256]; int gsize = declarator_storage_size(g->children[1], g->children[2]);
               snprintf(src_sym, sizeof(src_sym), "_%s", rhs->strval);
               if (!strcmp(op, "+=")) {
                  if (dst_symbol) emit_add_symbol_to_symbol(dst->type, dst_sym, src_sym, gsize < dst->size ? gsize : dst->size);
                  else emit_add_symbol_to_fp(dst->type, dst->offset, src_sym, gsize < dst->size ? gsize : dst->size);
               }
               else {
                  if (dst_symbol) emit_sub_symbol_from_symbol(dst->type, dst_sym, src_sym, gsize < dst->size ? gsize : dst->size);
                  else emit_sub_symbol_from_fp(dst->type, dst->offset, src_sym, gsize < dst->size ? gsize : dst->size);
               }
               return;
            }
         }
      }

      if (!strcmp(rhs->name, "lvalue") && rhs->count > 0) {
         ContextEntry *src = ctx_lookup_lvalue(ctx, rhs);
         if (src && !src->is_static && !src->is_quick) {
            if (!strcmp(op, "+=")) {
               emit_add_fp_to_fp(dst->type, dst->offset, src->offset, src->size < dst->size ? src->size : dst->size);
            }
            else {
               emit_sub_fp_from_fp(dst->type, dst->offset, src->offset, src->size < dst->size ? src->size : dst->size);
            }
            return;
         }
      }
      }
      else {
         int tmp_total = dst->size * 2;
         int factor_offset = 0;
         int scaled_rhs_offset = 0;
         int rhs_value_offset = ctx->locals + dst->size;
         int lhs_tmp_offset = ctx->locals;
         int rhs_tmp_offset = ctx->locals + dst->size;
         ContextEntry rhs_tmp = { .name = "$rhs_tmp", .type = expr_value_type(rhs, ctx), .declarator = NULL, .is_static = false, .is_quick = false, .is_global = false, .offset = rhs_tmp_offset, .size = dst->size };
         if (scaled_pointer_assign && pointer_scale != 1) {
            tmp_total += dst->size * 2;
            factor_offset = ctx->locals + dst->size * 2;
            scaled_rhs_offset = ctx->locals + dst->size * 3;
            rhs_value_offset = scaled_rhs_offset;
         }
         unsigned char *zeroes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
         if (!zeroes) {
            return;
         }
         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         emit_store_immediate_to_fp(lhs_tmp_offset, zeroes, dst->size);
         emit_store_immediate_to_fp(rhs_tmp_offset, zeroes, dst->size);
         if (scaled_pointer_assign && pointer_scale != 1) {
            emit_store_immediate_to_fp(factor_offset, zeroes, dst->size);
            emit_store_immediate_to_fp(scaled_rhs_offset, zeroes, dst->size);
         }
         free(zeroes);
         if (dst_symbol) {
            emit_copy_symbol_to_fp(lhs_tmp_offset, dst_sym, dst->size);
         }
         else if (lv.indirect) {
            emit_copy_lvalue_to_fp(lhs_tmp_offset, &lv, dst->size);
         }
         else {
            emit_copy_fp_to_fp(lhs_tmp_offset, dst->offset, dst->size);
         }
         if (!compile_expr_to_slot(rhs, ctx, &rhs_tmp)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            warning("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
            return;
         }
         if (scaled_pointer_assign && pointer_scale != 1) {
            unsigned char *factor_bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
            char scaled_buf[64];
            if (!factor_bytes) {
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
               return;
            }
            snprintf(scaled_buf, sizeof(scaled_buf), "%d", pointer_scale);
            if (has_flag(type_name_from_node(dst->type), "$endian:big")) {
               make_be_int(scaled_buf, factor_bytes, dst->size);
            }
            else {
               make_le_int(scaled_buf, factor_bytes, dst->size);
            }
            emit_store_immediate_to_fp(factor_offset, factor_bytes, dst->size);
            free(factor_bytes);
            emit_runtime_binary_fp_fp("mulN", scaled_rhs_offset, rhs_tmp_offset, factor_offset, dst->size);
         }
         if (!strcmp(op, "+=")) {
            emit_add_fp_to_fp(dst->type, lhs_tmp_offset, rhs_value_offset, dst->size);
         }
         else if (!strcmp(op, "-=")) {
            emit_sub_fp_from_fp(dst->type, lhs_tmp_offset, rhs_value_offset, dst->size);
         }
         else if (!strcmp(op, "&=")) {
            emit_runtime_binary_fp_fp("bit_andN", lhs_tmp_offset, lhs_tmp_offset, rhs_tmp_offset, dst->size);
         }
         else if (!strcmp(op, "|=")) {
            emit_runtime_binary_fp_fp("bit_orN", lhs_tmp_offset, lhs_tmp_offset, rhs_tmp_offset, dst->size);
         }
         else if (!strcmp(op, "^=")) {
            emit_runtime_binary_fp_fp("bit_xorN", lhs_tmp_offset, lhs_tmp_offset, rhs_tmp_offset, dst->size);
         }
         else if (!strcmp(op, "*=")) {
            emit_runtime_binary_fp_fp("mulN", rhs_tmp_offset, lhs_tmp_offset, rhs_tmp_offset, dst->size);
            emit_copy_fp_to_fp(lhs_tmp_offset, rhs_tmp_offset, dst->size);
         }
         else if (!strcmp(op, "/=") || !strcmp(op, "%=")) {
            int rem_offset = rhs_tmp_offset;
            int quo_offset = lhs_tmp_offset;
            emit_prepare_fp_ptr(0, lhs_tmp_offset);
            emit_prepare_fp_ptr(1, rhs_tmp_offset);
            emit_prepare_fp_ptr(2, quo_offset);
            emit_prepare_fp_ptr(3, rem_offset);
            emit(&es_code, "    lda #$%02x\n", dst->size & 0xff);
            emit(&es_code, "    sta arg0\n");
            remember_runtime_import("divN");
            emit(&es_code, "    jsr _divN\n");
            if (!strcmp(op, "%=")) {
               emit_copy_fp_to_fp(lhs_tmp_offset, rem_offset, dst->size);
            }
         }
         else if (!strcmp(op, "<<=") || !strcmp(op, ">>=")) {
            int scratch_offset = rhs_tmp_offset;
            const ASTNode *lhs_type = dst->type;
            const char *helper = !strcmp(op, "<<=") ? "lslN" : (lhs_type && has_flag(type_name_from_node(lhs_type), "$signed") ? "asrN" : "lsrN");
            emit_runtime_shift_fp(helper, lhs_tmp_offset, scratch_offset, rhs_tmp_offset, dst->size);
         }
         else {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            warning("[%s:%d.%d] expression '%s' not compiled yet", node->file, node->line, node->column, op);
            return;
         }
         if (dst_symbol) {
            emit_copy_fp_to_symbol(dst_sym, lhs_tmp_offset, dst->size);
         }
         else if (lv.indirect) {
            emit_copy_fp_to_lvalue(&lv, lhs_tmp_offset, dst->size);
         }
         else {
            emit_copy_fp_to_fp(dst->offset, lhs_tmp_offset, dst->size);
         }
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return;
      }
   }

   warning("[%s:%d.%d] expression '%s' not compiled yet", node->file, node->line, node->column, op ? op : "?");
}

#if 0
static void compile_block_decl_stmt(ASTNode *node, Context *ctx) {
   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   parse_dump_node(node);

   if (has_modifier(node->children[0], "static")) {
      ASTNode *type = node->children[1];
      const char *name = node->children[2]->strval;
      ctx_static(ctx, type, name, node->children[5] ? false : true);
   }
   else if (has_modifier(node->children[0], "quick")) {
      ASTNode *type = node->children[1];
      const char *name = node->children[2]->strval;
      ctx_quick(ctx, type, name, node->children[5] ? false : true);
   }
   else {
      ASTNode *type = node->children[1];
      const char *name = node->children[2]->strval;
      ctx_push(ctx, type, name);

      if (node->children[5]) {
         // TODO FIX evaluate and assign
      }
   }

   error("%s:%d %s exiting", __FILE__, __LINE__,  __FUNCTION__);
}
#endif

static void compile_statement_list(ASTNode *node, Context *ctx) {
   if (!node || is_empty(node)) {
      return;
   }

   for (int i = 0; i < node->count; i++) {
      ASTNode *stmt = node->children[i];
      if (!strcmp(stmt->name, "return_stmt")) {
         compile_return_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "expr") || !strcmp(stmt->name, "assign_expr")) {
         compile_expr(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "defdecl_stmt")) {
         ASTNode *list = stmt->children[0];
         for (int j = 0; j < list->count; j++) {
            compile_local_decl_item(list->children[j], ctx);
         }
      }
      else if (!strcmp(stmt->name, "if_stmt")) {
         compile_if_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "while_stmt")) {
         compile_while_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "for_stmt")) {
         compile_for_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "break_stmt")) {
         compile_break_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "continue_stmt")) {
         compile_continue_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "do_stmt")) {
         compile_do_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "label_stmt")) {
         compile_label_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "goto_stmt")) {
         compile_goto_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "switch_stmt")) {
         compile_switch_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "statement_list")) {
         compile_statement_list(stmt, ctx);
      }
      else {
         compile_expr(stmt, ctx);
      }
   }
}

static void compile_function_decl(ASTNode *node) {
   ASTNode *modifiers  = node->children[0]->children[0];
   ASTNode *declarator = node->children[1];
   ASTNode *body       = node->children[2];
   const char *name    = declarator->children[1]->strval;

   remember_function(node, name);

   if (!has_modifier(modifiers, "static")) {
      emit(&es_export, ".export _%s\n", name);
   }

   Context ctx;
   ctx.name = name;
   ctx.locals = 0;
   ctx.params = 0;
   ctx.vars = new_set();
   ctx.break_label = NULL;
   ctx.continue_label = NULL;
   build_function_context(node, &ctx);

   if (!is_empty(body) && !strcmp(body->name, "statement_list")) {
      predeclare_statement_list(body, &ctx);
   }

   emit(&es_code, ".proc _%s\n", name);
   emit(&es_code, "    lda sp+1\n");
   emit(&es_code, "    sta fp+1\n");
   emit(&es_code, "    lda sp\n");
   emit(&es_code, "    sta fp\n");
   if (ctx.locals > 0) {
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", ctx.locals & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
   }

   if (!is_empty(body)) {
      if (!strcmp(body->name, "statement_list")) {
         compile_statement_list(body, &ctx);
      }
      else {
         warning("[%s:%d.%d] function body node '%s' not compiled yet", body->file, body->line, body->column, body->name);
      }
   }

   emit(&es_code, "@fini:\n");
   if (ctx.locals > 0) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", ctx.locals & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
   }
   emit(&es_code, "    rts\n");
   emit(&es_code, ".endproc\n");
}

static void compile_mem_decl_stmt(ASTNode *node) {
   attach_memname(node->children[0]->strval, node);
}

static void compile_type_decl_stmt(ASTNode *node) {
   const char *key = node->children[0]->strval;
   attach_typename(key, node);

   //debug("%s:%s", __FUNCTION__, node->children[0]->strval);
   bool haveSize = false;
   int size = -1;
   bool haveEndian = false;
   const char *endian = NULL;

   // we need to guarantee a "size" and "endian"
   if (strcmp(node->children[1]->name, "empty")) {
      for (int i = 0; i < node->children[1]->count; i++) {
         ASTNode *item = node->children[1]->children[i];

         // check for $size, must be nonnegative
         if (!strncmp(item->strval, "$size:", 6)) {
            if (haveSize) {
               error("[%s:%d.%d] type_decl_stmt '%s' has multiple '$size:' flags",
                     node->file, node->line, node->column,
                     node->children[0]->strval);
            }
            char *p = strchr(item->strval, ':');
            p++;
            size = atoi(p);
            if (size < 0 || (size == 0 && strcmp(p, "0"))) {
               error("[%s:%d.%d] type_decl_stmt '%s' unrecognized '$size:%s' flag",
                     node->file, node->line, node->column,
                     node->children[0]->strval, p);
            }
            haveSize = true;
            pair_insert(typesizes, key, (void *)(intptr_t) size);
         }

         // check for $endian, must be "big" or "little"
         if (!strncmp(item->strval, "$endian:", 8)) {
            if (haveEndian) {
               error("[%s:%d.%d] type_decl_stmt '%s' has multiple '$endian:' flags",
                     node->file, node->line, node->column,
                     node->children[0]->strval);
            }
            endian = strchr(item->strval, ':');
            endian++;
            if (strcmp(endian, "big") && strcmp(endian, "little")) {
               error("[%s:%d.%d] type_decl_stmt '%s' unrecognized '$endian:%s' flag",
                     node->file, node->line, node->column,
                     node->children[0]->strval, endian);
            }

            haveEndian = true;
         }
      }
   }

   if (!haveSize) {
      error("[%s:%d.%d] type_decl_stmt '%s' missing '$size:' flag",
            node->file, node->line, node->column, node->children[0]->strval);
   }

   if (!haveEndian && size > 1) {
      error("[%s:%d.%d] type_decl_stmt '%s' missing '$endian:' flag",
            node->file, node->line, node->column, node->children[0]->strval);
   }

   if (get_xray(XRAY_TYPEINFO)) {
      message("TYPEINFO: %s %d %s", key, haveSize ? size : -1, haveEndian ? endian : "unspec");
   }
}

static void compile_struct_decl_stmt(ASTNode *node) {
   const char *key = node->children[0]->strval;
   attach_typename(key, node);

   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   debug("========================================\n");
   parse_dump_node(node);
   debug("========================================\n");
}

static void compile_union_decl_stmt(ASTNode *node) {
   const char *key = node->children[0]->strval;
   attach_typename(key, node);

   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   debug("========================================\n");
   parse_dump_node(node);
   debug("========================================\n");
}

static bool declarator_is_function(const ASTNode *declarator) {
   return declarator && declarator->count >= 3 &&
          !strcmp(declarator->children[2]->name, "parameter_list");
}

static int declarator_array_multiplier(const ASTNode *declarator) {
   int mult = 1;

   if (!declarator_is_function(declarator)) {
      for (int i = 2; i < declarator->count; i++) {
         mult *= atoi(declarator->children[i]->strval);
      }
   }

   return mult;
}

static int declarator_storage_size(const ASTNode *type, const ASTNode *declarator) {
   int size;

   if (atoi(declarator->children[0]->strval) > 0) {
      size = get_size("*");
   }
   else {
      size = get_size(type_name_from_node(type));
   }

   return size * declarator_array_multiplier(declarator);
}

static void compile_global_decl_item(ASTNode *node) {
   ASTNode *modifiers  = node->children[0];
   ASTNode *type       = node->children[1];
   ASTNode *declarator = node->children[2];
   const char *name    = declarator->children[1]->strval;
   ASTNode *expression = node->children[node->count - 1];

   if (!globals) {
      globals = new_set();
   }

   const ASTNode *value = set_get(globals, name);
   if (value != NULL) {
      error("[%s:%d.%d] duplicate symbol '%s' first defined at [%s:%d.%d]",
            node->file, node->line, node->column,
            name,
            value->file, value->line, value->column);
   }
   set_add(globals, strdup(name), node);

   bool is_extern = has_modifier(modifiers, "extern");
   bool is_const  = has_modifier(modifiers, "const");
   bool is_static = has_modifier(modifiers, "static");
   bool is_quick  = has_modifier(modifiers, "quick");
   bool is_ref    = has_modifier(modifiers, "ref");

   if (is_ref) {
      error("[%s:%d.%d] 'ref' not allowed in global declaration",
            node->file, node->line, node->column);
   }

   int size = declarator_storage_size(type, declarator);

   if (is_extern) {
      if (is_static) {
         error("[%s:%d.%d] 'extern' and 'static' don't mix",
               node->file, node->line, node->column);
      }

      if (is_quick) {
         emit(&es_import, ".zpimport _%s\n", name);
      }
      else {
         emit(&es_import, ".import _%s\n", name);
      }
      return;
   }

   if (!is_static) {
      if (is_quick) {
         emit(&es_export, ".zpexport _%s\n", name);
      }
      else {
         emit(&es_export, ".export _%s\n", name);
      }
   }

   if (is_empty(expression)) {
      if (is_const) {
         error("[%s:%d.%d] 'const' missing initializer",
               node->file, node->line, node->column);
      }
      if (is_quick) {
         emit(&es_zp, "_%s:\n", name);
         emit(&es_zp, "\t.res %d\n", size);
      }
      else {
         emit(&es_bss, "_%s:\n", name);
         emit(&es_bss, "\t.res %d\n", size);
      }
      return;
   }

   EmitSink *es = is_const ? &es_rodata : (is_quick ? &es_zpdata : &es_data);
   emit(es, "_%s:\n", name);

   if (expression->kind == AST_INTEGER) {
      unsigned char *bytes = (unsigned char *) calloc(size ? size : 1, sizeof(unsigned char));
      if (has_flag(type_name_from_node(type), "$endian:big")) {
         make_be_int(expression->strval, bytes, size);
      }
      else {
         make_le_int(expression->strval, bytes, size);
      }
      emit(es, "	.byte $%02x", bytes[0]);
      for (int i = 1; i < size; i++) {
         emit(es, ", $%02x", bytes[i]);
      }
      emit(es, "\n");
      free(bytes);
   }
   else {
      unsigned char *bytes = (unsigned char *) calloc(size ? size : 1, sizeof(unsigned char));
      if (bytes && build_initializer_bytes(bytes, size, 0, expression, type, declarator, size)) {
         emit(es, "	.byte $%02x", bytes[0]);
         for (int i = 1; i < size; i++) {
            emit(es, ", $%02x", bytes[i]);
         }
         emit(es, "\n");
      }
      else {
         warning("[%s:%d.%d] complex global initializer for '%s' not implemented yet",
               node->file, node->line, node->column, name);
         emit(es, "	.res %d, $00\n", size);
      }
      free(bytes);
   }
}

static void remember_function(const ASTNode *node, const char *name) {
   if (!functions) {
      functions = new_set();
   }

   const ASTNode *value = set_get(functions, name);
   if (value != NULL) {
      if (value->count >= 4 && node->count >= 4) {
         error("[%s:%d.%d] vs [%s:%d.%d] multiple definitions for '%s'",
               node->file, node->line, node->column,
               value->file, value->line, value->column,
               name);
      }
      return;
   }

   set_add(functions, strdup(name), (void *) node);
}

static void predeclare_top_level_functions(ASTNode *program) {
   if (!functions) {
      functions = new_set();
   }

   for (int i = 0; i < program->count; i++) {
      ASTNode *node = program->children[i];
      if (strcmp(node->name, "defdecl_stmt")) {
         continue;
      }

      if (node->count == 1 && !strcmp(node->children[0]->name, "decl_list")) {
         ASTNode *list = node->children[0];
         for (int j = 0; j < list->count; j++) {
            ASTNode *item = list->children[j];
            ASTNode *declarator = item->children[2];
            if (declarator_is_function(declarator)) {
               remember_function(item, declarator->children[1]->strval);
            }
         }
      }
      else if (node->count == 3) {
         ASTNode *declarator = node->children[1];
         remember_function(node, declarator->children[1]->strval);
      }
   }
}

static void compile_function_signature(ASTNode *node) {
   ASTNode *modifiers  = node->children[0];
   ASTNode *declarator = node->children[2];
   const char *name    = declarator->children[1]->strval;

   remember_function(node, name);

   if (has_modifier(modifiers, "extern") && !has_modifier(modifiers, "static")) {
      remember_symbol_import(name);
   }
}


static void compile_defdecl_stmt(ASTNode *node) {
   if (node->count == 1 && !strcmp(node->children[0]->name, "decl_list")) {
      ASTNode *list = node->children[0];
      for (int i = 0; i < list->count; i++) {
         ASTNode *item = list->children[i];
         ASTNode *declarator = item->children[2];
         if (declarator_is_function(declarator)) {
            compile_function_signature(item);
         }
         else {
            compile_global_decl_item(item);
         }
      }
      return;
   }

   if (node->count == 3) {
      compile_function_decl(node);
      return;
   }

   error("[%s:%d.%d] unsupported defdecl_stmt shape", node->file, node->line, node->column);
}

static void check_struct_union_undefined(ASTNode *program) {
   // undefined struct/union is always an error
   const char *undefined = typename_find_null();
   if (undefined) {
      ASTNode *node = NULL;

      // as an artifact of parsing,
      // floaters have an empty node
      // in the program tree
      for (int i = 0; i < program->count; i++) {
         if (!strcmp(program->children[i]->name, "empty")) {
            if (!strcmp(program->children[i]->strval, undefined)) {
               node = program->children[i];
            }
         }
      }

      if (node) {
         error("undefined struct/union '%s' [%s:%d.%d]",
               undefined, node->file, node->line, node->column);
      }
      else {
         error("undefined struct/union '%s'", undefined); // this is probably unreachable
      }
      // error() calls exit()
   }
}

static bool crosscheck_helper(Pair *markers, const char *name) {
   const char *childname;
   ASTNode *child;
   pair_insert(markers, name, (void *)1);
   ASTNode *node = get_typename_node(name);
   if (strcmp(node->name, "type_decl_stmt")) {
      for (int i = 1; i < node->count; i++) {
         child = node->children[i];
         if (!strcmp(child->children[2]->children[0]->strval, "0")) {
            childname = child->children[1]->strval;
            void *color = pair_get(markers, childname);
            if (color == 0) {
               if (crosscheck_helper(markers, childname)) {
                  goto problem;
               }
            }
            else if ((intptr_t)color == 1) {
               goto problem;   
            }
         }
      }
   }
   pair_insert(markers, name, (void *) 2);
   return false;

problem:
   warning("struct/union '%s' contains '%s' [%s:%d.%d]",
         name, childname,
         child->file, child->line, child->column);
   return true;
}

static void crosscheck_struct_union_nesting(ASTNode *program) {
   Pair *markers = pair_create();

   for (int i = 0; i < program->count; i++) {
      if (!strcmp(program->children[i]->name, "struct_decl_stmt") ||
          !strcmp(program->children[i]->name, "union_decl_stmt")) {
         ASTNode *node = program->children[i]->children[0];
         pair_insert(markers, node->strval, 0);
      }
   }

   for (int i = 0; i < program->count; i++) {
      if (!strcmp(program->children[i]->name, "struct_decl_stmt") ||
          !strcmp(program->children[i]->name, "union_decl_stmt")) {
         ASTNode *node = program->children[i]->children[0];
         if (pair_get(markers, node->strval) == 0) {
            if (crosscheck_helper(markers, node->strval)) {
               error("cyclic struct/union detected");
               // error() calls exit()
            }
         }
      }
   }

   pair_destroy(markers);
}

static void calculate_struct_union_sizes(ASTNode *program) {
   // everybody uses pointers, let's just do that now...

   if (!typename_exists("*")) {
      error("type * is not defined, pointer size is unknown");
      // error() calls exit()
   }

   int sizeof_ptr = (intptr_t) pair_get(typesizes, "*");

   bool done = false;

   while (!done) {
      done = true;

      for (int i = 0; i < program->count; i++) {
         bool is_struct = false;
         bool is_union = false;

         if (!strcmp(program->children[i]->name, "struct_decl_stmt")) {
            is_struct = true;
         }
         else if (!strcmp(program->children[i]->name, "union_decl_stmt")) {
            is_union = true;
         }
         // else if (!strcmp(program->children[i]->name, "type_decl_stmt")) {
         // // types have already been done.
         // }

         if (is_struct || is_union) {
            ASTNode *node = program->children[i];
            const char *name = node->children[0]->strval;
            int size = 0;

            if (!pair_exists(typesizes, name)) {
               // we need it
               int othersize;

               for (int i = 1; i < node->count; i++) {
                  ASTNode *item = node->children[i];
                  const char *tname = item->children[1]->strval;
                  int isptr = atoi(item->children[2]->children[0]->strval);
                  int mult = 1;

                  // arrays get multipliers
                  for (int j = 2; j < item->children[2]->count; j++) {
                     mult *= atoi(item->children[2]->children[j]->strval);
                  }

                  if (isptr) {
                     othersize = sizeof_ptr;
                  }
                  else {
                     if (pair_exists(typesizes, tname)) {
                        othersize = (intptr_t) pair_get(typesizes, tname);
                     }
                     else {
                        othersize = -1;
                     }
                  }

                  if (othersize == -1) {
                     size = -1;
                     break;
                  }
                  else if (is_struct) {
                     size += othersize * mult;
                  }
                  else if (is_union) {
                     if (othersize * mult > size) {
                        size = othersize * mult;
                     }
                  }
               }

               if (size == -1) {
                  done = false;
               }
               else {
                  pair_insert(typesizes, name, (void *)(intptr_t)size);
                  warning("sizeof(%s) == %d", name, size);
               }
            }
         }
      }
   }
}

static void compile(ASTNode *program) {

   if (!program) {
      error("internal NULL program node");
      // error calls exit()
   }

   if (strcmp(program->name, "program")) {
      error("internal non program node '%s' [%s:%d.%d]",
            program->name,
            program->file, program->line, program->column);
      // error calls exit()
   }

   for (int i = 0; i < program->count; i++) {
      ASTNode *node = program->children[i];
      if (!strcmp(node->name, "include_stmt")) {
         node->handled = true;
         // ignore these, they're handled in the parser
      }
      else if (!strcmp(node->name, "xform_decl_stmt")) {
         node->handled = true;
         // literally nothing to do here, parser.y has it covered.
      }
      else if (!strcmp(node->name, "empty")) {
         node->handled = true;
         // literally nothing to do here, parser.y has it covered.
      }
   }

   for (int i = 0; i < program->count; i++) {
      ASTNode *node = program->children[i];
      if (!strcmp(node->name, "mem_decl_stmt")) {
         node->handled = true;
         compile_mem_decl_stmt(node);
      }
   }

   for (int i = 0; i < program->count; i++) {
      ASTNode *node = program->children[i];
      if (!strcmp(node->name, "type_decl_stmt")) {
         node->handled = true;
         compile_type_decl_stmt(node);
      }
   }

   for (int i = 0; i < program->count; i++) {
      ASTNode *node = program->children[i];
      if (!strcmp(node->name, "struct_decl_stmt")) {
         node->handled = true;
         compile_struct_decl_stmt(node);
      }
      else if (!strcmp(node->name, "union_decl_stmt")) {
         node->handled = true;
         compile_union_decl_stmt(node);
      }
   }

   check_struct_union_undefined(program);
   crosscheck_struct_union_nesting(program);
   calculate_struct_union_sizes(program);
   predeclare_top_level_functions(program);

   for (int i = 0; i < program->count; i++) {
      ASTNode *node = program->children[i];
      if (!strcmp(node->name, "defdecl_stmt")) {
         node->handled = true;
         compile_defdecl_stmt(node);
      }
   }

   for (int i = 0; i < program->count; i++) {
      ASTNode *node = program->children[i];
      if (!node->handled) {
         error("[%s:%d.%d] unrecognized AST node '%s'",
               node->file, node->line, node->column,
               node->name);
         // error calls exit()
      }
   }
}

void do_compile(void) {

   typesizes = pair_create();

   emit(&es_header, "; this file produced by \"nc\" compiler\n");
   emit(&es_header, "; depends on --feature dollar_in_identifiers\n");
   emit(&es_header, ".include \"nlib.inc\"\n");
   emit(&es_code,   ".segment \"CODE\"\n");
   emit(&es_rodata, ".segment \"RODATA\"\n");
   emit(&es_data,   ".segment \"DATA\"\n");
   emit(&es_bss,    ".segment \"BSS\"\n");
   emit(&es_import, "; imports\n");
   emit(&es_export, "; exports\n");

   compile(root);

   emit_print(&es_header);
   printf("\n");

   emit_print(&es_import);
   printf("\n");

   emit_print(&es_export);
   printf("\n");

   emit_print(&es_bss);
   printf("\n");

   emit_print(&es_data);
   printf("\n");

   emit_print(&es_rodata);
   printf("\n");

   emit_print(&es_code);
}
