#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

#include "ast.h"
#include "compile.h"
#include "emit.h"
#include "float.h"
#include "integer.h"
#include "memname.h"
#include "messages.h"
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

Set *globals = NULL;
Set *functions = NULL;

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

#if 0

#if 0
// for parameterless flags (e.g. "$signed")
// also for compleate flags (e.g. "$endian:little")
static bool has_flag(const char *type, const char *flag) {
   const ASTNode *node = set_get(types, type);
   for (const ASTNode *list = node->children[1];
         list != NULL;
         list = list->children[1]) {
      if (!strcmp(list->children[0]->strval, flag)) {
         return true;
      }
   }
   return false;
}
#endif

#if 0
static bool has_modifier(ASTNode *node, const char *modifier) {
   if (node && node->kind != AST_EMPTY) {
      while (node) {
         if (!strcmp(modifier, node->children[0]->strval)) {
            return true;
         }
         node = node->children[1];
      }
   }
   return false;
}

static int get_size(const char *type) {
   const ASTNode *node = set_get(types, type);
   for (const ASTNode *list = node->children[1];
         list != NULL;
         list = list->children[1]) {
      if (!strncmp(list->children[0]->strval, "$size:", 6)) {
         return atoi(list->children[0]->strval + 6);
      }
   }
   error("[%s:%d] internal could not find '%s'", __FILE__, __LINE__, type);
   return -1; // unreachable
}
#endif

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

#if 0
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
#endif

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

#if 0
static void build_function_context(const ASTNode *node, Context *ctx) {
   int i = 0;

   // first the arguments, in order
   for (ASTNode *arg = node->children[3]; arg; arg = arg->children[1]) {
      ASTNode *type = arg->children[0]->children[1];
      const char *name = arg->children[0]->children[2] ? arg->children[0]->children[2]->strval : missing_argname(i);
      if (has_modifier(arg->children[0]->children[0], "static")) {
         ctx_static(ctx, type, name, true);
      }
      else if (has_modifier(arg->children[0]->children[0], "quick")) {
         ctx_quick(ctx, type, name, true);
      }
      else {
         ctx_shove(ctx, type, name);
      }
      i++;
   }

   // then the return value
   ctx_shove(ctx, node->children[1], "$$");
}
#endif

#if 0
static void compile_return_stmt(ASTNode *node, Context *ctx) {
   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   parse_dump_node(node);

   debug("TODO FIX");
}
#endif

#if 0
static void compile_expr(ASTNode *node, Context *ctx) {
   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   parse_dump_node(node);

   error("%s:%d %s exiting", __FILE__, __LINE__,  __FUNCTION__);
}
#endif

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

