#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>

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
Set *string_literals = NULL;
static int label_counter = 0;
static const char *loop_break_stack[128];
static const char *loop_continue_stack[128];
static int loop_depth = 0;
static const char *named_loop_names[128];
static const char *named_loop_break_stack[128];
static const char *named_loop_continue_stack[128];
static int named_loop_depth = 0;
static const char *pending_loop_label_name = NULL;

typedef struct OperatorOverload {
   const char *name;
   const ASTNode *node;
} OperatorOverload;

static OperatorOverload *operator_overloads = NULL;
static int operator_overload_count = 0;

typedef struct ContextEntry {
   const char *name;
   const ASTNode *type;
   const ASTNode *declarator;
   bool is_static;
   bool is_zeropage;
   bool is_global;
   bool is_ref;
   int offset;
   int size;
} ContextEntry;

typedef struct LValueRef {
   const char *name;
   const ASTNode *type;
   const ASTNode *declarator;
   bool is_static;
   bool is_zeropage;
   bool is_global;
   bool is_ref;
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

typedef enum InitConstKind {
   INIT_CONST_NONE = 0,
   INIT_CONST_INT,
   INIT_CONST_FLOAT,
   INIT_CONST_ADDRESS
} InitConstKind;

typedef struct InitConstValue {
   InitConstKind kind;
   long long i;
   double f;
   const char *symbol;
   long long addend;
} InitConstValue;

static const ASTNode *global_decl_lookup(const char *name);
static bool entry_symbol_name(Context *ctx, const ContextEntry *entry, char *buf, size_t bufsize);
static const char *type_name_from_node(const ASTNode *type);
static const ASTNode *required_typename_node(const char *name);
static const ASTNode *bool_type_node(void);
static bool type_is_bool(const ASTNode *type);
static bool type_is_signed_integer(const ASTNode *type);
static bool type_is_unsigned_integer(const ASTNode *type);
static bool type_is_promotable_integer(const ASTNode *type);
static const char *type_endian_name(const ASTNode *type);
static const ASTNode *promoted_integer_type_for_binary(const ASTNode *lhs_type, const ASTNode *rhs_type, ASTNode *origin);
static const ASTNode *literal_annotation_type(const ASTNode *expr);
static int type_size_from_node(const ASTNode *type);
static const char *find_mem_modifier_name(const ASTNode *modifiers);
static const ASTNode *find_mem_modifier_node(const ASTNode *modifiers);
static bool mem_decl_is_zeropage(const ASTNode *mem_decl);
static bool modifiers_imply_zeropage(const ASTNode *modifiers);
static int integer_literal_min_size(const ASTNode *expr);
static bool has_flag(const char *type, const char *flag);
static void emit_copy_fp_to_fp(int dst_offset, int src_offset, int size);

static void remember_function(const ASTNode *node, const char *name);
static bool is_operator_function_name(const char *name);
static bool function_symbol_name(const ASTNode *fn, const char *fallback_name, char *buf, size_t bufsize);
static ASTNode *make_synthetic_call_expr(ASTNode *origin, const char *callee_name, ASTNode *args[], int argc);
static ASTNode *make_synthetic_incdec_operand(ASTNode *origin);
static bool classify_incdec_lvalue_expr(ASTNode *expr, bool *inc, bool *pre);
static const ASTNode *resolve_function_call_target(const char *name, ASTNode *args, Context *ctx);
static const ASTNode *resolve_operator_overload_expr(ASTNode *expr, Context *ctx);
static const ASTNode *resolve_incdec_overload_expr(ASTNode *expr, Context *ctx);
static const ASTNode *resolve_truthiness_overload(ASTNode *expr, Context *ctx);
static const ASTNode *parameter_type(const ASTNode *parameter);
static const ASTNode *parameter_declarator(const ASTNode *parameter);
static bool parameter_is_ref(const ASTNode *parameter);
static int parameter_storage_size(const ASTNode *parameter);
static bool parameter_is_void(const ASTNode *parameter);
static bool resolve_ref_argument_lvalue(Context *ctx, ASTNode *expr, LValueRef *out);
static const ASTNode *unwrap_expr_node(const ASTNode *expr);
static void predeclare_top_level_functions(ASTNode *program);
static int declarator_storage_size(const ASTNode *type, const ASTNode *declarator);
static int declarator_value_size(const ASTNode *type, const ASTNode *declarator);
static int declarator_pointer_depth(const ASTNode *declarator);
static int declarator_array_count(const ASTNode *declarator);
static int declarator_first_element_size(const ASTNode *type, const ASTNode *declarator);
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
static bool eval_constant_initializer_expr(ASTNode *expr, InitConstValue *out);
static bool encode_integer_initializer_value(long long value, unsigned char *buf, int size, const ASTNode *type);
static bool encode_float_initializer_value(double value, unsigned char *buf, int size, const ASTNode *type);
static bool emit_symbol_address_initializer(EmitSink *es, int size, const ASTNode *type, const char *symbol, long long addend);
static bool emit_global_initializer(EmitSink *es, const ASTNode *type, const ASTNode *declarator, ASTNode *expression, int size);
static const ASTNode *expr_value_type(ASTNode *expr, Context *ctx);
static int expr_value_size(ASTNode *expr, Context *ctx);
static const ASTNode *expr_value_declarator(ASTNode *expr, Context *ctx);
static void emit_prepare_fp_ptr(int ptrno, int offset);
static int expr_byte_index(const ASTNode *type, int size, int i);
static void emit_fill_fp_bytes(int dst_offset, int start, int count, unsigned char value);
static void emit_copy_fp_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, int src_offset, int src_size, const ASTNode *src_type);
static void emit_copy_symbol_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, const char *symbol, int src_size, const ASTNode *src_type);
static unsigned char hex_value(unsigned char c) {
   if (c >= '0' && c <= '9') return (unsigned char) (c - '0');
   if (c >= 'a' && c <= 'f') return (unsigned char) (10 + c - 'a');
   if (c >= 'A' && c <= 'F') return (unsigned char) (10 + c - 'A');
   return 0xff;
}

static unsigned char *decode_string_literal_bytes(const char *text, int *out_len) {
   size_t raw_len;
   unsigned char *buf;
   int j = 0;

   if (!text) {
      text = "";
   }
   raw_len = strlen(text);
   buf = (unsigned char *) malloc(raw_len + 1);
   if (!buf) {
      error("out of memory");
   }

   for (size_t i = 0; i < raw_len; i++) {
      unsigned char c = (unsigned char) text[i];
      if (c != '\\' || i + 1 >= raw_len) {
         buf[j++] = c;
         continue;
      }

      c = (unsigned char) text[++i];
      switch (c) {
         case 'a': buf[j++] = 0x07; break;
         case 'b': buf[j++] = 0x08; break;
         case 'e': buf[j++] = 0x1b; break;
         case 'f': buf[j++] = 0x0c; break;
         case 'n': buf[j++] = 0x0a; break;
         case 'r': buf[j++] = 0x0d; break;
         case 't': buf[j++] = 0x09; break;
         case 'v': buf[j++] = 0x0b; break;
         case '\\': buf[j++] = '\\'; break;
         case '\'': buf[j++] = '\''; break;
         case '"': buf[j++] = '"'; break;
         case '?': buf[j++] = '?'; break;
         case '\n':
            break;
         case 'x': {
            unsigned char v1 = 0xff;
            unsigned char v2 = 0xff;
            if (i + 1 < raw_len) v1 = hex_value((unsigned char) text[i + 1]);
            if (i + 2 < raw_len) v2 = hex_value((unsigned char) text[i + 2]);
            if (v1 != 0xff) {
               i++;
               if (v2 != 0xff) {
                  i++;
                  buf[j++] = (unsigned char) ((v1 << 4) | v2);
               }
               else {
                  buf[j++] = v1;
               }
            }
            else {
               buf[j++] = 'x';
            }
            break;
         }
         case '0': case '1': case '2': case '3':
         case '4': case '5': case '6': case '7': {
            unsigned int value = (unsigned int) (c - '0');
            int digits = 1;
            while (digits < 3 && i + 1 < raw_len && text[i + 1] >= '0' && text[i + 1] <= '7') {
               value = (value << 3) | (unsigned int) (text[++i] - '0');
               digits++;
            }
            buf[j++] = (unsigned char) value;
            break;
         }
         default:
            buf[j++] = c;
            break;
      }
   }

   if (out_len) {
      *out_len = j;
   }
   return buf;
}

static const char *remember_string_literal(const char *text);
static void emit_store_label_address_to_fp(int dst_offset, int dst_size, const char *label);
static bool emit_string_initializer_to_fp(const ASTNode *type, const ASTNode *declarator, int base_offset, int total_size, const char *text);
static bool emit_string_initializer_bytes(unsigned char *buf, int buf_size, int base_offset, const ASTNode *type, const ASTNode *declarator, int total_size, const char *text);

static ContextEntry *ctx_lookup(Context *ctx, const char *name) {
   return ctx ? (ContextEntry *) set_get(ctx->vars, name) : NULL;
}



static bool is_operator_function_name(const char *name) {
   return name && !strncmp(name, "operator", 8);
}

static int function_fixed_param_count(const ASTNode *fn) {
   const ASTNode *declarator = function_declarator_node(fn);
   const ASTNode *params = (declarator && declarator->count > 2) ? declarator->children[2] : NULL;
   int count = 0;

   if (params && !is_empty(params)) {
      for (int i = 0; i < params->count; i++) {
         const ASTNode *parameter = params->children[i];
         if (!parameter || parameter_is_void(parameter)) {
            continue;
         }
         if (parameter_type(parameter)) {
            count++;
         }
      }
   }

   return count;
}

static bool declarator_signature_matches(const ASTNode *actual, const ASTNode *formal) {
   if (declarator_pointer_depth(actual) != declarator_pointer_depth(formal)) {
      return false;
   }
   if (declarator_array_count(actual) != declarator_array_count(formal)) {
      return false;
   }
   return true;
}

static int function_signature_match_score(const ASTNode *fn, int arg_count, const ASTNode **arg_types, const ASTNode **arg_decls, const bool *arg_lvalues) {
   const ASTNode *declarator = function_declarator_node(fn);
   const ASTNode *params = (declarator && declarator->count > 2) ? declarator->children[2] : NULL;
   int seen = 0;
   int score = 0;

   if (!declarator) {
      return -1;
   }

   if (function_fixed_param_count(fn) != arg_count) {
      return -1;
   }

   if (params && !is_empty(params)) {
      for (int i = 0; i < params->count; i++) {
         const ASTNode *parameter = params->children[i];
         const ASTNode *ptype;
         const ASTNode *pdecl;
         const char *pname;
         const char *aname;
         bool pref;

         if (!parameter || parameter_is_void(parameter)) {
            continue;
         }

         ptype = parameter_type(parameter);
         pdecl = parameter_declarator(parameter);
         pref = parameter_is_ref(parameter);
         if (!ptype || seen >= arg_count || !arg_types[seen]) {
            return -1;
         }

         pname = type_name_from_node(ptype);
         aname = type_name_from_node(arg_types[seen]);
         if (!pname || !aname || strcmp(pname, aname)) {
            return -1;
         }
         if (!declarator_signature_matches(arg_decls[seen], pdecl)) {
            return -1;
         }
         if (pref) {
            if (!arg_lvalues || !arg_lvalues[seen]) {
               return -1;
            }
            score++;
         }
         seen++;
      }
   }

   return seen == arg_count ? score : -1;
}


static bool function_same_signature(const ASTNode *a, const ASTNode *b) {
   if (!a || !b) {
      return false;
   }
   if (function_fixed_param_count(a) != function_fixed_param_count(b)) {
      return false;
   }

   {
      const ASTNode *adecl = function_declarator_node(a);
      const ASTNode *bdecl = function_declarator_node(b);
      const ASTNode *aparams = (adecl && adecl->count > 2) ? adecl->children[2] : NULL;
      const ASTNode *bparams = (bdecl && bdecl->count > 2) ? bdecl->children[2] : NULL;
      int ai = 0;
      int bi = 0;

      while ((aparams && !is_empty(aparams) && ai < aparams->count) ||
             (bparams && !is_empty(bparams) && bi < bparams->count)) {
         const ASTNode *aparam = NULL;
         const ASTNode *bparam = NULL;
         while (aparams && !is_empty(aparams) && ai < aparams->count) {
            aparam = aparams->children[ai++];
            if (aparam && !parameter_is_void(aparam) && parameter_type(aparam)) {
               break;
            }
            aparam = NULL;
         }
         while (bparams && !is_empty(bparams) && bi < bparams->count) {
            bparam = bparams->children[bi++];
            if (bparam && !parameter_is_void(bparam) && parameter_type(bparam)) {
               break;
            }
            bparam = NULL;
         }
         if (!aparam && !bparam) {
            break;
         }
         if (!aparam || !bparam) {
            return false;
         }
         if (strcmp(type_name_from_node(parameter_type(aparam)), type_name_from_node(parameter_type(bparam)))) {
            return false;
         }
         if (parameter_is_ref(aparam) != parameter_is_ref(bparam)) {
            return false;
         }
         if (!declarator_signature_matches(parameter_declarator(aparam), parameter_declarator(bparam))) {
            return false;
         }
      }
   }

   return true;
}

static void remember_operator_overload(const ASTNode *node, const char *name) {
   for (int i = 0; i < operator_overload_count; i++) {
      const ASTNode *value = operator_overloads[i].node;
      if (strcmp(operator_overloads[i].name, name)) {
         continue;
      }
      if (!function_same_signature(value, node)) {
         continue;
      }
      if (value->count >= 4 && node->count >= 4) {
         error("[%s:%d.%d] vs [%s:%d.%d] multiple definitions for '%s'",
               node->file, node->line, node->column,
               value->file, value->line, value->column,
               name);
      }
      if (value->count < 4 && node->count >= 4) {
         operator_overloads[i].node = node;
      }
      return;
   }

   operator_overloads = (OperatorOverload *) realloc(operator_overloads,
         sizeof(*operator_overloads) * (operator_overload_count + 1));
   if (!operator_overloads) {
      error("out of memory");
   }
   operator_overloads[operator_overload_count].name = strdup(name);
   operator_overloads[operator_overload_count].node = node;
   operator_overload_count++;
}

static void append_mangled_text(char *buf, size_t bufsize, const char *text) {
   size_t len = strlen(buf);
   if (!text) {
      return;
   }
   for (size_t i = 0; text[i] && len + 1 < bufsize; i++) {
      unsigned char c = (unsigned char) text[i];
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9')) {
         buf[len++] = (char) c;
      }
      else if (len + 3 < bufsize) {
         sprintf(buf + len, "x%02X", c);
         len += 3;
      }
      else {
         break;
      }
   }
   buf[len] = 0;
}

