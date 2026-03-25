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
} Context;

static void remember_function(const ASTNode *node, const char *name);
static void predeclare_top_level_functions(ASTNode *program);
static int declarator_storage_size(const ASTNode *type, const ASTNode *declarator);
static bool compile_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst);
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
           !strcmp(expr->name, "initializer"))) {
      expr = expr->children[0];
   }
   return expr;
}

static int expr_byte_index(const ASTNode *type, int size, int i) {
   if (has_flag(type->strval, "$endian:big")) {
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
         ret_size = get_size(ret_type->strval);
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
      if (has_flag(dst->type->strval, "$endian:big")) {
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

   if (expr->count == 2 && (!strcmp(expr->name, "+") || !strcmp(expr->name, "-"))) {
      const ASTNode *rhs = unwrap_expr_node(expr->children[1]);
      if (!compile_expr_to_slot(expr->children[0], ctx, dst)) {
         return false;
      }

      if (rhs && rhs->kind == AST_INTEGER) {
         unsigned char *bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
         if (has_flag(dst->type->strval, "$endian:big")) {
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

static bool compile_expr_to_return_slot(ASTNode *expr, Context *ctx, ContextEntry *ret) {
   return compile_expr_to_slot(expr, ctx, ret);
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

   if (!node || strcmp(node->name, "assign_expr") || node->count != 3 || strcmp(node->children[0]->strval, ":=")) {
      warning("[%s:%d.%d] expression not compiled yet", node->file, node->line, node->column);
      return;
   }

   ContextEntry *dst = ctx_lookup_lvalue(ctx, node->children[1]);
   if (!dst) {
      warning("[%s:%d.%d] assignment target not compiled yet", node->file, node->line, node->column);
      return;
   }

   if (dst->is_static || dst->is_quick) {
      warning("[%s:%d.%d] assignment to static/quick '%s' not compiled yet", node->file, node->line, node->column,
              node->children[1]->children[0]->children[0]->strval);
      return;
   }

   if (!compile_expr_to_slot(node->children[2], ctx, dst)) {
      warning("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
   }
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
      if (has_flag(type->strval, "$endian:big")) {
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
