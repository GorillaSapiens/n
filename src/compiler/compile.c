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

typedef struct ContextEntry {
   const ASTNode *type;
   bool is_static;
   bool is_quick;
   int offset;
   int size;
} ContextEntry;

typedef struct Context {
   const char *name;
   int locals;
   int params;
   Set *vars;
   const char *break_label;
   const char *continue_label;
} Context;

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

static ContextEntry *ctx_lookup(Context *ctx, const char *name) {
   return ctx ? (ContextEntry *) set_get(ctx->vars, name) : NULL;
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
   const ASTNode *node = get_typename_node(type);
   if (!node || node->count < 2 || is_empty(node->children[1])) {
      error("[%s:%d] internal could not find '%s'", __FILE__, __LINE__, type);
   }

   const ASTNode *flags = node->children[1];
   for (int i = 0; i < flags->count; i++) {
      if (!strncmp(flags->children[i]->strval, "$size:", 6)) {
         return atoi(flags->children[i]->strval + 6);
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
   entry->is_static = false;
   entry->is_quick = false;
   entry->type = type;
   entry->size = get_size(type->strval);
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
   entry->is_static = false;
   entry->is_quick = false;
   entry->type = type;
   entry->size = get_size(type->strval);
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
   entry->is_static = true;
   entry->is_quick = false;
   entry->type = type;
   entry->size = get_size(type->strval);
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
   entry->is_static = false;
   entry->is_quick = true;
   entry->type = type;
   entry->size = get_size(type->strval);
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
         }
         else if (has_modifier((ASTNode *) decl_specs->children[0], "quick")) {
            ctx_quick(ctx, type, name, true);
            ((ContextEntry *) set_get(ctx->vars, name))->size = size;
         }
         else {
            ctx_shove(ctx, type, name);
            ((ContextEntry *) set_get(ctx->vars, name))->size = size;
            ((ContextEntry *) set_get(ctx->vars, name))->offset = ctx->params + get_size(type->strval) - size;
            ctx->params -= (size - get_size(type->strval));
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

static ContextEntry *ctx_lookup_lvalue(Context *ctx, ASTNode *node) {
   if (!node || strcmp(node->name, "lvalue") || node->count == 0) {
      return NULL;
   }

   ASTNode *base = node->children[0];
   if (!base || strcmp(base->name, "lvalue_base") || base->count == 0) {
      return NULL;
   }

   if (base->children[0]->kind != AST_IDENTIFIER) {
      return NULL;
   }

   return ctx_lookup(ctx, base->children[0]->strval);
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
   }

   if (!strcmp(expr->name, "lvalue") && expr->count > 0) {
      ContextEntry *entry = ctx_lookup_lvalue(ctx, expr);
      if (entry && !entry->is_static && !entry->is_quick) {
         emit_copy_fp_to_fp(dst->offset, entry->offset, entry->size < dst->size ? entry->size : dst->size);
         return true;
      }
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
      if (!compile_expr_to_slot(expr->children[0], ctx, dst)) {
         return false;
      }

      if (rhs && rhs->kind == AST_INTEGER) {
         unsigned char *bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
         if (has_flag(type_name_from_node(dst->type), "$endian:big")) {
            make_be_int(rhs->strval, bytes, dst->size);
         }
         else {
            make_le_int(rhs->strval, bytes, dst->size);
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

      if (rhs && rhs->kind == AST_IDENTIFIER) {
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

      if (rhs && !strcmp(rhs->name, "lvalue") && rhs->count > 0) {
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
   }

   if (!strcmp(expr->name, "lvalue") && expr->count > 0) {
      ContextEntry *entry = ctx_lookup_lvalue(ctx, expr);
      if (entry) {
         return entry->type;
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

   if (expr->count >= 1) {
      return expr_value_type(expr->children[0], ctx);
   }

   return get_typename_node("int");
}

static int type_size_from_node(const ASTNode *type) {
   if (!type) {
      return get_size("int");
   }
   if (type->strval) {
      return get_size(type->strval);
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
      lhs = (ContextEntry){ type, false, false, ctx->locals, size };
      rhs = (ContextEntry){ type, false, false, ctx->locals + size, size };
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
      tmp = (ContextEntry){ type, false, false, ctx->locals, size };

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
   ASTNode *cond = node->children[0];
   ASTNode *body = node->children[1];

   if (!start_label || !end_label) {
      free((void *) start_label);
      free((void *) end_label);
      warning("[%s:%d.%d] while label generation failed", node->file, node->line, node->column);
      return;
   }

   push_loop_labels(end_label, start_label);
   emit(&es_code, "%s:\n", start_label);
   if (!compile_condition_branch_false(cond, ctx, end_label)) {
      warning("[%s:%d.%d] while condition not compiled yet", node->file, node->line, node->column);
      pop_loop_labels();
      free((void *) start_label);
      free((void *) end_label);
      return;
   }
   compile_statement_list(body, ctx);
   emit(&es_code, "    jmp %s\n", start_label);
   emit(&es_code, "%s:\n", end_label);
   pop_loop_labels();
   free((void *) start_label);
   free((void *) end_label);
}

static void compile_for_stmt(ASTNode *node, Context *ctx) {
   const char *start_label = next_label("for_start");
   const char *step_label = next_label("for_step");
   const char *end_label = next_label("for_end");
   ASTNode *init = node->children[0];
   ASTNode *cond = node->children[1];
   ASTNode *step = node->children[2];
   ASTNode *body = node->children[3];

   if (!start_label || !step_label || !end_label) {
      free((void *) start_label);
      free((void *) step_label);
      free((void *) end_label);
      warning("[%s:%d.%d] for label generation failed", node->file, node->line, node->column);
      return;
   }

   push_loop_labels(end_label, step_label);
   if (init && !is_empty(init)) {
      compile_expr(init, ctx);
   }

   emit(&es_code, "%s:\n", start_label);
   if (cond && !is_empty(cond)) {
      if (!compile_condition_branch_false(cond, ctx, end_label)) {
         warning("[%s:%d.%d] for condition not compiled yet", node->file, node->line, node->column);
         pop_loop_labels();
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
   free((void *) start_label);
   free((void *) step_label);
   free((void *) end_label);
}

static bool compile_expr_to_return_slot(ASTNode *expr, Context *ctx, ContextEntry *ret) {
   return compile_expr_to_slot(expr, ctx, ret);
}

static void compile_break_stmt(ASTNode *node, Context *ctx) {
   const char *target = current_break_label();

   if (!target) {
      warning("[%s:%d.%d] break used outside loop not compiled", node->file, node->line, node->column);
      return;
   }
   if (node->count > 0 && node->children[0] && !is_empty(node->children[0])) {
      warning("[%s:%d.%d] labeled break not compiled yet", node->file, node->line, node->column);
      return;
   }

   emit(&es_code, "    jmp %s\n", target);
}

static void compile_continue_stmt(ASTNode *node, Context *ctx) {
   const char *target = current_continue_label();

   if (!target) {
      warning("[%s:%d.%d] continue used outside loop not compiled", node->file, node->line, node->column);
      return;
   }
   if (node->count > 0 && node->children[0] && !is_empty(node->children[0])) {
      warning("[%s:%d.%d] labeled continue not compiled yet", node->file, node->line, node->column);
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
   }
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
   }

   while (expression && expression->count == 1 && !strcmp(expression->name, "assign_expr")) {
      expression = expression->children[0];
   }

   if (!is_empty(expression) && !entry->is_static && !entry->is_quick) {
      if (!compile_expr_to_slot(expression, ctx, entry)) {
         warning("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
      }
   }
   else if (!is_empty(expression)) {
      warning("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
   }
}


static void compile_do_stmt(ASTNode *node, Context *ctx) {
   const char *start_label = next_label("do_start");
   const char *end_label = next_label("do_end");
   if (!start_label || !end_label) {
      free((void *) start_label);
      free((void *) end_label);
      warning("[%s:%d.%d] failed to allocate labels for do statement", node->file, node->line, node->column);
      return;
   }

   emit(&es_code, "%s:\n", start_label);
   push_loop_labels(end_label, start_label);
   compile_statement_list(node->children[0], ctx);
   pop_loop_labels();
   if (!compile_condition_branch_false(node->children[1], ctx, end_label)) {
      warning("[%s:%d.%d] do/while condition not compiled yet", node->file, node->line, node->column);
   }
   emit(&es_code, "    jmp %s\n", start_label);
   emit(&es_code, "%s:\n", end_label);

   free((void *) start_label);
   free((void *) end_label);
}

static void compile_label_stmt(ASTNode *node, Context *ctx) {
   (void) ctx;
   emit(&es_code, "@user_%s:\n", node->children[0]->strval);
   if (node->count > 1) {
      ASTNode *stmt = node->children[1];
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
   }
}

static void compile_goto_stmt(ASTNode *node, Context *ctx) {
   (void) ctx;
   if (node->count > 0 && !is_empty(node->children[0])) {
      emit(&es_code, "    jmp @user_%s\n", node->children[0]->strval);
   }
}

static void compile_switch_stmt(ASTNode *node, Context *ctx) {
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
   lhs = (ContextEntry){ type, false, false, ctx->locals, size };
   rhs = (ContextEntry){ type, false, false, ctx->locals + size, size };
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
      if (dst->is_static || dst->is_quick) {
         warning("[%s:%d.%d] increment/decrement of static/quick '%s' not compiled yet", node->file, node->line, node->column,
                 node->children[0]->children[0]->strval);
         return;
      }
      one = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
      if (!one) {
         return;
      }
      one[0] = 1;
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

   if (!node || strcmp(node->name, "assign_expr") || node->count != 3) {
      warning("[%s:%d.%d] expression not compiled yet", node->file, node->line, node->column);
      return;
   }

   ContextEntry *dst = ctx_lookup_lvalue(ctx, node->children[1]);
   const char *op = node->children[0] ? node->children[0]->strval : NULL;
   ASTNode *rhs = node->children[2];
   if (!dst) {
      warning("[%s:%d.%d] assignment target not compiled yet", node->file, node->line, node->column);
      return;
   }

   if (dst->is_static || dst->is_quick) {
      warning("[%s:%d.%d] assignment to static/quick '%s' not compiled yet", node->file, node->line, node->column,
              node->children[1]->children[0]->children[0]->strval);
      return;
   }

   if (!op || !strcmp(op, ":=")) {
      if (!compile_expr_to_slot(rhs, ctx, dst)) {
         warning("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
      }
      return;
   }

   rhs = (ASTNode *) unwrap_expr_node(rhs);
   if (!rhs) {
      warning("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
      return;
   }

   if (!strcmp(op, "+=") || !strcmp(op, "-=")) {
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
            emit_add_immediate_to_fp(dst->type, dst->offset, bytes, dst->size);
         }
         else {
            emit_sub_immediate_from_fp(dst->type, dst->offset, bytes, dst->size);
         }
         free(bytes);
         return;
      }

      if (rhs->kind == AST_IDENTIFIER) {
         ContextEntry *src = ctx_lookup(ctx, rhs->strval);
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
      else {
         warning("[%s:%d.%d] statement '%s' not compiled yet", stmt->file, stmt->line, stmt->column, stmt->name);
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
      size = get_size(type->strval);
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
      emit(es, "\t.byte $%02x", bytes[0]);
      for (int i = 1; i < size; i++) {
         emit(es, ", $%02x", bytes[i]);
      }
      emit(es, "\n");
      free(bytes);
   }
   else {
      warning("[%s:%d.%d] complex global initializer for '%s' not implemented yet",
            node->file, node->line, node->column, name);
      emit(es, "\t.res %d, $00\n", size);
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