static bool function_symbol_name(const ASTNode *fn, const char *fallback_name, char *buf, size_t bufsize) {
   const ASTNode *declarator = function_declarator_node(fn);
   const char *name = fallback_name;

   if (!buf || bufsize == 0) {
      return false;
   }
   buf[0] = 0;

   if (!name && declarator && declarator->count > 1 && declarator->children[1]) {
      name = declarator->children[1]->strval;
   }
   if (!name) {
      return false;
   }

   if (!is_operator_function_name(name)) {
      snprintf(buf, bufsize, "%s", name);
      return true;
   }

   append_mangled_text(buf, bufsize, name);
   if (!fn) {
      return true;
   }

   {
      const ASTNode *params = (declarator && declarator->count > 2) ? declarator->children[2] : NULL;
      if (params && !is_empty(params)) {
         for (int i = 0; i < params->count; i++) {
            const ASTNode *parameter = params->children[i];
            const ASTNode *ptype;
            const ASTNode *pdecl;
            char tmp[64];
            if (!parameter || parameter_is_void(parameter)) {
               continue;
            }
            ptype = parameter_type(parameter);
            pdecl = parameter_declarator(parameter);
            strncat(buf, "__", bufsize - strlen(buf) - 1);
            append_mangled_text(buf, bufsize, type_name_from_node(ptype));
            if (parameter_is_ref(parameter)) {
               strncat(buf, "_r1", bufsize - strlen(buf) - 1);
            }
            snprintf(tmp, sizeof(tmp), "_p%d_a%d", declarator_pointer_depth(pdecl), declarator_array_count(pdecl));
            strncat(buf, tmp, bufsize - strlen(buf) - 1);
         }
      }
      else {
         strncat(buf, "__void", bufsize - strlen(buf) - 1);
      }
   }

   return true;
}

static ASTNode *make_synthetic_call_expr(ASTNode *origin, const char *callee_name, ASTNode *args[], int argc) {
   ASTNode *call;
   ASTNode *arglist;

   if (!origin || !callee_name) {
      return NULL;
   }

   call = calloc(1, sizeof(ASTNode) + sizeof(ASTNode *) * 2);
   arglist = calloc(1, sizeof(ASTNode) + sizeof(ASTNode *) * (argc > 0 ? argc : 1));
   if (!call || !arglist) {
      free(call);
      free(arglist);
      return NULL;
   }

   call->name = "()";
   call->file = origin->file;
   call->line = origin->line;
   call->column = origin->column;
   call->handled = false;
   call->kind = AST_GENERIC;
   call->count = 2;
   call->children[0] = make_identifier_leaf(callee_name);
   call->children[0]->file = origin->file;
   call->children[0]->line = origin->line;
   call->children[0]->column = origin->column;

   arglist->name = "expr_args";
   arglist->file = origin->file;
   arglist->line = origin->line;
   arglist->column = origin->column;
   arglist->handled = false;
   arglist->kind = argc > 0 ? AST_GENERIC : AST_EMPTY;
   arglist->count = argc;
   for (int i = 0; i < argc; i++) {
      arglist->children[i] = args[i];
   }
   call->children[1] = arglist;
   return call;
}

static ASTNode *make_synthetic_incdec_operand(ASTNode *origin) {
   ASTNode *operand;

   if (!origin || strcmp(origin->name, "lvalue") || origin->count < 2) {
      return NULL;
   }

   operand = calloc(1, sizeof(ASTNode) + sizeof(ASTNode *) * 2);
   if (!operand) {
      return NULL;
   }

   operand->name = origin->name;
   operand->file = origin->file;
   operand->line = origin->line;
   operand->column = origin->column;
   operand->handled = false;
   operand->kind = origin->kind;
   operand->count = 2;
   operand->children[0] = origin->children[0];
   operand->children[1] = origin->children[1];
   return operand;
}

static bool classify_incdec_lvalue_expr(ASTNode *expr, bool *inc, bool *pre) {
   const char *op;

   expr = (ASTNode *) unwrap_expr_node(expr);
   if (!expr || strcmp(expr->name, "lvalue") || expr->count < 3 || !expr->children[2] || expr->children[2]->kind != AST_IDENTIFIER) {
      return false;
   }

   op = expr->children[2]->strval;
   if (!op) {
      return false;
   }

   if (!strcmp(op, "pre++")) {
      if (inc) *inc = true;
      if (pre) *pre = true;
      return true;
   }
   if (!strcmp(op, "post++")) {
      if (inc) *inc = true;
      if (pre) *pre = false;
      return true;
   }
   if (!strcmp(op, "pre--")) {
      if (inc) *inc = false;
      if (pre) *pre = true;
      return true;
   }
   if (!strcmp(op, "post--")) {
      if (inc) *inc = false;
      if (pre) *pre = false;
      return true;
   }
   return false;
}

static const ASTNode *lookup_operator_overload(const char *name, int arg_count, const ASTNode **arg_types, const ASTNode **arg_decls, const bool *arg_lvalues) {
   const ASTNode *best = NULL;
   int best_score = -1;

   for (int i = 0; i < operator_overload_count; i++) {
      int score;
      if (strcmp(operator_overloads[i].name, name)) {
         continue;
      }
      score = function_signature_match_score(operator_overloads[i].node, arg_count, arg_types, arg_decls, arg_lvalues);
      if (score < 0) {
         continue;
      }
      if (!best || score > best_score) {
         best = operator_overloads[i].node;
         best_score = score;
      }
   }
   return best;
}

static const ASTNode *resolve_function_call_target(const char *name, ASTNode *args, Context *ctx) {
   if (is_operator_function_name(name)) {
      int arg_count = (args && !is_empty(args)) ? args->count : 0;
      const ASTNode *arg_types[8];
      const ASTNode *arg_decls[8];
      bool arg_lvalues[8];
      if (arg_count > (int) (sizeof(arg_types) / sizeof(arg_types[0]))) {
         return NULL;
      }
      for (int i = 0; i < arg_count; i++) {
         arg_types[i] = expr_value_type(args->children[i], ctx);
         arg_decls[i] = expr_value_declarator(args->children[i], ctx);
         arg_lvalues[i] = resolve_ref_argument_lvalue(ctx, args->children[i], NULL);
      }
      return lookup_operator_overload(name, arg_count, arg_types, arg_decls, arg_lvalues);
   }

   if (functions) {
      return (const ASTNode *) set_get(functions, name);
   }
   return NULL;
}

static const ASTNode *resolve_operator_overload_expr(ASTNode *expr, Context *ctx) {
   const char *op = NULL;
   const char *name = NULL;
   const ASTNode *arg_types[2];
   const ASTNode *arg_decls[2];
   bool arg_lvalues[2];
   int arg_count;
   char buf[64];

   expr = (ASTNode *) unwrap_expr_node(expr);
   if (!expr || is_empty(expr)) {
      return NULL;
   }

   if (expr->count == 1 && (!strcmp(expr->name, "+") || !strcmp(expr->name, "-") || !strcmp(expr->name, "~"))) {
      op = expr->name;
   }
   else if (expr->count == 2 && (!strcmp(expr->name, "+") || !strcmp(expr->name, "-") || !strcmp(expr->name, "*") ||
             !strcmp(expr->name, "/") || !strcmp(expr->name, "%") || !strcmp(expr->name, "&") ||
             !strcmp(expr->name, "|") || !strcmp(expr->name, "^") || !strcmp(expr->name, "<<") ||
             !strcmp(expr->name, ">>") || !strcmp(expr->name, "==") || !strcmp(expr->name, "!=") ||
             !strcmp(expr->name, "<") || !strcmp(expr->name, ">") || !strcmp(expr->name, "<=") ||
             !strcmp(expr->name, ">="))) {
      op = expr->name;
   }
   else {
      return NULL;
   }

   snprintf(buf, sizeof(buf), "operator%s", op);
   name = buf;
   arg_count = expr->count;
   for (int i = 0; i < arg_count; i++) {
      arg_types[i] = expr_value_type(expr->children[i], ctx);
      arg_decls[i] = expr_value_declarator(expr->children[i], ctx);
      arg_lvalues[i] = resolve_ref_argument_lvalue(ctx, expr->children[i], NULL);
      if (!arg_types[i]) {
         return NULL;
      }
   }
   return lookup_operator_overload(name, arg_count, arg_types, arg_decls, arg_lvalues);
}

static const ASTNode *resolve_incdec_overload_expr(ASTNode *expr, Context *ctx) {
   bool inc;
   const ASTNode *arg_type;
   const ASTNode *arg_decl;
   bool arg_lvalue;

   expr = (ASTNode *) unwrap_expr_node(expr);
   if (!classify_incdec_lvalue_expr(expr, &inc, NULL)) {
      return NULL;
   }

   arg_type = expr_value_type(expr, ctx);
   if (!arg_type) {
      return NULL;
   }
   arg_decl = expr_value_declarator(expr, ctx);
   arg_lvalue = resolve_ref_argument_lvalue(ctx, expr, NULL);
   return lookup_operator_overload(inc ? "operator++" : "operator--", 1, &arg_type, &arg_decl, &arg_lvalue);
}

static const ASTNode *resolve_truthiness_overload(ASTNode *expr, Context *ctx) {
   const ASTNode *arg_type;
   const ASTNode *arg_decl;
   bool arg_lvalue;
   expr = (ASTNode *) unwrap_expr_node(expr);
   if (!expr || is_empty(expr)) {
      return NULL;
   }
   arg_type = expr_value_type(expr, ctx);
   if (!arg_type) {
      return NULL;
   }
   arg_decl = expr_value_declarator(expr, ctx);
   arg_lvalue = resolve_ref_argument_lvalue(ctx, expr, NULL);
   return lookup_operator_overload("operator{}", 1, &arg_type, &arg_decl, &arg_lvalue);
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
   if (entry->is_static || entry->is_zeropage) {
      snprintf(buf, bufsize, "_%s$%s", ctx && ctx->name ? ctx->name : "", entry->name);
      return true;
   }
   return false;
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

static const char *remember_string_literal(const char *text) {
   const char *existing;
   char *label;
   unsigned char *bytes;
   int n = 0;

   if (!text) {
      text = "";
   }
   if (!string_literals) {
      string_literals = new_set();
   }
   existing = (const char *) set_get(string_literals, text);
   if (existing) {
      return existing;
   }
   label = (char *) malloc(64);
   if (!label) {
      error("out of memory");
   }
   snprintf(label, 64, "@str_%d", label_counter++);
   set_add(string_literals, strdup(text), label);
   emit(&es_rodata, "%s:\n", label);

   bytes = decode_string_literal_bytes(text, &n);
   if (n <= 0) {
      emit(&es_rodata, "\t.byte $00\n");
   }
   else {
      emit(&es_rodata, "\t.byte $%02x", (unsigned int) bytes[0]);
      for (int i = 1; i < n; i++) {
         emit(&es_rodata, ", $%02x", (unsigned int) bytes[i]);
      }
      emit(&es_rodata, ", $00\n");
   }
   free(bytes);
   return label;
}

static void emit_store_label_address_to_fp(int dst_offset, int dst_size, const char *label) {
   if (!label || dst_size <= 0) {
      return;
   }
   emit(&es_code, "    lda #<%s\n", label);
   emit(&es_code, "    ldy #%d\n", dst_offset);
   emit(&es_code, "    sta (fp),y\n");
   if (dst_size > 1) {
      emit(&es_code, "    lda #>%s\n", label);
      emit(&es_code, "    ldy #%d\n", dst_offset + 1);
      emit(&es_code, "    sta (fp),y\n");
   }
   if (dst_size > 2) {
      emit_fill_fp_bytes(dst_offset, 2, dst_size - 2, 0);
   }
}

static bool emit_string_initializer_to_fp(const ASTNode *type, const ASTNode *declarator, int base_offset, int total_size, const char *text) {
   int elem_count, elem_size, copy_len;
   (void) total_size;
   if (!text) {
      text = "";
   }
   if (declarator && declarator_array_count(declarator) > 0 && declarator_pointer_depth(declarator) == 0) {
      elem_count = atoi(declarator->children[2]->strval);
      elem_size = declarator_first_element_size(type, declarator);
      if (elem_size != 1 || elem_count <= 0) {
         return false;
      }
      copy_len = (int) strlen(text) + 1;
      if (copy_len > elem_count) {
         copy_len = elem_count;
      }
      for (int i = 0; i < copy_len; i++) {
         unsigned char b = (unsigned char) (i < (int) strlen(text) ? text[i] : 0);
         emit(&es_code, "    lda #$%02x\n", (unsigned int) b);
         emit(&es_code, "    ldy #%d\n", base_offset + i);
         emit(&es_code, "    sta (fp),y\n");
      }
      return true;
   }
   if (declarator_pointer_depth(declarator) > 0 || (type && !strcmp(type_name_from_node(type), "*"))) {
      const char *label = remember_string_literal(text);
      emit_store_label_address_to_fp(base_offset, total_size > 0 ? total_size : declarator_storage_size(type, declarator), label);
      return true;
   }
   return false;
}

static bool emit_string_initializer_bytes(unsigned char *buf, int buf_size, int base_offset, const ASTNode *type, const ASTNode *declarator, int total_size, const char *text) {
   int elem_count, elem_size, copy_len, bytes_len = 0;
   unsigned char *bytes;
   (void) total_size;
   if (!buf || !text) {
      return false;
   }
   bytes = decode_string_literal_bytes(text, &bytes_len);
   if (declarator && declarator_array_count(declarator) > 0 && declarator_pointer_depth(declarator) == 0) {
      elem_count = atoi(declarator->children[2]->strval);
      elem_size = declarator_first_element_size(type, declarator);
      if (elem_size != 1 || elem_count <= 0) {
         free(bytes);
         return false;
      }
      copy_len = bytes_len + 1;
      if (copy_len > elem_count) {
         copy_len = elem_count;
      }
      if (base_offset < 0 || base_offset + elem_count > buf_size) {
         free(bytes);
         return false;
      }
      if (copy_len > 1) {
         memcpy(buf + base_offset, bytes, (size_t) (copy_len - 1));
      }
      if (copy_len > 0) {
         buf[base_offset + copy_len - 1] = 0;
      }
      free(bytes);
      return true;
   }
   free(bytes);
   return false;
}

static void emit_fill_fp_bytes(int dst_offset, int start, int count, unsigned char value) {
   bool direct;
   if (count <= 0) {
      return;
   }
   direct = dst_offset >= 0 && dst_offset + start + count <= 256;
   if (!direct) {
      emit_prepare_fp_ptr(0, dst_offset + start);
   }
   for (int i = 0; i < count; i++) {
      emit(&es_code, "    ldy #%d\n", direct ? (dst_offset + start + i) : i);
      emit(&es_code, "    lda #$%02x\n", value);
      emit(&es_code, "    sta %s,y\n", direct ? "(fp)" : "(ptr0)");
   }
}

static void emit_copy_fp_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, int src_offset, int src_size, const ASTNode *src_type) {
   bool big_endian = src_type && has_flag(type_name_from_node(src_type), "$endian:big");
   bool is_signed = src_type && has_flag(type_name_from_node(src_type), "$signed");
   int copy_size;
   int dst_copy_start;
   int src_copy_start;

   if (dst_size <= 0 || src_size <= 0) {
      return;
   }

   copy_size = dst_size < src_size ? dst_size : src_size;
   dst_copy_start = big_endian && dst_size > copy_size ? dst_size - copy_size : 0;
   src_copy_start = big_endian && src_size > copy_size ? src_size - copy_size : 0;
   emit_copy_fp_to_fp(dst_offset + dst_copy_start, src_offset + src_copy_start, copy_size);

   if (dst_size > copy_size) {
      if (big_endian) {
         if (is_signed) {
            int sign_src = src_offset + src_copy_start;
            bool dst_direct = dst_offset >= 0 && dst_offset + (dst_size - copy_size) <= 256;
            bool src_direct = sign_src >= 0 && sign_src < 256;
            if (!src_direct) {
               emit_prepare_fp_ptr(0, sign_src);
            }
            if (!dst_direct) {
               emit_prepare_fp_ptr(1, dst_offset);
            }
            emit(&es_code, "    ldy #%d\n", src_direct ? sign_src : 0);
            emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr0)");
            emit(&es_code, "    and #$80\n");
            emit(&es_code, "    beq :+\n");
            emit(&es_code, "    lda #$ff\n");
            emit(&es_code, ":\n");
            for (int i = 0; i < dst_size - copy_size; i++) {
               emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + i) : i);
               emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
            }
         }
         else {
            emit_fill_fp_bytes(dst_offset, 0, dst_size - copy_size, 0x00);
         }
      }
      else {
         if (is_signed) {
            int sign_src = src_offset + copy_size - 1;
            bool dst_direct = dst_offset >= 0 && dst_offset + dst_size <= 256;
            bool src_direct = sign_src >= 0 && sign_src < 256;
            if (!src_direct) {
               emit_prepare_fp_ptr(0, sign_src);
            }
            if (!dst_direct) {
               emit_prepare_fp_ptr(1, dst_offset);
            }
            emit(&es_code, "    ldy #%d\n", src_direct ? sign_src : 0);
            emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr0)");
            emit(&es_code, "    and #$80\n");
            emit(&es_code, "    beq :+\n");
            emit(&es_code, "    lda #$ff\n");
            emit(&es_code, ":\n");
            for (int i = copy_size; i < dst_size; i++) {
               emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + i) : i);
               emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
            }
         }
         else {
            emit_fill_fp_bytes(dst_offset, copy_size, dst_size - copy_size, 0x00);
         }
      }
   }
}