#if 0
static void compile_statement_list(ASTNode *node, Context *ctx) {
   while(node) {
      if (!strcmp(node->children[0]->name, "return_stmt")) {
         compile_return_stmt(node->children[0], ctx);
      }
      else if (!strcmp(node->children[0]->name, "expr")) {
         compile_expr(node->children[0], ctx);
      }
      else if (!strcmp(node->children[0]->name, "block_decl_stmt")) {
         compile_block_decl_stmt(node->children[0], ctx);
      }
      else {
         parse_dump_node(node);
         error("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
      }
      node = node->children[1];
   }
}
#endif

#if 0
static void compile_function_decl(ASTNode *node) {
   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   parse_dump_node(node);

   const char *name = node->children[2]->strval;

   if (!functions) {
      functions = new_set();
   }
   const ASTNode *value = set_get(functions, name);
   if (value == NULL) {
      set_add(functions, name, node);
   }
   else {
      if (!function_prototype_match(node, value)) {
         error("[%s:%d.%d] vs [%s:%d.%d] function prototype mismatch for '%s'",
            node->file, node->line, node->column,
            value->file, value->line, value->column,
            name);
      }
   }

   if (!node->children[4]) {
      // just a declaration, no body
      return;
   }

   // we have a body!
   if (value && value->children[4]) {
      error("[%s:%d.%d] vs [%s:%d.%d] multiple definitions for '%s'",
         node->file, node->line, node->column,
         value->file, value->line, value->column,
         name);
   }
   else {
      set_rm(functions, name);
      set_add(functions, name, node);
   }

   // build the context
   Context ctx;
   ctx.name = name;
   ctx.locals = 0;
   ctx.params = 0;
   ctx.vars = new_set();
   build_function_context(node, &ctx);

   emit(&es_code, ".proc _%s\n", name);

   // prologue

   // ;push frame pointer onto HARDWARE stack
   emit(&es_code, "    lda fp+1\n");
   emit(&es_code, "    pha\n");
   emit(&es_code, "    lda fp\n");
   emit(&es_code, "    pha\n");
   emit(&es_code, "\n");

   // ;transfer stack pointer to frame pointer
   emit(&es_code, "    lda sp+1\n");
   emit(&es_code, "    sta fp+1\n");
   emit(&es_code, "    lda sp\n");
   emit(&es_code, "    sta sp\n");
   emit(&es_code, "\n");

   // body

   if (!strcmp(node->children[4]->name, "statement_list")) {
      compile_statement_list(node->children[4], &ctx);
   }
   else {
      if (node->children[4]->kind != AST_EMPTY) {
         error("[%s:%d.%d] unknown body node type '%s'",
               node->children[4]->file, node->children[4]->line, node->children[4]->column,
               node->children[4]->name);
      }
   }

   // epilogue

   // ;pop frame pointer
   emit(&es_code, "@fini:\n");
   emit(&es_code, "    pla\n");
   emit(&es_code, "    sta fp\n");
   emit(&es_code, "    pla\n");
   emit(&es_code, "    sta fp+1\n");
   emit(&es_code, "\n");

   // ;pop stack pointer
   emit(&es_code, "    pla\n");
   emit(&es_code, "    sta sp\n");
   emit(&es_code, "    pla\n");
   emit(&es_code, "    sta sp+1\n");
   emit(&es_code, "    rts\n");

   emit(&es_code, ".endproc\n");

   return;
}
#endif

#endif

////////////////////////////////////////

static void compile_mem_decl_stmt(ASTNode *node) {
   // TODO FIX sanity check the flags!
   attach_memname(node->children[0]->strval, node);
}

// check type_decl_stmt for existence of $size and $endian
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
   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   debug("========================================\n");
   parse_dump_node(node);
   debug("========================================\n");
}

static void compile_union_decl_stmt(ASTNode *node) {
   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   debug("========================================\n");
   parse_dump_node(node);
   debug("========================================\n");
}

static void compile_defdecl_stmt(ASTNode *node) {
   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   debug("========================================\n");
   parse_dump_node(node);
   debug("========================================\n");
}

static void compile(ASTNode *node) {
   if (!node) {
      return;
   }

   if (!strcmp(node->name, "program")) {
      for (int i = 0; i < node->count; i++) {
         compile(node->children[i]);
      }
   }
   else if (!strcmp(node->name, "include_stmt")) {
      // ignore these, they're handled in the parser
   }
   else if (!strcmp(node->name, "xform_decl_stmt")) {
      // literally nothing to do here, parser.y has it covered.
   }
   else if (!strcmp(node->name, "mem_decl_stmt")) {
      compile_mem_decl_stmt(node);
   }
   else if (!strcmp(node->name, "type_decl_stmt")) {
      compile_type_decl_stmt(node);
   }
   else if (!strcmp(node->name, "struct_decl_stmt")) {
      compile_struct_decl_stmt(node);
   }
   else if (!strcmp(node->name, "union_decl_stmt")) {
      compile_union_decl_stmt(node);
   }
   else if (!strcmp(node->name, "defdecl_stmt")) {
      compile_defdecl_stmt(node);
   }
   else {
      error("[%s:%d.%d] unrecognized AST node '%s'",
         node->file, node->line, node->column,
         node->name);
   }
}

void do_compile(void) {

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