static void emit_copy_symbol_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, const char *symbol, int src_size, const ASTNode *src_type) {
   int copy_size;
   int dst_copy_start;
   int src_copy_start;
   bool big_endian = src_type && has_flag(type_name_from_node(src_type), "$endian:big");
   bool is_signed = src_type && has_flag(type_name_from_node(src_type), "$signed");
   bool dst_direct;
   if (dst_size <= 0 || src_size <= 0) {
      return;
   }
   copy_size = dst_size < src_size ? dst_size : src_size;
   dst_copy_start = big_endian && dst_size > copy_size ? dst_size - copy_size : 0;
   src_copy_start = big_endian && src_size > copy_size ? src_size - copy_size : 0;
   dst_direct = dst_offset >= 0 && dst_offset + dst_size <= 256;
   if (!dst_direct) {
      emit_prepare_fp_ptr(1, dst_offset);
   }
   for (int i = 0; i < copy_size; i++) {
      emit(&es_code, "    ldy #%d\n", src_copy_start + i);
      emit(&es_code, "    lda %s,y\n", symbol);
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + dst_copy_start + i) : (dst_copy_start + i));
      emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
   }
   if (dst_size > copy_size) {
      if (big_endian) {
         if (is_signed) {
            emit(&es_code, "    ldy #%d\n", src_copy_start);
            emit(&es_code, "    lda %s,y\n", symbol);
            emit(&es_code, "    and #$80\n");
            emit(&es_code, "    beq :+\n");
            emit(&es_code, "    lda #$ff\n");
            emit(&es_code, ":\n");
            for (int i = 0; i < dst_size - copy_size; i++) {
               emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + i) : i);
               emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
            }
         }
         else {
            emit_fill_fp_bytes(dst_offset, 0, dst_size - copy_size, 0x00);
         }
      }
      else {
         if (is_signed) {
            emit(&es_code, "    ldy #%d\n", src_copy_start + copy_size - 1);
            emit(&es_code, "    lda %s,y\n", symbol);
            emit(&es_code, "    and #$80\n");
            emit(&es_code, "    beq :+\n");
            emit(&es_code, "    lda #$ff\n");
            emit(&es_code, ":\n");
            for (int i = copy_size; i < dst_size; i++) {
               emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + i) : i);
               emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
            }
         }
         else {
            emit_fill_fp_bytes(dst_offset, copy_size, dst_size - copy_size, 0x00);
         }
      }
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
      return NULL;
   }
   if (type->strval) {
      return type->strval;
   }
   if (type->count > 0 && type->children[0] && type->children[0]->strval) {
      return type->children[0]->strval;
   }
   return NULL;
}

static const ASTNode *required_typename_node(const char *name) {
   const ASTNode *node;

   if (!name) {
      error("[%s:%d] internal missing required type name", __FILE__, __LINE__);
   }

   node = get_typename_node(name);
   if (!node) {
      error("type %s is not defined", name);
   }

   return node;
}

static const ASTNode *bool_type_node(void) {
   return required_typename_node("bool");
}

static bool type_is_bool(const ASTNode *type) {
   const char *name = type_name_from_node(type);
   return name && !strcmp(name, "bool");
}

static bool type_is_signed_integer(const ASTNode *type) {
   const char *name = type_name_from_node(type);
   return name && strcmp(name, "*") && has_flag(name, "$signed");
}

static bool type_is_unsigned_integer(const ASTNode *type) {
   const char *name = type_name_from_node(type);
   return name && strcmp(name, "*") && (has_flag(name, "$unsigned") || type_is_bool(type));
}

static bool type_is_promotable_integer(const ASTNode *type) {
   return type_is_signed_integer(type) || type_is_unsigned_integer(type);
}

static const char *type_endian_name(const ASTNode *type) {
   const char *name = type_name_from_node(type);
   if (!name) {
      return NULL;
   }
   if (has_flag(name, "$endian:big")) {
      return "big";
   }
   if (has_flag(name, "$endian:little")) {
      return "little";
   }
   return NULL;
}

static const ASTNode *promoted_integer_type_for_binary(const ASTNode *lhs_type, const ASTNode *rhs_type, ASTNode *origin) {
   bool lhs_signed;
   bool rhs_signed;
   int lhs_size;
   int rhs_size;
   int required_size;
   bool require_signed;
   const char *preferred_endian = NULL;
   const ASTNode *best = NULL;
   int best_size = INT_MAX;
   int best_penalty = INT_MAX;

   if (!type_is_promotable_integer(lhs_type) || !type_is_promotable_integer(rhs_type)) {
      return NULL;
   }

   lhs_signed = type_is_signed_integer(lhs_type);
   rhs_signed = type_is_signed_integer(rhs_type);
   lhs_size = type_size_from_node(lhs_type);
   rhs_size = type_size_from_node(rhs_type);
   if (lhs_size <= 0 || rhs_size <= 0) {
      return NULL;
   }

   if (lhs_signed == rhs_signed) {
      require_signed = lhs_signed;
      required_size = lhs_size > rhs_size ? lhs_size : rhs_size;
   }
   else {
      int signed_size = lhs_signed ? lhs_size : rhs_size;
      int unsigned_size = lhs_signed ? rhs_size : lhs_size;
      require_signed = true;
      required_size = signed_size > (unsigned_size + 1) ? signed_size : (unsigned_size + 1);
   }

   if (lhs_size >= rhs_size) {
      preferred_endian = type_endian_name(lhs_type);
   }
   else {
      preferred_endian = type_endian_name(rhs_type);
   }
   if (!preferred_endian) {
      preferred_endian = type_endian_name(lhs_type);
   }
   if (!preferred_endian) {
      preferred_endian = type_endian_name(rhs_type);
   }

   for (int i = 0; root && i < root->count; i++) {
      ASTNode *node = root->children[i];
      int penalty = 0;
      const char *cand_endian;
      int cand_size;

      if (!node || strcmp(node->name, "type_decl_stmt")) {
         continue;
      }
      if (require_signed) {
         if (!type_is_signed_integer(node)) {
            continue;
         }
      }
      else if (!type_is_unsigned_integer(node)) {
         continue;
      }

      cand_size = type_size_from_node(node);
      if (cand_size < required_size) {
         continue;
      }

      cand_endian = type_endian_name(node);
      if (preferred_endian && cand_size > 1 && cand_endian && strcmp(preferred_endian, cand_endian)) {
         penalty += 8;
      }
      if (node == lhs_type || node == rhs_type) {
         penalty -= 1;
      }

      if (!best || cand_size < best_size || (cand_size == best_size && penalty < best_penalty)) {
         best = node;
         best_size = cand_size;
         best_penalty = penalty;
      }
   }

   if (!best) {
      warning("[%s:%d.%d] no integer promotion type can represent both operands; keeping existing operand type",
              origin ? origin->file : __FILE__, origin ? origin->line : __LINE__, origin ? origin->column : 0);
      if (lhs_size > rhs_size) {
         return lhs_type;
      }
      if (rhs_size > lhs_size) {
         return rhs_type;
      }
      return lhs_signed ? lhs_type : rhs_type;
   }

   return best;
}

static const ASTNode *literal_annotation_type(const ASTNode *expr) {
   if (!expr) {
      return NULL;
   }
   if ((expr->kind == AST_INTEGER || expr->kind == AST_FLOAT) && expr->count > 0 && expr->children[0]) {
      return expr->children[0];
   }
   return NULL;
}

static int integer_literal_min_size(const ASTNode *expr) {
   unsigned long long value;
   int size = 1;
   char *end = NULL;

   if (!expr || expr->kind != AST_INTEGER || !expr->strval) {
      return 0;
   }

   value = strtoull(expr->strval, &end, 0);
   if (end == expr->strval || (end && *end != 0)) {
      return 1;
   }

   while (size < (int) sizeof(value) && value > ((1ULL << (size * 8)) - 1ULL)) {
      size++;
   }

   return size;
}

// for parameterless flags (e.g. "$signed")
// also for complete flags (e.g. "$endian:little")
static bool has_flag(const char *type, const char *flag) {
   const ASTNode *node;

   if (!type || !flag) {
      return false;
   }

   node = get_typename_node(type);
   if (!node || node->count < 2 || is_empty(node->children[1])) {
      return false;
   }

   const ASTNode *flags = node->children[1];
   for (int i = 0; i < flags->count; i++) {
      if (flags->children[i] && flags->children[i]->strval && !strcmp(flags->children[i]->strval, flag)) {
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

static bool parse_flag_u64(const ASTNode *flags, const char *prefix, unsigned long long *out) {
   size_t prefix_len;

   if (!flags || is_empty(flags) || !prefix || !out) {
      return false;
   }

   prefix_len = strlen(prefix);
   for (int i = 0; i < flags->count; i++) {
      char *end = NULL;
      unsigned long long value;
      const char *text;

      if (!flags->children[i] || !flags->children[i]->strval) {
         continue;
      }
      text = flags->children[i]->strval;
      if (strncmp(text, prefix, prefix_len)) {
         continue;
      }
      value = strtoull(text + prefix_len, &end, 0);
      if (end && *end == '\0') {
         *out = value;
         return true;
      }
   }
   return false;
}

static const char *find_mem_modifier_name(const ASTNode *modifiers) {
   const char *found = NULL;

   if (!modifiers || is_empty(modifiers)) {
      return NULL;
   }

   for (int i = 0; i < modifiers->count; i++) {
      const char *name;
      if (!modifiers->children[i] || !modifiers->children[i]->strval) {
         continue;
      }
      name = modifiers->children[i]->strval;
      if (!memname_exists(name)) {
         continue;
      }
      if (found && strcmp(found, name)) {
         error("[%s:%d.%d] multiple mem modifiers '%s' and '%s' are not allowed",
               modifiers->file, modifiers->line, modifiers->column,
               found, name);
      }
      found = name;
   }

   return found;
}

static const ASTNode *find_mem_modifier_node(const ASTNode *modifiers) {
   const char *name = find_mem_modifier_name(modifiers);

   if (!name) {
      return NULL;
   }
   return get_memname_node(name);
}

static bool mem_decl_is_zeropage(const ASTNode *mem_decl) {
   const ASTNode *flags;
   unsigned long long start = 0;
   unsigned long long size = 0;
   unsigned long long end = 0;
   bool have_start;
   bool have_size;
   bool have_end;

   if (!mem_decl || strcmp(mem_decl->name, "mem_decl_stmt") || mem_decl->count < 2) {
      return false;
   }

   flags = mem_decl->children[1];
   have_start = parse_flag_u64(flags, "$start:", &start);
   have_size = parse_flag_u64(flags, "$size:", &size);
   have_end = parse_flag_u64(flags, "$end:", &end);

   if (!have_start) {
      return false;
   }

   if (have_size) {
      return start <= 0xFFull && size <= 0x100ull && start + size <= 0x100ull;
   }

   if (have_end) {
      return start <= 0xFFull && end <= 0x100ull && start <= end;
   }

   return false;
}

static bool modifiers_imply_zeropage(const ASTNode *modifiers) {
   return mem_decl_is_zeropage(find_mem_modifier_node(modifiers));
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
   entry->is_zeropage = false;
   entry->is_global = false;
   entry->is_ref = false;
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
   entry->is_zeropage = false;
   entry->is_global = false;
   entry->is_ref = false;
   entry->type = type;
   entry->declarator = NULL;
   entry->size = get_size(type_name_from_node(type));
   entry->offset = ctx->locals;
   ctx->locals += entry->size;
   debug("[%s:%d] ctx_push(%s, %s, %d, %d)", __FILE__, __LINE__, type->strval, name, entry->size, entry->offset);
   set_add(ctx->vars, strdup(name), entry);

   // TODO FIX increment the stack pointer.
}

static void ctx_resize_last_push(Context *ctx, const ASTNode *type, const ASTNode *declarator, const char *name) {
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


static void ctx_static(Context *ctx, const ASTNode *type, const char *name) {
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
   entry->is_zeropage = false;
   entry->is_global = false;
   entry->is_ref = false;
   entry->type = type;
   entry->declarator = NULL;
   entry->size = get_size(type_name_from_node(type));
   entry->offset = 0;
   debug("[%s:%d] ctx_static(%s, %s$%s, %d, %d)", __FILE__, __LINE__, type->strval, ctx->name, name, entry->size, entry->offset);
   set_add(ctx->vars, strdup(name), entry);
}

static void ctx_zeropage(Context *ctx, const ASTNode *type, const char *name) {
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
   entry->is_zeropage = true;
   entry->is_global = false;
   entry->is_ref = false;
   entry->type = type;
   entry->declarator = NULL;
   entry->size = get_size(type_name_from_node(type));
   entry->offset = 0;
   debug("[%s:%d] ctx_zeropage(%s, %s$%s, %d, %d)", __FILE__, __LINE__, type->strval, ctx->name, name, entry->size, entry->offset);
   set_add(ctx->vars, strdup(name), entry);
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

static bool parameter_is_ref(const ASTNode *parameter) {
   const ASTNode *decl_specs = parameter_decl_specifiers(parameter);
   const ASTNode *modifiers = (decl_specs && decl_specs->count > 0) ? decl_specs->children[0] : NULL;
   return has_modifier((ASTNode *) modifiers, "ref");
}

static int parameter_storage_size(const ASTNode *parameter) {
   const ASTNode *ptype = parameter_type(parameter);
   const ASTNode *pdecl = parameter_declarator(parameter);
   if (parameter_is_ref(parameter)) {
      return get_size("*");
   }
   return declarator_storage_size(ptype, pdecl);
}

static void ctx_resize_last_shove(Context *ctx, const ASTNode *type, const ASTNode *declarator, const char *name) {
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
   entry->offset = ctx->params + base_size - value_size;
   ctx->params -= (value_size - base_size);
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
         int slot_size;
         ContextEntry *entry;

         if (!type || parameter_is_void(parameter)) {
            continue;
         }

         size = declarator_storage_size(type, param_decl);
         slot_size = parameter_storage_size(parameter);
         if (has_modifier((ASTNode *) decl_specs->children[0], "static")) {
            ctx_static(ctx, type, name);
            entry = (ContextEntry *) set_get(ctx->vars, name);
            entry->size = size;
            entry->declarator = param_decl;
            entry->is_ref = parameter_is_ref(parameter);
         }
         else if (modifiers_imply_zeropage((ASTNode *) decl_specs->children[0])) {
            ctx_zeropage(ctx, type, name);
            entry = (ContextEntry *) set_get(ctx->vars, name);
            entry->size = size;
            entry->declarator = param_decl;
            entry->is_ref = parameter_is_ref(parameter);
         }
         else {
            ctx_shove(ctx, type, name);
            entry = (ContextEntry *) set_get(ctx->vars, name);
            entry->size = size;
            entry->declarator = param_decl;
            entry->is_ref = parameter_is_ref(parameter);
            entry->offset = ctx->params + get_size(type_name_from_node(type)) - slot_size;
            ctx->params -= (slot_size - get_size(type_name_from_node(type)));
         }
         i++;
      }
   }

   ctx_shove(ctx, node->children[0]->children[1], "$$");
   ctx_resize_last_shove(ctx, node->children[0]->children[1], declarator, "$$");
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

static bool make_incdec_delta_bytes(const ASTNode *type, const ASTNode *declarator, int size, unsigned char *bytes) {
   int step = 1;
   char step_buf[64];

   if (!bytes || size <= 0) {
      return false;
   }

   memset(bytes, 0, (size_t) size);
   if (declarator && declarator_pointer_depth(declarator) > 0) {
      step = declarator_first_element_size(type, declarator);
      if (step <= 0) {
         step = 1;
      }
   }

   snprintf(step_buf, sizeof(step_buf), "%d", step);
   if (type && has_flag(type_name_from_node(type), "$endian:big")) {
      make_be_int(step_buf, bytes, size);
   }
   else {
      make_le_int(step_buf, bytes, size);
   }
   return true;
}

static void emit_copy_fp_to_fp(int dst_offset, int src_offset, int size) {
   bool dst_direct;
   bool src_direct;

   if (size <= 0 || dst_offset == src_offset) {
      return;
   }

   dst_direct = dst_offset >= 0 && dst_offset + size <= 256;
   src_direct = src_offset >= 0 && src_offset + size <= 256;

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

static bool resolve_ref_argument_lvalue(Context *ctx, ASTNode *expr, LValueRef *out) {
   ContextEntry *entry;
   expr = (ASTNode *) unwrap_expr_node(expr);
   if (!expr) {
      return false;
   }
   if (!strcmp(expr->name, "lvalue") && expr->count > 0) {
      if (!out) {
         LValueRef tmp;
         return resolve_lvalue(ctx, expr, &tmp);
      }
      return resolve_lvalue(ctx, expr, out);
   }
   if (expr->kind != AST_IDENTIFIER) {
      return false;
   }
   entry = ctx_lookup(ctx, expr->strval);
   if (!entry) {
      const ASTNode *g = global_decl_lookup(expr->strval);
      if (g && g->count >= 3) {
         static ContextEntry gtmp;
         gtmp.name = expr->strval;
         gtmp.type = g->children[1];
         gtmp.declarator = g->children[2];
         gtmp.is_static = false;
         gtmp.is_zeropage = modifiers_imply_zeropage((ASTNode *) g->children[0]);
         gtmp.is_global = true;
         gtmp.is_ref = false;
         gtmp.offset = 0;
         gtmp.size = declarator_storage_size(gtmp.type, gtmp.declarator);
         entry = &gtmp;
      }
   }
   if (!entry) {
      return false;
   }
   if (out) {
      memset(out, 0, sizeof(*out));
      out->name = entry->name ? entry->name : expr->strval;
      out->type = entry->type;
      out->declarator = entry->declarator;
      out->is_static = entry->is_static;
      out->is_zeropage = entry->is_zeropage;
      out->is_global = entry->is_global;
      out->is_ref = entry->is_ref;
      out->offset = entry->offset;
      out->size = entry->size;
      if (entry->is_ref) {
         out->indirect = true;
      }
   }
   return true;
}

static bool compile_ref_argument_to_slot(ASTNode *expr, Context *ctx, int dst_offset, int dst_size) {
   LValueRef lv;
   if (!resolve_ref_argument_lvalue(ctx, expr, &lv)) {
      error("[%s:%d.%d] ref argument must be an lvalue", expr->file, expr->line, expr->column);
   }
   if (lv.indirect) {
      if (lv.is_static || lv.is_zeropage || lv.is_global) {
         char sym[256];
         if (!entry_symbol_name(ctx, &(ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_zeropage = lv.is_zeropage, .is_global = lv.is_global, .is_ref = lv.is_ref, .offset = lv.offset, .size = lv.size }, sym, sizeof(sym))) {
            return false;
         }
         emit(&es_code, "    ldy #0\n");
         emit(&es_code, "    lda %s,y\n", sym);
         emit(&es_code, "    sta ptr0\n");
         emit(&es_code, "    iny\n");
         emit(&es_code, "    lda %s,y\n", sym);
         emit(&es_code, "    sta ptr0+1\n");
         emit_add_immediate_to_ptr(0, lv.ptr_adjust);
      }
      else {
         emit_load_ptr_from_fpvar(0, lv.offset);
         emit_add_immediate_to_ptr(0, lv.ptr_adjust);
      }
   }
   else if (lv.is_static || lv.is_zeropage || lv.is_global) {
      char sym[256];
      if (!entry_symbol_name(ctx, &(ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_zeropage = lv.is_zeropage, .is_global = lv.is_global, .is_ref = lv.is_ref, .offset = lv.offset, .size = lv.size }, sym, sizeof(sym))) {
         return false;
      }
      emit(&es_code, "    lda #<(%s + %d)\n", sym, lv.offset + lv.ptr_adjust);
      emit(&es_code, "    sta ptr0\n");
      emit(&es_code, "    lda #>(%s + %d)\n", sym, lv.offset + lv.ptr_adjust);
      emit(&es_code, "    sta ptr0+1\n");
   }
   else {
      emit_prepare_fp_ptr(0, lv.offset + lv.ptr_adjust);
   }
   emit_store_ptr_to_fp(dst_offset, 0, dst_size);
   return true;
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
      if (out->indirect) {
         out->ptr_adjust += atoi(idx->strval) * elem_size;
      }
      else {
         out->offset += atoi(idx->strval) * elem_size;
      }
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
            gtmp.is_zeropage = modifiers_imply_zeropage((ASTNode *) g->children[0]);
            gtmp.is_global = true;
            gtmp.is_ref = false;
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
      out->is_zeropage = entry->is_zeropage;
      out->is_global = entry->is_global;
      out->is_ref = entry->is_ref;
      out->offset = entry->offset;
      out->size = entry->size;
      if (entry->is_ref) {
         out->indirect = true;
      }
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
      out->is_zeropage = entry->is_zeropage;
      out->is_global = entry->is_global;
      out->is_ref = entry->is_ref;
      out->indirect = true;
      out->offset = entry->offset;
      out->size = get_size(type_name_from_node(entry->type));
   }
   else {
      return false;
   }

   return resolve_lvalue_suffixes(ctx, node->children[1], out);
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

   fn = resolve_function_call_target(callee->strval, args, ctx);
   if (fn) {
      const ASTNode *known_ret = function_return_type(fn);
      const ASTNode *params = NULL;
      declarator = function_declarator_node(fn);
      if (known_ret) {
         ret_type = known_ret;
         ret_size = declarator_value_size(ret_type, declarator);
      }
      if (declarator && declarator->count > 2) {
         params = declarator->children[2];
         if (params && !is_empty(params)) {
            int fixed_params = 0;
            for (int i = 0; i < params->count; i++) {
               const ASTNode *parameter = params->children[i];
               const ASTNode *ptype = parameter_type(parameter);
               if (!ptype || parameter_is_void(parameter)) {
                  continue;
               }
               fixed_params++;
               arg_total += parameter_storage_size(parameter);
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
            psz = parameter_storage_size(parameter);
            tmp.type = parameter_is_ref(parameter) ? required_typename_node("*") : ptype;
            tmp.declarator = parameter_is_ref(parameter) ? NULL : pdecl;
            tmp.is_static = false;
            tmp.is_zeropage = false;
            tmp.is_global = false;
            tmp.is_ref = false;
            tmp.offset = ctx->locals + arg_offset;
            tmp.size = psz;
            if (parameter_is_ref(parameter)) {
               if (!compile_ref_argument_to_slot(args->children[actual_index], ctx, tmp.offset, tmp.size)) {
                  if (call_size > 0) {
                     remember_runtime_import("popN");
                     emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
                     emit(&es_code, "    sta arg0\n");
                     emit(&es_code, "    jsr _popN\n");
                  }
                  return false;
               }
            }
            else if (!compile_expr_to_slot(args->children[actual_index], ctx, &tmp)) {
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

   {
      char callee_sym[256];
      if (!function_symbol_name(fn, callee->strval, callee_sym, sizeof(callee_sym))) {
         return false;
      }
      remember_symbol_import(callee_sym);
      emit(&es_code, "    lda fp+1\n");
      emit(&es_code, "    pha\n");
      emit(&es_code, "    lda fp\n");
      emit(&es_code, "    pha\n");
      emit(&es_code, "    jsr _%s\n", callee_sym);
      emit(&es_code, "    pla\n");
      emit(&es_code, "    sta fp\n");
      emit(&es_code, "    pla\n");
      emit(&es_code, "    sta fp+1\n");
   }

   if (dst && ret_size > 0) {
      emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, ctx->locals, ret_size, ret_type);
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

   if (expr->kind == AST_FLOAT) {
      unsigned char *bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
      if (has_flag(type_name_from_node(dst->type), "$endian:big")) {
         make_be_float(expr->strval, bytes, dst->size);
      }
      else {
         make_le_float(expr->strval, bytes, dst->size);
      }
      emit_store_immediate_to_fp(dst->offset, bytes, dst->size);
      free(bytes);
      return true;
   }

   if (expr->kind == AST_STRING) {
      const char *label = remember_string_literal(expr->strval);
      emit_store_label_address_to_fp(dst->offset, dst->size, label);
      return true;
   }

   if (expr->kind == AST_IDENTIFIER) {
      ContextEntry *entry = ctx_lookup(ctx, expr->strval);
      if (entry && !entry->is_static && !entry->is_zeropage) {
         emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, entry->offset, entry->size, entry->type);
         return true;
      }
      if (entry) {
         char sym[256];
         if (entry_symbol_name(ctx, entry, sym, sizeof(sym))) {
            emit_copy_symbol_to_fp_convert(dst->offset, dst->size, dst->type, sym, entry->size, entry->type);
            return true;
         }
      }
      {
         const ASTNode *g = global_decl_lookup(expr->strval);
         if (g && g->count >= 3) {
            char sym[256];
            int gsize = declarator_storage_size(g->children[1], g->children[2]);
            snprintf(sym, sizeof(sym), "_%s", expr->strval);
            emit_copy_symbol_to_fp_convert(dst->offset, dst->size, dst->type, sym, gsize, g->children[1]);
            return true;
         }
      }
   }

   if (expr->count == 1 && !strcmp(expr->name, "&")) {
      LValueRef lv;
      ASTNode *inner = (ASTNode *) unwrap_expr_node(expr->children[0]);
      if (inner && !strcmp(inner->name, "lvalue") && resolve_lvalue(ctx, inner, &lv)) {
         if (lv.indirect) {
            if (lv.is_static || lv.is_zeropage || lv.is_global) {
               char sym[256];
               if (!entry_symbol_name(ctx, &(ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_zeropage = lv.is_zeropage, .is_global = lv.is_global, .offset = lv.offset, .size = lv.size }, sym, sizeof(sym))) {
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
         else if (lv.is_static || lv.is_zeropage || lv.is_global) {
            char sym[256];
            if (!entry_symbol_name(ctx, &(ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_zeropage = lv.is_zeropage, .is_global = lv.is_global, .offset = lv.offset, .size = lv.size }, sym, sizeof(sym))) {
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
      const ASTNode *ofn;
      if (!resolve_lvalue(ctx, expr, &lv)) {
         return false;
      }
      classify_incdec_lvalue_expr(expr, &inc, &pre);
      ofn = resolve_incdec_overload_expr(expr, ctx);
      if (ofn) {
         const ASTNode *rtype = function_return_type(ofn);
         const ASTNode *rdecl = function_declarator_node(ofn);
         int old_size = lv.size > 0 ? lv.size : dst->size;
         int result_size = declarator_storage_size(rtype, rdecl);
         int store_size = lv.size > 0 ? lv.size : old_size;
         int result_offset;
         int store_offset;
         int tmp_total;
         ContextEntry result_tmp;
         ASTNode *operand;
         ASTNode *argv[1] = { NULL };
         ASTNode *call;

         if (result_size <= 0) {
            result_size = type_size_from_node(rtype);
         }
         if (result_size <= 0) {
            error("[%s:%d.%d] overloaded %s has unknown return size", expr->file, expr->line, expr->column, inc ? "operator++" : "operator--");
         }
         result_offset = ctx->locals + old_size;
         store_offset = result_offset + result_size;
         tmp_total = old_size + result_size + store_size;
         result_tmp = (ContextEntry){ .name = "$incdec_result", .type = rtype, .declarator = rdecl, .is_static = false, .is_zeropage = false, .is_global = false, .offset = result_offset, .size = result_size };
         operand = make_synthetic_incdec_operand(expr);
         if (!operand) {
            return false;
         }
         argv[0] = operand;
         call = make_synthetic_call_expr(expr, function_declarator_node(ofn)->children[1]->strval, argv, 1);
         if (!call) {
            return false;
         }

         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         emit_copy_lvalue_to_fp(ctx->locals, &lv, old_size);
         if (!pre) {
            emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, ctx->locals, old_size, lv.type);
         }
         if (!compile_call_expr_to_slot(call, ctx, &result_tmp)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         emit_copy_fp_to_fp_convert(store_offset, store_size, lv.type, result_offset, result_size, rtype);
         emit_copy_fp_to_lvalue(&lv, store_offset, store_size);
         if (pre) {
            emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, store_offset, store_size, lv.type);
         }
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return true;
      }
      {
         int tmp_size;
         ContextEntry tmp;
         unsigned char *one;
         tmp_size = lv.size > 0 ? lv.size : dst->size;
         tmp = (ContextEntry){ .name = "$tmp", .type = lv.type, .declarator = lv.declarator, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = tmp_size };
         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         emit_copy_lvalue_to_fp(tmp.offset, &lv, tmp.size);
         if (!pre) {
            emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, tmp.offset, tmp.size, tmp.type);
         }
         one = (unsigned char *) calloc(tmp.size ? tmp.size : 1, sizeof(unsigned char));
         if (!one) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         if (!make_incdec_delta_bytes(tmp.type, lv.declarator, tmp.size, one)) {
            free(one);
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         if (inc) {
            emit_add_immediate_to_fp(tmp.type, tmp.offset, one, tmp.size);
         }
         else {
            emit_sub_immediate_from_fp(tmp.type, tmp.offset, one, tmp.size);
         }
         free(one);
         emit_copy_fp_to_lvalue(&lv, tmp.offset, tmp.size);
         if (pre) {
            emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, tmp.offset, tmp.size, tmp.type);
         }
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return true;
      }
   }

   if (!strcmp(expr->name, "lvalue") && expr->count > 0) {
      LValueRef lv;
      if (resolve_lvalue(ctx, expr, &lv)) {
         if (!lv.is_static && !lv.is_zeropage && !lv.is_global) {
            emit_copy_lvalue_to_fp(dst->offset, &lv, lv.size < dst->size ? lv.size : dst->size);
            emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, dst->offset, lv.size < dst->size ? lv.size : dst->size, lv.type);
            return true;
         }
         if (lv.indirect) {
            char sym[256];
            if (!entry_symbol_name(ctx, &(ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_zeropage = lv.is_zeropage, .is_global = lv.is_global, .offset = lv.offset, .size = lv.size }, sym, sizeof(sym))) {
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
            emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, dst->offset, lv.size < dst->size ? lv.size : dst->size, lv.type);
            return true;
         }
         else {
            char sym[256];
            if (!entry_symbol_name(ctx, &(ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_zeropage = lv.is_zeropage, .is_global = lv.is_global, .offset = lv.offset, .size = lv.size }, sym, sizeof(sym))) {
               return false;
            }
            emit_copy_symbol_to_fp_convert(dst->offset, dst->size, dst->type, sym, lv.size, lv.type);
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

   {
      const ASTNode *ofn = resolve_operator_overload_expr(expr, ctx);
      if (ofn) {
         ASTNode *argv[2] = { NULL, NULL };
         ASTNode *call;
         argv[0] = expr->children[0];
         if (expr->count > 1) {
            argv[1] = expr->children[1];
         }
         call = make_synthetic_call_expr(expr, function_declarator_node(ofn)->children[1]->strval, argv, expr->count);
         return call ? compile_call_expr_to_slot(call, ctx, dst) : false;
      }
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

   if (expr->count == 2 && (!strcmp(expr->name, "==") || !strcmp(expr->name, "!=") ||
                            !strcmp(expr->name, "<") || !strcmp(expr->name, ">") ||
                            !strcmp(expr->name, "<=") || !strcmp(expr->name, ">="))) {
      const char *false_label = next_label("cmp_false");
      const char *end_label = next_label("cmp_end");
      unsigned char *zeroes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
      unsigned char *ones = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
      if (!false_label || !end_label || !zeroes || !ones) {
         free(zeroes);
         free(ones);
         free((void *) false_label);
         free((void *) end_label);
         return false;
      }
      ones[0] = 1;
      if (!compile_condition_branch_false(expr, ctx, false_label)) {
         free(zeroes);
         free(ones);
         free((void *) false_label);
         free((void *) end_label);
         return false;
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
      const ASTNode *work_type = expr_value_type(expr, ctx);
      int work_size = expr_value_size(expr, ctx);
      int pointer_scale = 1;
      bool scaled_pointer_arith = lhs_decl && declarator_pointer_depth(lhs_decl) > 0;

      if (scaled_pointer_arith) {
         work_size = declarator_storage_size(lhs_type, lhs_decl);
         if (work_size <= 0) {
            work_size = dst->size;
         }
      }
      if (work_size <= 0) {
         work_size = dst->size;
      }
      if (work_size <= 0) {
         work_size = 1;
      }
      if (!work_type) {
         work_type = scaled_pointer_arith ? lhs_type : dst->type;
      }
      if (scaled_pointer_arith) {
         pointer_scale = declarator_first_element_size(lhs_type, lhs_decl);
         if (pointer_scale <= 0) {
            pointer_scale = 1;
         }
      }

      if (!strcmp(expr->name, "-") && lhs_decl && rhs && expr_value_declarator((ASTNode *) rhs, ctx) && declarator_pointer_depth(expr_value_declarator((ASTNode *) rhs, ctx)) > 0) {
         int ptr_size = declarator_storage_size(lhs_type, lhs_decl);
         int elem_size = pointer_scale > 0 ? pointer_scale : 1;
         int tmp_total = ptr_size * 3;
         int lhs_off = ctx->locals;
         int rhs_off = lhs_off + ptr_size;
         int quo_off = rhs_off + ptr_size;
         ContextEntry lhs_tmp = { .name = "$lhs", .type = lhs_type, .declarator = lhs_decl, .is_static = false, .is_zeropage = false, .is_global = false, .offset = lhs_off, .size = ptr_size };
         ContextEntry rhs_tmp = { .name = "$rhs", .type = lhs_type, .declarator = lhs_decl, .is_static = false, .is_zeropage = false, .is_global = false, .offset = rhs_off, .size = ptr_size };
         unsigned char *factor_bytes;
         char factor_buf[64];
         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         if (!compile_expr_to_slot(expr->children[0], ctx, &lhs_tmp) || !compile_expr_to_slot((ASTNode *) rhs, ctx, &rhs_tmp)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         emit_sub_fp_from_fp(lhs_type, lhs_off, rhs_off, ptr_size);
         factor_bytes = (unsigned char *) calloc(ptr_size ? ptr_size : 1, sizeof(unsigned char));
         if (!factor_bytes) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         snprintf(factor_buf, sizeof(factor_buf), "%d", elem_size);
         if (has_flag(type_name_from_node(lhs_type), "$endian:big")) make_be_int(factor_buf, factor_bytes, ptr_size);
         else make_le_int(factor_buf, factor_bytes, ptr_size);
         emit_store_immediate_to_fp(rhs_off, factor_bytes, ptr_size);
         free(factor_bytes);
         emit_prepare_fp_ptr(0, lhs_off);
         emit_prepare_fp_ptr(1, rhs_off);
         emit_prepare_fp_ptr(2, quo_off);
         emit_prepare_fp_ptr(3, rhs_off);
         emit(&es_code, "    lda #$%02x\n", ptr_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         remember_runtime_import("divN");
         emit(&es_code, "    jsr _divN\n");
         emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, quo_off, ptr_size, dst->type ? dst->type : lhs_type);
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return true;
      }

      {
         int lhs_offset = ctx->locals;
         int rhs_offset = lhs_offset + work_size;
         int factor_offset = 0;
         int scaled_offset = 0;
         int value_offset = rhs_offset;
         int tmp_total = work_size * 2;
         const ASTNode *rhs_slot_type = scaled_pointer_arith ? expr_value_type((ASTNode *) rhs, ctx) : work_type;
         ContextEntry lhs_tmp = { .name = "$lhs", .type = work_type, .declarator = lhs_decl, .is_static = false, .is_zeropage = false, .is_global = false, .offset = lhs_offset, .size = work_size };
         ContextEntry rhs_tmp = { .name = "$rhs", .type = rhs_slot_type ? rhs_slot_type : work_type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = rhs_offset, .size = work_size };

         if (scaled_pointer_arith && pointer_scale != 1) {
            tmp_total += work_size * 2;
            factor_offset = rhs_offset + work_size;
            scaled_offset = factor_offset + work_size;
            value_offset = scaled_offset;
         }

         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");

         if (!compile_expr_to_slot(expr->children[0], ctx, &lhs_tmp) ||
             !compile_expr_to_slot((ASTNode *) rhs, ctx, &rhs_tmp)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }

         if (scaled_pointer_arith && pointer_scale != 1) {
            unsigned char *factor_bytes = (unsigned char *) calloc(work_size ? work_size : 1, sizeof(unsigned char));
            char scaled_buf[64];
            const ASTNode *factor_type = rhs_slot_type ? rhs_slot_type : work_type;
            snprintf(scaled_buf, sizeof(scaled_buf), "%d", pointer_scale);
            if (factor_type && has_flag(type_name_from_node(factor_type), "$endian:big")) {
               make_be_int(scaled_buf, factor_bytes, work_size);
            }
            else {
               make_le_int(scaled_buf, factor_bytes, work_size);
            }
            emit_store_immediate_to_fp(factor_offset, factor_bytes, work_size);
            free(factor_bytes);
            emit_runtime_binary_fp_fp("mulN", scaled_offset, rhs_offset, factor_offset, work_size);
         }

         if (!strcmp(expr->name, "+")) {
            emit_add_fp_to_fp(work_type, lhs_offset, value_offset, work_size);
         }
         else {
            emit_sub_fp_from_fp(work_type, lhs_offset, value_offset, work_size);
         }

         emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, lhs_offset, work_size, work_type);
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return true;
      }
   }



   if (expr->count == 2 && (!strcmp(expr->name, "<<") || !strcmp(expr->name, ">>"))) {
      const char *op = expr->name;
      const ASTNode *lhs_type = expr_value_type(expr->children[0], ctx);
      const ASTNode *rhs_type = expr_value_type(expr->children[1], ctx);
      const ASTNode *op_type = lhs_type ? lhs_type : expr_value_type(expr, ctx);
      const ASTNode *rhs_slot_type = rhs_type ? rhs_type : op_type;
      int lhs_size = op_type ? type_size_from_node(op_type) : 0;
      int rhs_size = rhs_slot_type ? type_size_from_node(rhs_slot_type) : 0;
      int tmp_total;
      int lhs_offset;
      int rhs_offset;
      int aux_offset;
      ContextEntry lhs_tmp;
      ContextEntry rhs_tmp;
      const char *helper;

      if (lhs_size <= 0) {
         lhs_size = expr_value_size(expr->children[0], ctx);
      }
      if (lhs_size <= 0) {
         lhs_size = expr_value_size(expr, ctx);
      }
      if (lhs_size <= 0) {
         lhs_size = dst->size > 0 ? dst->size : 1;
      }
      if (rhs_size <= 0) {
         rhs_size = expr_value_size(expr->children[1], ctx);
      }
      if (rhs_size <= 0) {
         rhs_size = 1;
      }

      tmp_total = lhs_size + rhs_size + lhs_size;
      lhs_offset = ctx->locals;
      rhs_offset = lhs_offset + lhs_size;
      aux_offset = rhs_offset + rhs_size;
      lhs_tmp = (ContextEntry){ .name = "$lhs", .type = op_type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = lhs_offset, .size = lhs_size };
      rhs_tmp = (ContextEntry){ .name = "$rhs", .type = rhs_slot_type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = rhs_offset, .size = rhs_size };

      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");

      if (!compile_expr_to_slot(expr->children[0], ctx, &lhs_tmp) ||
          !compile_expr_to_slot(expr->children[1], ctx, &rhs_tmp)) {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return false;
      }

      helper = !strcmp(op, "<<") ? "lslN" : (op_type && has_flag(type_name_from_node(op_type), "$signed") ? "asrN" : "lsrN");
      emit_runtime_shift_fp(helper, lhs_offset, aux_offset, rhs_offset, lhs_size);

      emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, lhs_offset, lhs_size, op_type);
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      return true;
   }

   if (expr->count == 2 && (!strcmp(expr->name, "&") || !strcmp(expr->name, "|") || !strcmp(expr->name, "^") ||
                            !strcmp(expr->name, "*") || !strcmp(expr->name, "/") || !strcmp(expr->name, "%"))) {
      const char *op = expr->name;
      const ASTNode *op_type = expr_value_type(expr, ctx);
      int op_size = expr_value_size(expr, ctx);
      int tmp_total;
      int lhs_offset;
      int rhs_offset;
      int aux_offset;
      ContextEntry lhs_tmp;
      ContextEntry rhs_tmp;
      const char *helper = NULL;

      if (op_size <= 0) {
         op_size = expr_value_size(expr->children[0], ctx);
      }
      if (op_size <= 0 && expr->count > 1) {
         op_size = expr_value_size(expr->children[1], ctx);
      }
      if (op_size <= 0) {
         op_size = dst->size > 0 ? dst->size : 1;
      }
      if (!op_type) {
         op_type = dst->type;
      }

      tmp_total = op_size * 2;
      if (!strcmp(op, "*") || !strcmp(op, "/") || !strcmp(op, "%")) {
         tmp_total += op_size * 2;
      }

      lhs_offset = ctx->locals;
      rhs_offset = lhs_offset + op_size;
      aux_offset = rhs_offset + op_size;
      lhs_tmp = (ContextEntry){ .name = "$lhs", .type = op_type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = lhs_offset, .size = op_size };
      rhs_tmp = (ContextEntry){ .name = "$rhs", .type = op_type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = rhs_offset, .size = op_size };

      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");

      if (!compile_expr_to_slot(expr->children[0], ctx, &lhs_tmp) ||
          !compile_expr_to_slot(expr->children[1], ctx, &rhs_tmp)) {
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
         emit_runtime_binary_fp_fp(helper, lhs_offset, lhs_offset, rhs_offset, op_size);
      }
      else if (!strcmp(op, "*")) {
         emit_runtime_binary_fp_fp("mulN", aux_offset, lhs_offset, rhs_offset, op_size);
         emit_copy_fp_to_fp(lhs_offset, aux_offset, op_size);
      }
      else if (!strcmp(op, "/") || !strcmp(op, "%")) {
         int rem_offset = aux_offset + op_size;
         emit_prepare_fp_ptr(0, lhs_offset);
         emit_prepare_fp_ptr(1, rhs_offset);
         emit_prepare_fp_ptr(2, aux_offset);
         emit_prepare_fp_ptr(3, rem_offset);
         emit(&es_code, "    lda #$%02x\n", op_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         remember_runtime_import("divN");
         emit(&es_code, "    jsr _divN\n");
         emit_copy_fp_to_fp(lhs_offset, !strcmp(op, "/") ? aux_offset : rem_offset, op_size);
      }

      emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, lhs_offset, op_size, op_type);
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      return true;
   }


   return false;
}


static const ASTNode *expr_value_type(ASTNode *expr, Context *ctx) {
   const ASTNode *lhs_type;
   const ASTNode *rhs_type;

   expr = (ASTNode *) unwrap_expr_node(expr);

   if (!expr || is_empty(expr)) {
      return NULL;
   }

   if (expr->kind == AST_INTEGER || expr->kind == AST_FLOAT) {
      return literal_annotation_type(expr);
   }

   if (expr->kind == AST_STRING) {
      return required_typename_node("*");
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
      return required_typename_node("*");
   }

   if (!strcmp(expr->name, "lvalue") && expr->count > 0) {
      LValueRef lv;
      if (resolve_lvalue(ctx, expr, &lv)) {
         return lv.type;
      }
   }

   if (!strcmp(expr->name, "()")) {
      ASTNode *callee = expr->children[0];
      ASTNode *args = (expr->count > 1) ? expr->children[1] : NULL;
      const ASTNode *fn = NULL;
      if (callee && callee->kind == AST_IDENTIFIER) {
         fn = resolve_function_call_target(callee->strval, args, ctx);
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
      lhs_type = expr_value_type(expr->children[2], ctx);
      rhs_type = expr_value_type(expr->children[3], ctx);
      return lhs_type ? lhs_type : rhs_type;
   }

   if ((expr->count == 1 && !strcmp(expr->name, "!")) ||
       (expr->count == 2 && (!strcmp(expr->name, "==") || !strcmp(expr->name, "!=") ||
        !strcmp(expr->name, "<") || !strcmp(expr->name, ">") || !strcmp(expr->name, "<=") || !strcmp(expr->name, ">=") ||
        !strcmp(expr->name, "&&") || !strcmp(expr->name, "||")))) {
      return bool_type_node();
   }

   if (expr->count == 2 && !strcmp(expr->name, "-")) {
      const ASTNode *lhs_decl = expr_value_declarator(expr->children[0], ctx);
      const ASTNode *rhs_decl = expr_value_declarator(expr->children[1], ctx);
      if (lhs_decl && rhs_decl && declarator_pointer_depth(lhs_decl) > 0 && declarator_pointer_depth(rhs_decl) > 0) {
         lhs_type = expr_value_type(expr->children[0], ctx);
         rhs_type = expr_value_type(expr->children[1], ctx);
         return lhs_type ? lhs_type : rhs_type;
      }
   }

   if (expr->count == 2 && (!strcmp(expr->name, "+") || !strcmp(expr->name, "-") ||
                            !strcmp(expr->name, "&") || !strcmp(expr->name, "|") || !strcmp(expr->name, "^") ||
                            !strcmp(expr->name, "*") || !strcmp(expr->name, "/") || !strcmp(expr->name, "%") ||
                            !strcmp(expr->name, "<<") || !strcmp(expr->name, ">>"))) {
      const ASTNode *lhs_decl = expr_value_declarator(expr->children[0], ctx);
      const ASTNode *rhs_decl = expr_value_declarator(expr->children[1], ctx);
      lhs_type = expr_value_type(expr->children[0], ctx);
      rhs_type = expr_value_type(expr->children[1], ctx);
      if ((!strcmp(expr->name, "+") || !strcmp(expr->name, "-")) && lhs_decl && declarator_pointer_depth(lhs_decl) > 0) {
         return lhs_type;
      }
      if (!strcmp(expr->name, "+") && rhs_decl && declarator_pointer_depth(rhs_decl) > 0) {
         return rhs_type;
      }
      if (!strcmp(expr->name, "<<") || !strcmp(expr->name, ">>")) {
         return lhs_type ? lhs_type : rhs_type;
      }
      {
         const ASTNode *promoted = promoted_integer_type_for_binary(lhs_type, rhs_type, expr);
         if (promoted) {
            return promoted;
         }
      }
   }

   if (expr->count >= 1) {
      lhs_type = expr_value_type(expr->children[0], ctx);
      if (lhs_type) {
         return lhs_type;
      }
   }

   if (expr->count >= 2) {
      rhs_type = expr_value_type(expr->children[1], ctx);
      if (rhs_type) {
         return rhs_type;
      }
   }

   return NULL;
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

   if (!strcmp(expr->name, "()")) {
      ASTNode *callee = expr->children[0];
      ASTNode *args = (expr->count > 1) ? expr->children[1] : NULL;
      const ASTNode *fn = NULL;
      if (callee && callee->kind == AST_IDENTIFIER) {
         fn = resolve_function_call_target(callee->strval, args, ctx);
      }
      if (fn) {
         return function_declarator_node(fn);
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
   const char *name = type_name_from_node(type);

   if (!name) {
      return 0;
   }

   return get_size(name);
}

static int declarator_value_size(const ASTNode *type, const ASTNode *declarator) {
   int size;
   int mult = 1;

   if (!type) {
      return 0;
   }

   size = declarator_pointer_depth(declarator) > 0 ? get_size("*") : get_size(type_name_from_node(type));

   if (!declarator) {
      return size;
   }

   for (int i = 2; i < declarator->count; i++) {
      if (declarator->children[i] && declarator->children[i]->kind == AST_INTEGER) {
         mult *= atoi(declarator->children[i]->strval);
      }
   }

   return size * mult;
}

static int expr_value_size(ASTNode *expr, Context *ctx) {
   const ASTNode *type;
   const ASTNode *declarator;
   int lhs_size;
   int rhs_size;

   expr = (ASTNode *) unwrap_expr_node(expr);
   if (!expr || is_empty(expr)) {
      return 0;
   }

   if (expr->kind == AST_INTEGER) {
      type = literal_annotation_type(expr);
      return type ? type_size_from_node(type) : integer_literal_min_size(expr);
   }

   if (expr->kind == AST_FLOAT) {
      type = literal_annotation_type(expr);
      return type ? type_size_from_node(type) : 0;
   }

   type = expr_value_type(expr, ctx);
   declarator = expr_value_declarator(expr, ctx);
   if (type) {
      return declarator ? declarator_value_size(type, declarator) : type_size_from_node(type);
   }

   if (!strcmp(expr->name, "conditional_expr") && expr->count == 4 && expr->children[0] && expr->children[0]->kind == AST_IDENTIFIER && !strcmp(expr->children[0]->strval, "?:")) {
      lhs_size = expr_value_size(expr->children[2], ctx);
      rhs_size = expr_value_size(expr->children[3], ctx);
      return lhs_size > rhs_size ? lhs_size : rhs_size;
   }

   lhs_size = (expr->count >= 1) ? expr_value_size(expr->children[0], ctx) : 0;
   rhs_size = (expr->count >= 2) ? expr_value_size(expr->children[1], ctx) : 0;
   return lhs_size > rhs_size ? lhs_size : rhs_size;
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

   if (expr->kind == AST_INTEGER) {
      if (!expr->strval || !strcmp(expr->strval, "0")) {
         emit(&es_code, "    jmp %s\n", false_label);
      }
      return true;
   }

   {
      const ASTNode *ofn = resolve_operator_overload_expr(expr, ctx);
      if (ofn) {
         const ASTNode *rtype = function_return_type(ofn);
         const ASTNode *rdecl = function_declarator_node(ofn);
         int rsize = declarator_storage_size(rtype, rdecl);
         ContextEntry tmp;
         ASTNode *argv[2] = { NULL, NULL };
         ASTNode *call;
         if (rsize <= 0) {
            rsize = type_size_from_node(rtype);
         }
         if (rsize <= 0) {
            error("[%s:%d.%d] overloaded operator '%s' has unknown return size", expr->file, expr->line, expr->column, expr->name);
         }
         argv[0] = expr->children[0];
         if (expr->count > 1) {
            argv[1] = expr->children[1];
         }
         call = make_synthetic_call_expr(expr, function_declarator_node(ofn)->children[1]->strval, argv, expr->count);
         if (!call) {
            return false;
         }
         tmp = (ContextEntry){ .name = "$tmp", .type = rtype, .declarator = rdecl, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = rsize };
         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", rsize & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         if (!compile_call_expr_to_slot(call, ctx, &tmp)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", rsize & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         emit(&es_code, "    lda #0\n");
         for (int i = 0; i < rsize; i++) {
            emit(&es_code, "    ldy #%d\n", tmp.offset + i);
            emit(&es_code, "    ora (fp),y\n");
         }
         emit(&es_code, "    sta arg1\n");
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", rsize & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         emit(&es_code, "    lda arg1\n");
         emit(&es_code, "    beq %s\n", false_label);
         return true;
      }
   }

   {
      const ASTNode *tfn = resolve_truthiness_overload(expr, ctx);
      if (tfn) {
         ASTNode *argv[1] = { expr };
         ASTNode *call = make_synthetic_call_expr(expr, function_declarator_node(tfn)->children[1]->strval, argv, 1);
         const ASTNode *rtype = function_return_type(tfn);
         const ASTNode *rdecl = function_declarator_node(tfn);
         int rsize = declarator_storage_size(rtype, rdecl);
         ContextEntry tmp;
         if (!call) {
            return false;
         }
         if (rsize <= 0) {
            rsize = type_size_from_node(rtype);
         }
         if (rsize <= 0) {
            error("[%s:%d.%d] truthiness overload has unknown return size", expr->file, expr->line, expr->column);
         }
         if (!type_is_bool(rtype)) {
            error("[%s:%d.%d] operator{} must return bool", expr->file, expr->line, expr->column);
         }
         tmp = (ContextEntry){ .name = "$tmp", .type = rtype, .declarator = rdecl, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = rsize };
         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", rsize & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         if (!compile_call_expr_to_slot(call, ctx, &tmp)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", rsize & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         emit(&es_code, "    lda #0\n");
         for (int i = 0; i < rsize; i++) {
            emit(&es_code, "    ldy #%d\n", tmp.offset + i);
            emit(&es_code, "    ora (fp),y\n");
         }
         emit(&es_code, "    sta arg1\n");
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", rsize & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         emit(&es_code, "    lda arg1\n");
         emit(&es_code, "    beq %s\n", false_label);
         return true;
      }
   }

   if (expr->count == 2 &&
       (!strcmp(expr->name, "==") || !strcmp(expr->name, "!=") ||
        !strcmp(expr->name, "<")  || !strcmp(expr->name, ">")  ||
        !strcmp(expr->name, "<=") || !strcmp(expr->name, ">="))) {
      const ASTNode *lhs_type = expr_value_type(expr->children[0], ctx);
      const ASTNode *rhs_type = expr_value_type(expr->children[1], ctx);
      const ASTNode *type = promoted_integer_type_for_binary(lhs_type, rhs_type, expr);
      int size;
      int compare_size;
      ContextEntry lhs;
      ContextEntry rhs;
      const char *helper = NULL;
      bool invert = false;
      bool is_signed;

      if (!type) {
         type = lhs_type ? lhs_type : rhs_type;
      }
      size = type ? type_size_from_node(type) : 0;
      if (size <= 0) {
         size = expr_value_size(expr->children[0], ctx);
      }
      if (size <= 0) {
         size = expr_value_size(expr->children[1], ctx);
      }
      if (size <= 0) {
         size = 1;
      }
      compare_size = size * 2;
      lhs = (ContextEntry){ .name = "$lhs", .type = type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = size };
      rhs = (ContextEntry){ .name = "$rhs", .type = type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals + size, .size = size };
      is_signed = type_is_signed_integer(type);

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
         size = 1;
      }
      tmp = (ContextEntry){ .name = "$tmp", .type = type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = size };

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
   //ASTNode *expression = node->children[node->count - 1];
   int size            = declarator_storage_size(type, declarator);
   ContextEntry *entry = (ContextEntry *) set_get(ctx->vars, name);

   if (entry != NULL) {
      return;
   }

   if (has_modifier(modifiers, "static")) {
      ctx_static(ctx, type, name);
      entry = (ContextEntry *) set_get(ctx->vars, name);
   }
   else if (modifiers_imply_zeropage(modifiers)) {
      ctx_zeropage(ctx, type, name);
      entry = (ContextEntry *) set_get(ctx->vars, name);
   }
   else {
      ctx_push(ctx, type, name);
      entry = (ContextEntry *) set_get(ctx->vars, name);
   }

   if (entry != NULL) {
      entry->size = size;
      entry->declarator = declarator;
      if (!has_modifier(modifiers, "static") && !modifiers_imply_zeropage(modifiers)) {
         ctx_resize_last_push(ctx, type, declarator, name);
      }
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

static bool init_const_truthy(const InitConstValue *value) {
   if (!value) {
      return false;
   }
   switch (value->kind) {
      case INIT_CONST_INT:
         return value->i != 0;
      case INIT_CONST_FLOAT:
         return value->f != 0.0;
      case INIT_CONST_ADDRESS:
         return value->symbol != NULL || value->addend != 0;
      default:
         break;
   }
   return false;
}

static bool eval_constant_initializer_expr(ASTNode *expr, InitConstValue *out) {
   InitConstValue lhs = {0};
   InitConstValue rhs = {0};

   if (!out) {
      return false;
   }
   memset(out, 0, sizeof(*out));
   expr = (ASTNode *) unwrap_expr_node(expr);
   if (!expr || is_empty(expr)) {
      return false;
   }

   if (!strcmp(expr->name, "comma_expr") && expr->count > 0) {
      return eval_constant_initializer_expr(expr->children[expr->count - 1], out);
   }

   if (expr->kind == AST_INTEGER) {
      out->kind = INIT_CONST_INT;
      out->i = parse_int(expr->strval);
      return true;
   }

   if (expr->kind == AST_FLOAT) {
      out->kind = INIT_CONST_FLOAT;
      out->f = parse_float(expr->strval);
      return true;
   }

   if (!strcmp(expr->name, "?:") && expr->count >= 3) {
      InitConstValue cond = {0};
      if (!eval_constant_initializer_expr(expr->children[0], &cond)) {
         return false;
      }
      if (init_const_truthy(&cond)) {
         return eval_constant_initializer_expr(expr->children[1], out);
      }
      return eval_constant_initializer_expr(expr->children[2], out);
   }

   if (expr->count == 1) {
      ASTNode *child = expr->children[0];
      if (!strcmp(expr->name, "+")) {
         return eval_constant_initializer_expr(child, out);
      }
      if (!strcmp(expr->name, "-")) {
         if (!eval_constant_initializer_expr(child, &lhs)) {
            return false;
         }
         if (lhs.kind == INIT_CONST_INT) {
            out->kind = INIT_CONST_INT;
            out->i = -lhs.i;
            return true;
         }
         if (lhs.kind == INIT_CONST_FLOAT) {
            out->kind = INIT_CONST_FLOAT;
            out->f = -lhs.f;
            return true;
         }
         return false;
      }
      if (!strcmp(expr->name, "~")) {
         if (!eval_constant_initializer_expr(child, &lhs) || lhs.kind != INIT_CONST_INT) {
            return false;
         }
         out->kind = INIT_CONST_INT;
         out->i = ~lhs.i;
         return true;
      }
      if (!strcmp(expr->name, "!")) {
         if (!eval_constant_initializer_expr(child, &lhs)) {
            return false;
         }
         out->kind = INIT_CONST_INT;
         out->i = init_const_truthy(&lhs) ? 0 : 1;
         return true;
      }
      if (!strcmp(expr->name, "&")) {
         ASTNode *inner = (ASTNode *) unwrap_expr_node(child);
         LValueRef lv;
         if (inner && !strcmp(inner->name, "lvalue") && resolve_lvalue(NULL, inner, &lv) && !lv.indirect) {
            static char symbuf[512];
            if (!entry_symbol_name(NULL, &(ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_zeropage = lv.is_zeropage, .is_global = lv.is_global, .offset = lv.offset, .size = lv.size }, symbuf, sizeof(symbuf))) {
               return false;
            }
            out->kind = INIT_CONST_ADDRESS;
            out->symbol = strdup(symbuf);
            out->addend = lv.offset + lv.ptr_adjust;
            return true;
         }
         if (inner && inner->kind == AST_IDENTIFIER) {
            const ASTNode *fn = resolve_function_call_target(inner->strval, NULL, NULL);
            char sym[512];
            if (fn && function_symbol_name(fn, inner->strval, sym, sizeof(sym))) {
               out->kind = INIT_CONST_ADDRESS;
               out->symbol = strdup(sym);
               out->addend = 0;
               return true;
            }
         }
         return false;
      }
   }

   if (expr->count == 2) {
      if (!eval_constant_initializer_expr(expr->children[0], &lhs) ||
          !eval_constant_initializer_expr(expr->children[1], &rhs)) {
         return false;
      }

      if (!strcmp(expr->name, "+") || !strcmp(expr->name, "-")) {
         bool add = !strcmp(expr->name, "+");
         if (lhs.kind == INIT_CONST_ADDRESS && rhs.kind == INIT_CONST_INT) {
            out->kind = INIT_CONST_ADDRESS;
            out->symbol = lhs.symbol;
            out->addend = lhs.addend + (add ? rhs.i : -rhs.i);
            return true;
         }
         if (lhs.kind == INIT_CONST_INT && rhs.kind == INIT_CONST_ADDRESS && add) {
            out->kind = INIT_CONST_ADDRESS;
            out->symbol = rhs.symbol;
            out->addend = rhs.addend + lhs.i;
            return true;
         }
         if (lhs.kind == INIT_CONST_FLOAT || rhs.kind == INIT_CONST_FLOAT) {
            double a = (lhs.kind == INIT_CONST_FLOAT) ? lhs.f : (double) lhs.i;
            double b = (rhs.kind == INIT_CONST_FLOAT) ? rhs.f : (double) rhs.i;
            out->kind = INIT_CONST_FLOAT;
            out->f = add ? (a + b) : (a - b);
            return true;
         }
         if (lhs.kind == INIT_CONST_INT && rhs.kind == INIT_CONST_INT) {
            out->kind = INIT_CONST_INT;
            out->i = add ? (lhs.i + rhs.i) : (lhs.i - rhs.i);
            return true;
         }
         return false;
      }

      if (!strcmp(expr->name, "*") || !strcmp(expr->name, "/") || !strcmp(expr->name, "%")) {
         if (lhs.kind == INIT_CONST_FLOAT || rhs.kind == INIT_CONST_FLOAT) {
            double a = (lhs.kind == INIT_CONST_FLOAT) ? lhs.f : (double) lhs.i;
            double b = (rhs.kind == INIT_CONST_FLOAT) ? rhs.f : (double) rhs.i;
            if ((!strcmp(expr->name, "/") || !strcmp(expr->name, "%")) && b == 0.0) {
               return false;
            }
            if (!strcmp(expr->name, "%")) {
               return false;
            }
            out->kind = INIT_CONST_FLOAT;
            out->f = !strcmp(expr->name, "*") ? (a * b) : (a / b);
            return true;
         }
         if (lhs.kind == INIT_CONST_INT && rhs.kind == INIT_CONST_INT) {
            if ((!strcmp(expr->name, "/") || !strcmp(expr->name, "%")) && rhs.i == 0) {
               return false;
            }
            out->kind = INIT_CONST_INT;
            if (!strcmp(expr->name, "*")) out->i = lhs.i * rhs.i;
            else if (!strcmp(expr->name, "/")) out->i = lhs.i / rhs.i;
            else out->i = lhs.i % rhs.i;
            return true;
         }
         return false;
      }

      if (!strcmp(expr->name, "<<") || !strcmp(expr->name, ">>") ||
          !strcmp(expr->name, "&") || !strcmp(expr->name, "|") || !strcmp(expr->name, "^")) {
         if (lhs.kind != INIT_CONST_INT || rhs.kind != INIT_CONST_INT) {
            return false;
         }
         out->kind = INIT_CONST_INT;
         if (!strcmp(expr->name, "<<")) out->i = lhs.i << rhs.i;
         else if (!strcmp(expr->name, ">>")) out->i = lhs.i >> rhs.i;
         else if (!strcmp(expr->name, "&")) out->i = lhs.i & rhs.i;
         else if (!strcmp(expr->name, "|")) out->i = lhs.i | rhs.i;
         else out->i = lhs.i ^ rhs.i;
         return true;
      }

      if (!strcmp(expr->name, "&&") || !strcmp(expr->name, "||")) {
         bool a = init_const_truthy(&lhs);
         bool b = init_const_truthy(&rhs);
         out->kind = INIT_CONST_INT;
         out->i = !strcmp(expr->name, "&&") ? (a && b) : (a || b);
         return true;
      }

      if (!strcmp(expr->name, "==") || !strcmp(expr->name, "!=") ||
          !strcmp(expr->name, "<") || !strcmp(expr->name, ">") ||
          !strcmp(expr->name, "<=") || !strcmp(expr->name, ">=")) {
         bool result;
         if (lhs.kind == INIT_CONST_ADDRESS || rhs.kind == INIT_CONST_ADDRESS) {
            if (lhs.kind != INIT_CONST_ADDRESS || rhs.kind != INIT_CONST_ADDRESS) {
               return false;
            }
            if ((lhs.symbol == NULL) != (rhs.symbol == NULL)) {
               return false;
            }
            if (lhs.symbol && rhs.symbol && strcmp(lhs.symbol, rhs.symbol)) {
               return false;
            }
            if (!strcmp(expr->name, "==")) result = lhs.addend == rhs.addend;
            else if (!strcmp(expr->name, "!=")) result = lhs.addend != rhs.addend;
            else if (!strcmp(expr->name, "<")) result = lhs.addend < rhs.addend;
            else if (!strcmp(expr->name, ">")) result = lhs.addend > rhs.addend;
            else if (!strcmp(expr->name, "<=")) result = lhs.addend <= rhs.addend;
            else result = lhs.addend >= rhs.addend;
            out->kind = INIT_CONST_INT;
            out->i = result ? 1 : 0;
            return true;
         }
         if (lhs.kind == INIT_CONST_FLOAT || rhs.kind == INIT_CONST_FLOAT) {
            double a = (lhs.kind == INIT_CONST_FLOAT) ? lhs.f : (double) lhs.i;
            double b = (rhs.kind == INIT_CONST_FLOAT) ? rhs.f : (double) rhs.i;
            if (!strcmp(expr->name, "==")) result = a == b;
            else if (!strcmp(expr->name, "!=")) result = a != b;
            else if (!strcmp(expr->name, "<")) result = a < b;
            else if (!strcmp(expr->name, ">")) result = a > b;
            else if (!strcmp(expr->name, "<=")) result = a <= b;
            else result = a >= b;
            out->kind = INIT_CONST_INT;
            out->i = result ? 1 : 0;
            return true;
         }
         if (lhs.kind == INIT_CONST_INT && rhs.kind == INIT_CONST_INT) {
            if (!strcmp(expr->name, "==")) result = lhs.i == rhs.i;
            else if (!strcmp(expr->name, "!=")) result = lhs.i != rhs.i;
            else if (!strcmp(expr->name, "<")) result = lhs.i < rhs.i;
            else if (!strcmp(expr->name, ">")) result = lhs.i > rhs.i;
            else if (!strcmp(expr->name, "<=")) result = lhs.i <= rhs.i;
            else result = lhs.i >= rhs.i;
            out->kind = INIT_CONST_INT;
            out->i = result ? 1 : 0;
            return true;
         }
         return false;
      }
   }

   return false;
}

static bool encode_integer_initializer_value(long long value, unsigned char *buf, int size, const ASTNode *type) {
   char tmp[128];
   unsigned long long mag;

   if (!buf || size < 0 || !type) {
      return false;
   }

   mag = value < 0 ? (unsigned long long) (-(value + 1)) + 1ULL : (unsigned long long) value;
   snprintf(tmp, sizeof(tmp), "%llu", mag);
   if (has_flag(type_name_from_node(type), "$endian:big")) {
      make_be_int(tmp, buf, size);
      if (value < 0) {
         negate_be_int(buf, size);
      }
   }
   else {
      make_le_int(tmp, buf, size);
      if (value < 0) {
         negate_le_int(buf, size);
      }
   }
   return true;
}

static bool encode_float_initializer_value(double value, unsigned char *buf, int size, const ASTNode *type) {
   char tmp[256];
   if (!buf || size < 0 || !type) {
      return false;
   }
   snprintf(tmp, sizeof(tmp), "%la", value);
   if (has_flag(type_name_from_node(type), "$endian:big")) {
      make_be_float(tmp, buf, size);
   }
   else {
      make_le_float(tmp, buf, size);
   }
   return true;
}

static bool emit_symbol_address_initializer(EmitSink *es, int size, const ASTNode *type, const char *symbol, long long addend) {
   if (!es || !type || !symbol || size <= 0) {
      return false;
   }
   if (size != 2) {
      return false;
   }
   if (has_flag(type_name_from_node(type), "$endian:big")) {
      emit(es, "\t.byte >(%s%+lld), <(%s%+lld)\n", symbol, addend, symbol, addend);
   }
   else {
      emit(es, "\t.byte <(%s%+lld), >(%s%+lld)\n", symbol, addend, symbol, addend);
   }
   return true;
}

static void emit_initializer_bytes_line(EmitSink *es, const unsigned char *bytes, int size) {
   emit(es, "\t.byte $%02x", bytes[0]);
   for (int i = 1; i < size; i++) {
      emit(es, ", $%02x", bytes[i]);
   }
   emit(es, "\n");
}

static bool emit_global_initializer(EmitSink *es, const ASTNode *type, const ASTNode *declarator, ASTNode *expression, int size) {
   ASTNode *uexpr = (ASTNode *) unwrap_expr_node(expression);
   unsigned char *bytes;
   InitConstValue value = {0};

   if (!es || !type || size < 0) {
      return false;
   }

   if (uexpr && uexpr->kind == AST_STRING && declarator_pointer_depth(declarator) > 0) {
      const char *label = remember_string_literal(uexpr->strval);
      return emit_symbol_address_initializer(es, size, type, label, 0);
   }

   bytes = (unsigned char *) calloc(size ? size : 1, sizeof(unsigned char));
   if (!bytes) {
      return false;
   }

   if (build_initializer_bytes(bytes, size, 0, uexpr ? uexpr : expression, type, declarator, size)) {
      emit_initializer_bytes_line(es, bytes, size);
      free(bytes);
      return true;
   }
   free(bytes);

   if (!initializer_is_list(uexpr ? uexpr : expression) && eval_constant_initializer_expr(uexpr ? uexpr : expression, &value)) {
      if (value.kind == INIT_CONST_ADDRESS) {
         return emit_symbol_address_initializer(es, size, type, value.symbol, value.addend);
      }
   }

   return false;
}

static bool compile_initializer_to_fp(const ASTNode *init, Context *ctx, const ASTNode *type, const ASTNode *declarator, int base_offset, int total_size) {
   const ASTNode *uinit = unwrap_expr_node((ASTNode *) init);
   int size = scalar_storage_size(type, declarator, total_size);

   if (!uinit || is_empty(uinit)) {
      return true;
   }

   if (uinit->kind == AST_STRING) {
      return emit_string_initializer_to_fp(type, declarator, base_offset, size, uinit->strval);
   }

   if (!initializer_is_list(uinit)) {
      ContextEntry dst = { .name = "$init", .type = type, .declarator = declarator, .is_static = false, .is_zeropage = false, .is_global = false, .offset = base_offset, .size = size };
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

   if (uinit->kind == AST_STRING) {
      return emit_string_initializer_bytes(buf, buf_size, base_offset, type, declarator, size, uinit->strval);
   }

   if (!initializer_is_list(uinit)) {
      InitConstValue value = {0};
      if (!eval_constant_initializer_expr((ASTNode *) uinit, &value)) {
         return false;
      }
      if (value.kind == INIT_CONST_FLOAT || has_flag(type_name_from_node(type), "$float")) {
         if (value.kind != INIT_CONST_FLOAT && value.kind != INIT_CONST_INT) {
            return false;
         }
         return encode_float_initializer_value(value.kind == INIT_CONST_FLOAT ? value.f : (double) value.i,
               buf + base_offset, size, type);
      }
      if (value.kind != INIT_CONST_INT) {
         return false;
      }
      return encode_integer_initializer_value(value.i, buf + base_offset, size, type);
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
      else if (!strcmp(stmt->name, "statement_list")) {
         predeclare_statement_list(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "if_stmt")) {
         predeclare_statement_list(stmt->children[1], ctx);
         if (stmt->count > 2) {
            predeclare_statement_list(stmt->children[2], ctx);
         }
      }
      else if (!strcmp(stmt->name, "while_stmt")) {
         predeclare_statement_list(stmt->children[1], ctx);
      }
      else if (!strcmp(stmt->name, "for_stmt")) {
         if (stmt->count > 3) {
            predeclare_statement_list(stmt->children[3], ctx);
         }
      }
      else if (!strcmp(stmt->name, "do_stmt")) {
         predeclare_statement_list(stmt->children[0], ctx);
      }
      else if (!strcmp(stmt->name, "label_stmt")) {
         if (stmt->count > 1) {
            predeclare_statement_list(stmt->children[1], ctx);
         }
      }
      else if (!strcmp(stmt->name, "switch_stmt")) {
         if (stmt->count > 1) {
            predeclare_statement_list(stmt->children[1], ctx);
         }
      }
   }
}

static void compile_local_decl_item(ASTNode *node, Context *ctx) {
   ASTNode *modifiers  = node->children[0];
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

   if (entry == NULL) {
      warning("[%s:%d.%d] local declaration for '%s' not compiled yet", node->file, node->line, node->column, name);
      return;
   }

   if (is_empty(expression) && has_modifier(modifiers, "const")) {
      error("[%s:%d.%d] 'const' missing initializer", node->file, node->line, node->column);
   }

   if (!entry->is_static && !entry->is_zeropage) {
      if (!is_empty(expression)) {
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
      return;
   }

   {
      char sym[256];
      EmitSink *sink;
      if (!entry_symbol_name(ctx, entry, sym, sizeof(sym))) {
         warning("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
         return;
      }
      if (is_empty(expression)) {
         sink = entry->is_zeropage ? &es_zp : &es_bss;
         emit(sink, "%s:\n", sym);
         emit(sink, "	.res %d\n", size);
         return;
      }

      sink = has_modifier(modifiers, "const") ? &es_rodata : (entry->is_zeropage ? &es_zpdata : &es_data);
      emit(sink, "%s:\n", sym);
      if (!emit_global_initializer(sink, type, declarator, expression, size)) {
         warning("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
         emit(sink, "	.res %d\n", size);
      }
      return;
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
      size = 1;
   }
   compare_size = size * 2;
   lhs = (ContextEntry){ .name = "$lhs", .type = type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = size };
   rhs = (ContextEntry){ .name = "$rhs", .type = type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals + size, .size = size };
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

   if (!node || strcmp(node->name, "assign_expr") || node->count != 3) {
      const ASTNode *type = expr_value_type(node, ctx);
      int size = expr_value_size(node, ctx);
      if (size <= 0) {
         size = 1;
      }
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      if (!compile_expr_to_slot(node, ctx, &(ContextEntry){ .name = "$tmp", .type = type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = size })) {
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
   dst_store = (ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_zeropage = lv.is_zeropage, .is_global = lv.is_global, .offset = lv.offset, .size = lv.size };
   dst = &dst_store;


   if (!op || !strcmp(op, ":=")) {
      if (dst->is_static || dst->is_zeropage || dst->is_global) {
         char sym[256];
         if (!entry_symbol_name(ctx, dst, sym, sizeof(sym))) {
            warning("[%s:%d.%d] assignment target not compiled yet", node->file, node->line, node->column);
            return;
         }
         if (!compile_expr_to_slot(rhs, ctx, &(ContextEntry){ .name = "$tmp", .type = dst->type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = dst->size })) {
            warning("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
            return;
         }
         emit_copy_fp_to_symbol(sym, ctx->locals, dst->size);
         return;
      }
      if (lv.indirect) {
         int tmp_size = dst->size > 0 ? dst->size : expr_value_size(rhs, ctx);
         if (tmp_size <= 0) {
            tmp_size = 1;
         }
         ContextEntry tmp = { .name = "$tmp", .type = dst->type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = tmp_size };
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
      bool dst_symbol = (dst->is_static || dst->is_zeropage || dst->is_global) && entry_symbol_name(ctx, dst, dst_sym, sizeof(dst_sym));
      bool scaled_pointer_assign = dst->declarator && declarator_pointer_depth(dst->declarator) > 0 && (!strcmp(op, "+=") || !strcmp(op, "-="));
      const ASTNode *rhs_type = expr_value_type(rhs, ctx);
      const ASTNode *work_type = NULL;
      const ASTNode *rhs_slot_type = NULL;
      int work_size = 0;
      int rhs_work_size = 0;
      int tmp_total;
      int lhs_tmp_offset;
      int rhs_tmp_offset;
      int aux_offset;
      int factor_offset = 0;
      int scaled_rhs_offset = 0;
      int rhs_value_offset;
      int store_offset = 0;
      bool need_store_tmp = false;
      int pointer_scale = 1;
      ContextEntry rhs_tmp;
      const char *helper = NULL;

      if (scaled_pointer_assign) {
         work_type = dst->type;
         rhs_slot_type = rhs_type;
         work_size = dst->size;
         pointer_scale = declarator_first_element_size(dst->type, dst->declarator);
         if (pointer_scale <= 0) {
            pointer_scale = 1;
         }
      }
      else if (!strcmp(op, "<<=") || !strcmp(op, ">>=")) {
         work_type = dst->type ? dst->type : rhs_type;
         rhs_slot_type = rhs_type ? rhs_type : work_type;
         work_size = work_type ? type_size_from_node(work_type) : 0;
         rhs_work_size = rhs_slot_type ? type_size_from_node(rhs_slot_type) : 0;
      }
      else {
         work_type = promoted_integer_type_for_binary(dst->type, rhs_type, node);
         if (!work_type) {
            work_type = dst->type ? dst->type : rhs_type;
         }
         rhs_slot_type = work_type;
         work_size = work_type ? type_size_from_node(work_type) : 0;
      }

      if (work_size <= 0) {
         work_size = dst->size;
      }
      if (work_size <= 0) {
         work_size = expr_value_size(rhs, ctx);
      }
      if (work_size <= 0) {
         work_size = 1;
      }
      if (!work_type) {
         work_type = dst->type;
      }
      if (!rhs_slot_type) {
         rhs_slot_type = work_type;
      }
      if (rhs_work_size <= 0) {
         rhs_work_size = rhs_slot_type ? type_size_from_node(rhs_slot_type) : 0;
      }
      if (rhs_work_size <= 0) {
         rhs_work_size = expr_value_size(rhs, ctx);
      }
      if (rhs_work_size <= 0) {
         rhs_work_size = work_size;
      }
      if (rhs_work_size <= 0) {
         rhs_work_size = 1;
      }

      tmp_total = work_size + rhs_work_size;
      lhs_tmp_offset = ctx->locals;
      rhs_tmp_offset = lhs_tmp_offset + work_size;
      aux_offset = rhs_tmp_offset + rhs_work_size;
      rhs_value_offset = rhs_tmp_offset;

      if (!strcmp(op, "*=") || !strcmp(op, "/=") || !strcmp(op, "%=")) {
         tmp_total += work_size * 2;
      }
      else if (!strcmp(op, "<<=") || !strcmp(op, ">>=")) {
         tmp_total += work_size;
      }

      if (scaled_pointer_assign && pointer_scale != 1) {
         factor_offset = aux_offset;
         scaled_rhs_offset = factor_offset + work_size;
         rhs_value_offset = scaled_rhs_offset;
         tmp_total += work_size * 2;
      }

      need_store_tmp = dst_symbol || lv.indirect;
      if (need_store_tmp) {
         store_offset = ctx->locals + tmp_total;
         tmp_total += dst->size;
      }

      rhs_tmp = (ContextEntry){ .name = "$rhs_tmp", .type = rhs_slot_type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = rhs_tmp_offset, .size = rhs_work_size };

      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");

      if (dst_symbol) {
         emit_copy_symbol_to_fp_convert(lhs_tmp_offset, work_size, work_type, dst_sym, dst->size, dst->type);
      }
      else if (lv.indirect) {
         int lhs_src_size = dst->size < work_size ? dst->size : work_size;
         emit_copy_lvalue_to_fp(lhs_tmp_offset, &lv, lhs_src_size);
         emit_copy_fp_to_fp_convert(lhs_tmp_offset, work_size, work_type, lhs_tmp_offset, lhs_src_size, dst->type);
      }
      else {
         emit_copy_fp_to_fp_convert(lhs_tmp_offset, work_size, work_type, dst->offset, dst->size, dst->type);
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
         unsigned char *factor_bytes = (unsigned char *) calloc(work_size ? work_size : 1, sizeof(unsigned char));
         char scaled_buf[64];
         const ASTNode *factor_type = rhs_slot_type ? rhs_slot_type : work_type;
         if (!factor_bytes) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return;
         }
         snprintf(scaled_buf, sizeof(scaled_buf), "%d", pointer_scale);
         if (factor_type && has_flag(type_name_from_node(factor_type), "$endian:big")) {
            make_be_int(scaled_buf, factor_bytes, work_size);
         }
         else {
            make_le_int(scaled_buf, factor_bytes, work_size);
         }
         emit_store_immediate_to_fp(factor_offset, factor_bytes, work_size);
         free(factor_bytes);
         emit_runtime_binary_fp_fp("mulN", scaled_rhs_offset, rhs_tmp_offset, factor_offset, work_size);
      }

      if (!strcmp(op, "+=")) {
         emit_add_fp_to_fp(work_type, lhs_tmp_offset, rhs_value_offset, work_size);
      }
      else if (!strcmp(op, "-=")) {
         emit_sub_fp_from_fp(work_type, lhs_tmp_offset, rhs_value_offset, work_size);
      }
      else if (!strcmp(op, "&=")) {
         emit_runtime_binary_fp_fp("bit_andN", lhs_tmp_offset, lhs_tmp_offset, rhs_tmp_offset, work_size);
      }
      else if (!strcmp(op, "|=")) {
         emit_runtime_binary_fp_fp("bit_orN", lhs_tmp_offset, lhs_tmp_offset, rhs_tmp_offset, work_size);
      }
      else if (!strcmp(op, "^=")) {
         emit_runtime_binary_fp_fp("bit_xorN", lhs_tmp_offset, lhs_tmp_offset, rhs_tmp_offset, work_size);
      }
      else if (!strcmp(op, "*=")) {
         emit_runtime_binary_fp_fp("mulN", aux_offset, lhs_tmp_offset, rhs_tmp_offset, work_size);
         emit_copy_fp_to_fp(lhs_tmp_offset, aux_offset, work_size);
      }
      else if (!strcmp(op, "/=") || !strcmp(op, "%=")) {
         int quo_offset = aux_offset;
         int rem_offset = aux_offset + work_size;
         emit_prepare_fp_ptr(0, lhs_tmp_offset);
         emit_prepare_fp_ptr(1, rhs_tmp_offset);
         emit_prepare_fp_ptr(2, quo_offset);
         emit_prepare_fp_ptr(3, rem_offset);
         emit(&es_code, "    lda #$%02x\n", work_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         remember_runtime_import("divN");
         emit(&es_code, "    jsr _divN\n");
         emit_copy_fp_to_fp(lhs_tmp_offset, !strcmp(op, "/=") ? quo_offset : rem_offset, work_size);
      }
      else if (!strcmp(op, "<<=") || !strcmp(op, ">>=")) {
         helper = !strcmp(op, "<<=") ? "lslN" : (work_type && has_flag(type_name_from_node(work_type), "$signed") ? "asrN" : "lsrN");
         emit_runtime_shift_fp(helper, lhs_tmp_offset, aux_offset, rhs_tmp_offset, work_size);
      }
      else {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         warning("[%s:%d.%d] expression '%s' not compiled yet", node->file, node->line, node->column, op);
         return;
      }

      if (need_store_tmp) {
         emit_copy_fp_to_fp_convert(store_offset, dst->size, dst->type, lhs_tmp_offset, work_size, work_type);
         if (dst_symbol) {
            emit_copy_fp_to_symbol(dst_sym, store_offset, dst->size);
         }
         else {
            emit_copy_fp_to_lvalue(&lv, store_offset, dst->size);
         }
      }
      else {
         emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, lhs_tmp_offset, work_size, work_type);
      }

      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      return;
   }

   warning("[%s:%d.%d] expression '%s' not compiled yet", node->file, node->line, node->column, op ? op : "?");
}


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
   char sym[256];

   remember_function(node, name);
   if (!function_symbol_name(node, name, sym, sizeof(sym))) {
      error("[%s:%d.%d] could not mangle function '%s'", node->file, node->line, node->column, name);
   }

   if (!has_modifier(modifiers, "static")) {
      emit(&es_export, ".export _%s\n", sym);
   }

   Context ctx;
   ctx.name = strdup(sym);
   ctx.locals = 0;
   ctx.params = 0;
   ctx.vars = new_set();
   ctx.break_label = NULL;
   ctx.continue_label = NULL;
   build_function_context(node, &ctx);

   if (!is_empty(body) && !strcmp(body->name, "statement_list")) {
      predeclare_statement_list(body, &ctx);
   }

   emit(&es_code, ".proc _%s\n", sym);
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
   ASTNode *uexpr;

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
   bool is_zeropage  = modifiers_imply_zeropage(modifiers);
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

      if (is_zeropage) {
         emit(&es_import, ".zpimport _%s\n", name);
      }
      else {
         emit(&es_import, ".import _%s\n", name);
      }
      return;
   }

   if (!is_static) {
      if (is_zeropage) {
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
      if (is_zeropage) {
         emit(&es_zp, "_%s:\n", name);
         emit(&es_zp, "\t.res %d\n", size);
      }
      else {
         emit(&es_bss, "_%s:\n", name);
         emit(&es_bss, "\t.res %d\n", size);
      }
      return;
   }

   EmitSink *es = is_const ? &es_rodata : (is_zeropage ? &es_zpdata : &es_data);
   emit(es, "_%s:\n", name);
   uexpr = (ASTNode *) unwrap_expr_node(expression);

   if (!emit_global_initializer(es, type, declarator, uexpr ? uexpr : expression, size)) {
      warning("[%s:%d.%d] complex global initializer for '%s' not implemented yet",
            node->file, node->line, node->column, name);
      emit(es, "	.res %d, $00\n", size);
   }

}

static void remember_function(const ASTNode *node, const char *name) {
   if (is_operator_function_name(name)) {
      remember_operator_overload(node, name);
      return;
   }

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
   char sym[256];

   remember_function(node, name);

   if (has_modifier(modifiers, "extern") && !has_modifier(modifiers, "static")) {
      if (!function_symbol_name(node, name, sym, sizeof(sym))) {
         error("[%s:%d.%d] could not mangle function '%s'", node->file, node->line, node->column, name);
      }
      remember_symbol_import(sym);
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

   if (!typename_exists("bool")) {
      error("type bool is not defined");
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
   emit(&es_zp,     ".segment \"ZEROPAGE\"\n");
   emit(&es_zpdata, ".segment \"ZEROPAGE\"\n");
   emit(&es_import, "; imports\n");
   emit(&es_export, "; exports\n");

   compile(root);

   emit_print(&es_header);
   printf("\n");

   emit_print(&es_import);
   printf("\n");

   emit_print(&es_export);
   printf("\n");

   emit_print(&es_zp);
   printf("\n");

   emit_print(&es_zpdata);
   printf("\n");

   emit_print(&es_bss);
   printf("\n");

   emit_print(&es_data);
   printf("\n");

   emit_print(&es_rodata);
   printf("\n");

   emit_print(&es_code);
}
