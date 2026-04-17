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
#include "compile_expr.h"
#include "compile_expr_info.h"
#include "compile_function.h"
#include "compile_init.h"
#include "compile_internal.h"
#include "compile_lvalue.h"
#include "compile_overload.h"
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
#include "xray.h"
#include "lextern.h"

static ASTNode *make_synthetic_call_expr(ASTNode *origin, const char *callee_name, ASTNode *args[], int argc);
static ASTNode *make_synthetic_incdec_operand(ASTNode *origin);
static int cast_expr_target_size(const ASTNode *expr);
static bool compile_indirect_call_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst,
                                               ASTNode *callee, ASTNode *args,
                                               const ASTNode *ret_type,
                                               const ASTNode *callable_decl);
static int expr_byte_index(const ASTNode *type, int size, int i);

static bool expr_eligible_for_weak_builtin_operator(ASTNode *expr, Context *ctx,
                                                    const char **opname_out,
                                                    const ASTNode **ret_type_out,
                                                    const ASTNode **ret_decl_out,
                                                    int *ret_size_out,
                                                    int *arg_count_out,
                                                    ASTNode **arg_exprs_out,
                                                    const ASTNode **arg_types_out,
                                                    const ASTNode **arg_decls_out) {
   (void) expr;
   (void) ctx;
   (void) opname_out;
   (void) ret_type_out;
   (void) ret_decl_out;
   (void) ret_size_out;
   (void) arg_count_out;
   (void) arg_exprs_out;
   (void) arg_types_out;
   (void) arg_decls_out;

   /* Exact visible overloads still resolve first. Otherwise same-type operators
    * now fall back to generic lowering unless the type opted into $exactops,
    * which is enforced separately at the call site. */
   return false;
}

static bool weak_builtin_operator_symbol_name(const char *opname, int arg_count,
                                              const ASTNode **arg_types,
                                              const ASTNode **arg_decls,
                                              char *buf, size_t bufsize) {
   if (!opname || !buf || bufsize == 0) {
      return false;
   }
   buf[0] = 0;
   append_mangled_text(buf, bufsize, opname);
   for (int i = 0; i < arg_count; i++) {
      char tmp[64];
      strncat(buf, "__", bufsize - strlen(buf) - 1);
      append_mangled_text(buf, bufsize, type_name_from_node(arg_types[i]));
      snprintf(tmp, sizeof(tmp), "_p%d_a%d", declarator_pointer_depth(arg_decls ? arg_decls[i] : NULL), declarator_array_count(arg_decls ? arg_decls[i] : NULL));
      strncat(buf, tmp, bufsize - strlen(buf) - 1);
   }
   if (arg_count == 0) {
      strncat(buf, "__void", bufsize - strlen(buf) - 1);
   }
   {
      char raw[256];
      snprintf(raw, sizeof(raw), "%s", buf);
      return format_user_asm_symbol(raw, buf, bufsize);
   }
}

static bool compile_weak_builtin_operator_call_to_slot(const char *symbol,
                                                       const ASTNode *ret_type,
                                                       const ASTNode *ret_decl,
                                                       int ret_size,
                                                       int arg_count,
                                                       ASTNode **arg_exprs,
                                                       const ASTNode **arg_types,
                                                       const ASTNode **arg_decls,
                                                       Context *ctx,
                                                       ContextEntry *dst) {
   int arg_total = 0;
   int base_locals = ctx ? ctx->locals : 0;
   int arg_offset;
   int call_size;
   if (!symbol || !ret_type || !dst) {
      return false;
   }
   if (ret_size <= 0) {
      ret_size = declarator_value_size(ret_type, ret_decl);
   }
   if (ret_size <= 0) {
      ret_size = type_size_from_node(ret_type);
   }
   if (ret_size < 0) {
      ret_size = 0;
   }
   for (int i = 0; i < arg_count; i++) {
      int psz = declarator_value_size(arg_types[i], arg_decls ? arg_decls[i] : NULL);
      if (psz <= 0) {
         psz = type_size_from_node(arg_types[i]);
      }
      if (psz < 0) {
         return false;
      }
      arg_total += psz;
   }
   call_size = ret_size + arg_total;
   if (call_size > 0) {
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
   }
   if (ctx) {
      ctx->locals = base_locals + call_size;
   }

   arg_offset = ret_size + arg_total;
   for (int i = 0; i < arg_count; i++) {
      ContextEntry tmp;
      int psz = declarator_value_size(arg_types[i], arg_decls ? arg_decls[i] : NULL);
      if (psz <= 0) {
         psz = type_size_from_node(arg_types[i]);
      }
      arg_offset -= psz;
      tmp = (ContextEntry){ .name = "$arg", .type = arg_types[i], .declarator = arg_decls ? arg_decls[i] : NULL,
                            .is_static = false, .is_zeropage = false, .is_global = false,
                            .offset = base_locals + arg_offset, .size = psz };
      if (!compile_expr_to_slot(arg_exprs[i], ctx, &tmp)) {
         if (ctx) {
            ctx->locals = base_locals;
         }
         if (call_size > 0) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
         }
         return false;
      }
   }

   remember_symbol_import(symbol);
   emit(&es_code, "    lda fp+1\n");
   emit(&es_code, "    pha\n");
   emit(&es_code, "    lda fp\n");
   emit(&es_code, "    pha\n");
   emit(&es_code, "    jsr %s\n", symbol);
   emit(&es_code, "    pla\n");
   emit(&es_code, "    sta fp\n");
   emit(&es_code, "    pla\n");
   emit(&es_code, "    sta fp+1\n");

   if (ctx) {
      ctx->locals = base_locals;
   }
   if (ret_size > 0) {
      emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, base_locals, ret_size, ret_type);
   }
   if (call_size > 0) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
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

bool classify_incdec_lvalue_expr(ASTNode *expr, bool *inc, bool *pre) {
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


bool compile_constant_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst) {
   InitConstValue value = {0};
   unsigned char *bytes;
   (void) ctx;

   if (!dst || !eval_constant_initializer_expr(expr, &value)) {
      return false;
   }

   if (value.kind == INIT_CONST_FLOAT || type_is_float_like(dst->type)) {
      if (value.kind != INIT_CONST_FLOAT && value.kind != INIT_CONST_INT) {
         return false;
      }
      bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
      if (!bytes) {
         error_unreachable("out of memory");
      }
      if (!encode_float_initializer_value(value.kind == INIT_CONST_FLOAT ? value.f : (double) value.i,
                                          bytes, dst->size, dst->type)) {
         free(bytes);
         return false;
      }
      emit_store_immediate_to_fp(dst->offset, bytes, dst->size);
      free(bytes);
      return true;
   }

   if (value.kind != INIT_CONST_INT) {
      return false;
   }

   bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
   if (!bytes) {
      error_unreachable("out of memory");
   }
   if (!encode_init_const_int_value(&value, bytes, dst->size, dst->type)) {
      free(bytes);
      return false;
   }
   emit_store_immediate_to_fp(dst->offset, bytes, dst->size);
   free(bytes);
   return true;
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

void emit_copy_fp_to_fp(int dst_offset, int src_offset, int size) {
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



static int cast_expr_target_size(const ASTNode *expr) {
   const ASTNode *type = cast_expr_target_type(expr);
   const ASTNode *declarator = cast_expr_target_declarator(expr);
   int size;

   if (!type) {
      return 0;
   }

   size = declarator_storage_size(type, declarator);
   if (size <= 0) {
      size = type_size_from_node(type);
   }
   return size;
}

static int sizeof_operand_size(const ASTNode *operand, Context *ctx) {
   operand = unwrap_expr_node(operand);
   if (!operand || is_empty(operand)) {
      return 0;
   }
   if (!strcmp(operand->name, "sizeof_expr") && operand->count > 0) {
      return expr_value_size((ASTNode *) operand->children[0], ctx);
   }
   if (!strcmp(operand->name, "sizeof_type") && operand->count > 0) {
      const ASTNode *cast_type = operand->children[0];
      const ASTNode *specifiers;
      const ASTNode *type;
      const ASTNode *declarator;
      int size;
      if (!cast_type || strcmp(cast_type->name, "cast_type") || cast_type->count < 2) {
         return 0;
      }
      specifiers = cast_type->children[0];
      if (!specifiers || specifiers->count < 2) {
         return 0;
      }
      type = specifiers->children[1];
      declarator = cast_type->children[1];
      size = declarator_storage_size(type, declarator);
      if (size <= 0) {
         size = type_size_from_node(type);
      }
      return size;
   }
   return 0;
}


static int expr_byte_index(const ASTNode *type, int size, int i) {
   if (has_flag(type_name_from_node(type), "$endian:big")) {
      return size - 1 - i;
   }
   return i;
}

void emit_add_immediate_to_fp(const ASTNode *type, int offset, const unsigned char *bytes, int size) {
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

void emit_add_fp_to_fp(const ASTNode *type, int dst_offset, int src_offset, int size) {
   bool helper_is_generic = false;
   const char *helper = int_addsub_helper_name(type, size, false, &helper_is_generic);
   bool dst_direct = dst_offset >= 0 && dst_offset + size <= 256;
   bool src_direct = src_offset >= 0 && src_offset + size <= 256;

   if (helper) {
      if (helper_is_generic) {
         emit_runtime_binary_fp_fp(helper, dst_offset, dst_offset, src_offset, size);
      }
      else {
         emit_runtime_fixed_binary_fp_fp(helper, dst_offset, dst_offset, src_offset);
      }
      return;
   }

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
   bool helper_is_generic = false;
   const char *helper = int_addsub_helper_name(type, size, true, &helper_is_generic);
   bool dst_direct = dst_offset >= 0 && dst_offset + size <= 256;
   bool src_direct = src_offset >= 0 && src_offset + size <= 256;

   if (helper) {
      if (helper_is_generic) {
         emit_runtime_binary_fp_fp(helper, dst_offset, dst_offset, src_offset, size);
      }
      else {
         emit_runtime_fixed_binary_fp_fp(helper, dst_offset, dst_offset, src_offset);
      }
      return;
   }

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


static bool compile_indirect_call_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst,
                                               ASTNode *callee, ASTNode *args,
                                               const ASTNode *ret_type,
                                               const ASTNode *callable_decl) {
   const ASTNode *params = declarator_parameter_list(callable_decl);
   const ASTNode *ret_decl = function_return_declarator_from_callable(callable_decl);
   int arg_count = (args && !is_empty(args)) ? args->count : 0;
   int ret_size = dst ? dst->size : 0;
   int arg_total = 0;
   int ptr_size = get_size("*");
   int len_size = get_size("*");
   int base_locals = ctx ? ctx->locals : 0;
   int callee_tmp_offset;
   int call_size;
   int fixed_params = 0;
   int fixed_stack_total = 0;
   int variadic_total = 0;
   bool variadic = parameter_list_is_variadic(params);
   ContextEntry callee_tmp;

   if (ret_type && dst) {
      ret_size = declarator_value_size(ret_type, ret_decl);
   }
   if (ret_size < 0) {
      ret_size = 0;
   }

   if (params && !is_empty(params)) {
      for (int i = 0; i < params->count; i++) {
         const ASTNode *parameter = params->children[i];
         const ASTNode *ptype = parameter_type(parameter);
         if (!ptype || parameter_is_void(parameter)) {
            continue;
         }
         if (parameter_has_symbol_storage(parameter)) {
            error_user("[%s:%d.%d] indirect call target type cannot use symbol-backed parameters", expr->file, expr->line, expr->column);
         }
         fixed_params++;
         fixed_stack_total += parameter_storage_size(parameter);
      }
      if ((!variadic && fixed_params != arg_count) || (variadic && arg_count < fixed_params)) {
         warning("[%s:%d.%d] indirect call argument count mismatch (%d vs %d)", expr->file, expr->line, expr->column, arg_count, fixed_params);
      }
   }

    if (variadic && args && !is_empty(args)) {
      for (int i = fixed_params; i < arg_count; i++) {
         int actual_size = expr_value_size(args->children[i], ctx);
         if (actual_size <= 0) {
            error_user("[%s:%d.%d] variadic argument %d has no runtime storage", args->children[i]->file, args->children[i]->line, args->children[i]->column, i - fixed_params + 1);
         }
         variadic_total += actual_size;
      }
    }

   arg_total = fixed_stack_total;
   if (variadic) {
      arg_total += variadic_total + ptr_size + len_size;
   }

   callee_tmp_offset = 0;
   call_size = ptr_size + ret_size + arg_total;

   if (call_size > 0) {
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
   }
   if (ctx) {
      ctx->locals = base_locals + call_size;
   }

   if (params && !is_empty(params)) {
      int arg_offset = ptr_size + ret_size + (variadic ? variadic_total + ptr_size + len_size + fixed_stack_total : fixed_stack_total);
      int actual_index = 0;

      if (variadic) {
         int extra_offset = ptr_size;

         for (int i = fixed_params; i < arg_count; i++) {
            ContextEntry tmp;
            int actual_size = expr_value_size(args->children[i], ctx);
            const ASTNode *actual_type = expr_value_type(args->children[i], ctx);
            const ASTNode *actual_decl = expr_value_declarator(args->children[i], ctx);

            tmp.name = "$vararg";
            tmp.type = actual_type ? actual_type : required_typename_node("int");
            tmp.declarator = actual_decl;
            tmp.is_static = false;
            tmp.is_zeropage = false;
            tmp.is_global = false;
            tmp.is_ref = false;
            tmp.is_absolute_ref = false;
            tmp.read_expr = NULL;
            tmp.write_expr = NULL;
            tmp.offset = base_locals + extra_offset;
            tmp.size = actual_size;
            if (!compile_expr_to_slot(args->children[i], ctx, &tmp)) {
               goto fail;
            }
            extra_offset += actual_size;
         }

         emit_prepare_fp_ptr(0, base_locals + ptr_size);
         emit_store_ptr_to_fp(base_locals + ptr_size + variadic_total + ret_size, 0, ptr_size);
         {
            unsigned char bytes[sizeof(long long)] = {0};
            char len_buf[32];
            snprintf(len_buf, sizeof(len_buf), "%d", variadic_total);
            if (type_is_big_endian(required_typename_node("*"))) make_be_int(len_buf, bytes, len_size);
            else make_le_int(len_buf, bytes, len_size);
            emit_store_immediate_to_fp(base_locals + ptr_size + variadic_total + ret_size + ptr_size, bytes, len_size);
         }
      }

      for (int i = 0; i < params->count && actual_index < arg_count; i++) {
         const ASTNode *parameter = params->children[i];
         const ASTNode *ptype = parameter_type(parameter);
         const ASTNode *pdecl = parameter_declarator(parameter);
         ContextEntry tmp;
         int psz;

         if (!ptype || parameter_is_void(parameter) || parameter_is_ellipsis(parameter)) {
            continue;
         }

         psz = parameter_storage_size(parameter);
         tmp.type = parameter_is_ref(parameter) ? required_typename_node("*") : ptype;
         tmp.declarator = parameter_is_ref(parameter) ? NULL : call_adjusted_parameter_declarator(pdecl, false);
         tmp.is_static = false;
         tmp.is_zeropage = false;
         tmp.is_global = false;
         tmp.is_ref = false;
         tmp.is_absolute_ref = false;
         tmp.read_expr = NULL;
         tmp.write_expr = NULL;
         arg_offset -= psz;
         tmp.offset = base_locals + arg_offset;
         tmp.size = psz;

         if (parameter_is_ref(parameter)) {
            if (!compile_ref_argument_to_slot(args->children[actual_index], ctx, tmp.offset, tmp.size)) {
               goto fail;
            }
         }
         else if (!compile_expr_to_slot(args->children[actual_index], ctx, &tmp)) {
            goto fail;
         }

         actual_index++;
      }
   }

   callee_tmp.name = "$callee";
   callee_tmp.type = required_typename_node("*");
   callee_tmp.declarator = NULL;
   callee_tmp.is_static = false;
   callee_tmp.is_zeropage = false;
   callee_tmp.is_global = false;
   callee_tmp.is_ref = false;
   callee_tmp.is_absolute_ref = false;
   callee_tmp.read_expr = NULL;
   callee_tmp.write_expr = NULL;
   callee_tmp.offset = base_locals + callee_tmp_offset;
   callee_tmp.size = ptr_size;

   if (!compile_expr_to_slot(callee, ctx, &callee_tmp)) {
      goto fail;
   }

   emit_load_ptr_from_fpvar(0, callee_tmp.offset);
   remember_runtime_import("callptr0");
   emit(&es_code, "    lda fp+1\n");
   emit(&es_code, "    pha\n");
   emit(&es_code, "    lda fp\n");
   emit(&es_code, "    pha\n");
   emit(&es_code, "    jsr _callptr0\n");
   emit(&es_code, "    pla\n");
   emit(&es_code, "    sta fp\n");
   emit(&es_code, "    pla\n");
   emit(&es_code, "    sta fp+1\n");

   if (ctx) {
      ctx->locals = base_locals;
   }

   if (dst && ret_size > 0) {
      emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type,
                                 base_locals + ptr_size + variadic_total,
                                 ret_size, ret_type);
   }

   if (call_size > 0) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
   }

   return true;

fail:
   if (ctx) {
      ctx->locals = base_locals;
   }
   if (call_size > 0) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
   }
   return false;
}

static bool compile_call_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst) {
   if (!expr || strcmp(expr->name, "()") || expr->count < 1) {
      return false;
   }

   ASTNode *callee = expr->children[0];
   {
      const char *builtin_name = expr_bare_identifier_name(callee);
      if (builtin_name && builtin_variadic_call_name(builtin_name)) {
         if (dst) {
            error_user("[%s:%d.%d] %s does not produce a value", expr->file, expr->line, expr->column, builtin_name);
         }
         if (!strcmp(builtin_name, BUILTIN_VA_START_NAME)) {
            return compile_builtin_va_start_expr(expr, ctx);
         }
         if (!strcmp(builtin_name, BUILTIN_VA_ARG_NAME)) {
            return compile_builtin_va_arg_expr(expr, ctx);
         }
         if (!strcmp(builtin_name, BUILTIN_VA_END_NAME)) {
            return compile_builtin_va_end_expr(expr, ctx);
         }
      }
   }
   ASTNode *args = (expr->count > 1) ? expr->children[1] : NULL;
   const ASTNode *fn = NULL;
   const ASTNode *ret_type = dst ? dst->type : NULL;
   const ASTNode *declarator = NULL;
   const ASTNode *ret_decl = NULL;
   int ret_size = dst ? dst->size : 0;
   int arg_total = 0;
   int arg_count = (args && !is_empty(args)) ? args->count : 0;
   int ptr_size = get_size("*");
   int len_size = get_size("*");
   int fixed_params = 0;
   int fixed_stack_total = 0;
   int variadic_total = 0;
   bool variadic = false;
   int base_locals = ctx ? ctx->locals : 0;

   {
      const char *callee_name = expr_bare_identifier_name(callee);
      if (callee_name) {
         fn = resolve_function_call_target(callee_name, expr, args, ctx);
         if (!fn && is_identifier_spelling(callee_name) && !ctx_lookup(ctx, callee_name) && !global_decl_lookup(callee_name)) {
            error_user("[%s:%d.%d] call target '%s' has no visible signature; declare it in this translation unit or with extern",
                  expr->file, expr->line, expr->column, callee_name);
         }
      }
   }

   if (!fn) {
      const ASTNode *callable_decl = expr_value_declarator(callee, ctx);
      const ASTNode *callable_type = expr_value_type(callee, ctx);
      if (callable_decl && declarator_has_parameter_list(callable_decl) && declarator_function_pointer_depth(callable_decl) > 0) {
         return compile_indirect_call_expr_to_slot(expr, ctx, dst, callee, args, callable_type, callable_decl);
      }
      if (expr_bare_identifier_name(callee)) {
         error_user("[%s:%d.%d] call target '%s' has no visible signature; declare it in this translation unit or with extern",
               expr->file, expr->line, expr->column, expr_bare_identifier_name(callee));
      }
      error_user("[%s:%d.%d] call target has no visible signature", expr->file, expr->line, expr->column);
      return false;
   }

   {
      const ASTNode *known_ret = function_return_type(fn);
      const ASTNode *params = NULL;
      declarator = function_declarator_node(fn);
      ret_decl = function_return_declarator_from_callable(declarator);
      if (known_ret) {
         ret_type = known_ret;
         ret_size = declarator_value_size(ret_type, ret_decl);
      }
      params = declarator_parameter_list(declarator);
      variadic = parameter_list_is_variadic(params);
      if (params && !is_empty(params)) {
         for (int i = 0; i < params->count; i++) {
            const ASTNode *parameter = params->children[i];
            const ASTNode *ptype = parameter_type(parameter);
            int psz;
            if (!ptype || parameter_is_void(parameter) || parameter_is_ellipsis(parameter)) {
               continue;
            }
            fixed_params++;
            psz = parameter_storage_size(parameter);
            if (!parameter_has_symbol_storage(parameter)) {
               fixed_stack_total += psz;
            }
         }
         if ((!variadic && fixed_params != arg_count) || (variadic && arg_count < fixed_params)) {
            warning("[%s:%d.%d] call to '%s' argument count mismatch (%d vs %d)",
                    expr->file, expr->line, expr->column,
                    callee->strval, arg_count, fixed_params);
         }
      }

      if (variadic && args && !is_empty(args)) {
         for (int i = fixed_params; i < arg_count; i++) {
            int actual_size = expr_value_size(args->children[i], ctx);
            if (actual_size <= 0) {
               error_user("[%s:%d.%d] variadic argument %d has no runtime storage", args->children[i]->file, args->children[i]->line, args->children[i]->column, i - fixed_params + 1);
            }
            variadic_total += actual_size;
         }
      }
   }

   arg_total = fixed_stack_total;
   if (variadic) {
      arg_total += variadic_total + ptr_size + len_size;
   }

   if (ret_size < 0) ret_size = 0;
   int call_size = ret_size + arg_total;

   if (call_size > 0) {
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
   }
   if (ctx) {
      ctx->locals = base_locals + call_size;
   }

   if (fn && declarator) {
      const ASTNode *params = declarator_parameter_list(declarator);
      int arg_offset = ret_size + (variadic ? variadic_total + ptr_size + len_size + fixed_stack_total : fixed_stack_total);
      int actual_index = 0;
      char callee_sym[256];
      if (!function_symbol_name(fn, callee->strval, callee_sym, sizeof(callee_sym))) {
         goto fail;
      }

      if (variadic) {
         int extra_offset = 0;

         for (int i = fixed_params; i < arg_count; i++) {
            ContextEntry tmp;
            int actual_size = expr_value_size(args->children[i], ctx);
            const ASTNode *actual_type = expr_value_type(args->children[i], ctx);
            const ASTNode *actual_decl = expr_value_declarator(args->children[i], ctx);

            tmp.name = "$vararg";
            tmp.type = actual_type ? actual_type : required_typename_node("int");
            tmp.declarator = actual_decl;
            tmp.is_static = false;
            tmp.is_zeropage = false;
            tmp.is_global = false;
            tmp.is_ref = false;
            tmp.is_absolute_ref = false;
            tmp.read_expr = NULL;
            tmp.write_expr = NULL;
            tmp.offset = base_locals + extra_offset;
            tmp.size = actual_size;
            if (!compile_expr_to_slot(args->children[i], ctx, &tmp)) {
               goto fail;
            }
            extra_offset += actual_size;
         }

         emit_prepare_fp_ptr(0, base_locals);
         emit_store_ptr_to_fp(base_locals + variadic_total + ret_size, 0, ptr_size);
         {
            unsigned char bytes[sizeof(long long)] = {0};
            char len_buf[32];
            snprintf(len_buf, sizeof(len_buf), "%d", variadic_total);
            if (type_is_big_endian(required_typename_node("*"))) make_be_int(len_buf, bytes, len_size);
            else make_le_int(len_buf, bytes, len_size);
            emit_store_immediate_to_fp(base_locals + variadic_total + ret_size + ptr_size, bytes, len_size);
         }
      }

      if (params && !is_empty(params)) {
         for (int i = 0; i < params->count && actual_index < arg_count; i++) {
            const ASTNode *parameter = params->children[i];
            const ASTNode *ptype = parameter_type(parameter);
            const ASTNode *pdecl = parameter_declarator(parameter);
            ContextEntry tmp;
            int psz;

            if (!ptype || parameter_is_void(parameter) || parameter_is_ellipsis(parameter)) {
               continue;
            }

            psz = parameter_storage_size(parameter);
            tmp.type = parameter_is_ref(parameter) ? required_typename_node("*") : ptype;
            tmp.declarator = parameter_is_ref(parameter) ? NULL : call_adjusted_parameter_declarator(pdecl, false);
            tmp.is_static = false;
            tmp.is_zeropage = false;
            tmp.is_global = false;
            tmp.is_ref = false;
            tmp.is_absolute_ref = false;
            tmp.read_expr = NULL;
            tmp.write_expr = NULL;
            tmp.size = psz;

            if (parameter_has_symbol_storage(parameter)) {
               char sym[256];
               bool is_zeropage = false;

               if (!function_parameter_symbol_name(fn, parameter, i, sym, sizeof(sym), &is_zeropage)) {
                  goto fail;
               }

               tmp.offset = base_locals;
               if (parameter_is_ref(parameter)) {
                  if (!compile_ref_argument_to_slot(args->children[actual_index], ctx, tmp.offset, tmp.size)) {
                     goto fail;
                  }
               }
               else if (!compile_expr_to_slot(args->children[actual_index], ctx, &tmp)) {
                  goto fail;
               }
               if (!function_has_body(fn)) {
                  remember_symbol_import_mode(sym, is_zeropage);
               }
               emit_copy_fp_to_symbol(sym, tmp.offset, tmp.size);
            }
            else {
               arg_offset -= psz;
               tmp.offset = base_locals + arg_offset;
               if (parameter_is_ref(parameter)) {
                  if (!compile_ref_argument_to_slot(args->children[actual_index], ctx, tmp.offset, tmp.size)) {
                     goto fail;
                  }
               }
               else if (!compile_expr_to_slot(args->children[actual_index], ctx, &tmp)) {
                  goto fail;
               }
            }
            actual_index++;
         }
      }

      record_call_graph_edge(current_call_graph_function, fn);
      remember_symbol_import(callee_sym);
      emit(&es_code, "    lda fp+1\n");
      emit(&es_code, "    pha\n");
      emit(&es_code, "    lda fp\n");
      emit(&es_code, "    pha\n");
      emit(&es_code, "    jsr %s\n", callee_sym);
      emit(&es_code, "    pla\n");
      emit(&es_code, "    sta fp\n");
      emit(&es_code, "    pla\n");
      emit(&es_code, "    sta fp+1\n");
   }

   if (ctx) {
      ctx->locals = base_locals;
   }

   if (dst && ret_size > 0) {
      emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type,
                                 base_locals + (variadic ? variadic_total : 0),
                                 ret_size, ret_type);
   }

   if (call_size > 0) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
   }

   return true;

fail:
   if (ctx) {
      ctx->locals = base_locals;
   }
   if (call_size > 0) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", call_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
   }
   return false;
}


bool compile_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst) {
   expr = (ASTNode *) unwrap_expr_node(expr);

   if (!expr || is_empty(expr)) {
      return true;
   }

   if (dst && dst->declarator && declarator_pointer_depth(dst->declarator) > 0) {
      const ASTNode *src_decl = expr_value_declarator(expr, ctx);
      if (src_decl && declarator_pointer_depth(src_decl) == 0 && declarator_array_count(src_decl) > 0) {
         LValueRef lv;
         if (resolve_ref_argument_lvalue(ctx, expr, &lv)) {
            if (!emit_prepare_lvalue_ptr(ctx, &lv, LVALUE_ACCESS_ADDRESS)) {
               return false;
            }
            emit_store_ptr_to_fp(dst->offset, 0, dst->size);
            return true;
         }
      }
   }

   if (!strcmp(expr->name, "assign_expr") && expr->count == 3) {
      LValueRef lv;
      int load_size;

      compile_expr(expr, ctx);
      if (!resolve_lvalue(ctx, expr->children[1], &lv)) {
         return false;
      }

      load_size = lv.size < dst->size ? lv.size : dst->size;
      if (load_size <= 0) {
         load_size = dst->size > 0 ? dst->size : lv.size;
      }
      if (load_size <= 0) {
         return false;
      }

      if (!emit_copy_lvalue_to_fp(ctx, dst->offset, &lv, load_size)) {
         return false;
      }
      emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, dst->offset, load_size, lv.type);
      return true;
   }

   if (!strcmp(expr->name, "()")) {
      return compile_call_expr_to_slot(expr, ctx, dst);
   }

   if (!strcmp(expr->name, "cast") || !strcmp(expr->name, "flag_cast")) {
      const ASTNode *target_type = !strcmp(expr->name, "cast") ? cast_expr_target_type(expr) : flag_cast_target_type(expr, ctx);
      const ASTNode *target_decl = !strcmp(expr->name, "cast") ? cast_expr_target_declarator(expr) : flag_cast_target_declarator(expr, ctx);
      int target_size = !strcmp(expr->name, "cast") ? cast_expr_target_size(expr) : flag_cast_target_size(expr, ctx);
      int saved_locals = ctx ? ctx->locals : 0;
      ContextEntry tmp;
      if (!target_type || target_size <= 0 || expr->count < 2) {
         return false;
      }
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", target_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      tmp = (ContextEntry){ .name = "$cast", .type = target_type, .declarator = target_decl, .is_static = false, .is_zeropage = false, .is_global = false, .offset = saved_locals, .size = target_size };
      if (ctx) {
         ctx->locals = saved_locals + target_size;
      }
      if (!compile_expr_to_slot(expr->children[1], ctx, &tmp)) {
         if (ctx) {
            ctx->locals = saved_locals;
         }
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", target_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return false;
      }
      if (ctx) {
         ctx->locals = saved_locals;
      }
      emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, tmp.offset, tmp.size, tmp.type);
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", target_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      return true;
   }

   if (!strcmp(expr->name, "sizeof")) {
      int size_value = sizeof_operand_size(expr->children[0], ctx);
      unsigned char *bytes;
      if (size_value <= 0) {
         return false;
      }
      bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
      if (!bytes) {
         error_unreachable("out of memory");
      }
      if (!encode_integer_initializer_value(size_value, bytes, dst->size, dst->type)) {
         free(bytes);
         return false;
      }
      emit_store_immediate_to_fp(dst->offset, bytes, dst->size);
      free(bytes);
      return true;
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
      const char *style = type_float_style(dst->type);
      if (!bytes) {
         error_unreachable("out of memory");
      }
      if (!style) {
         error_unreachable("[%s:%d] floating literal assigned to non-float type", __FILE__, __LINE__);
      }
      if (has_flag(type_name_from_node(dst->type), "$endian:big")) {
         make_be_float_style(expr->strval, bytes, dst->size, style);
      }
      else {
         make_le_float_style(expr->strval, bytes, dst->size, style);
      }
      emit_store_immediate_to_fp(dst->offset, bytes, dst->size);
      free(bytes);
      return true;
   }

   if (expr->kind == AST_STRING) {
      long long ch_value = 0;

      if (decode_char_constant_value(expr->strval, &ch_value)) {
         unsigned char *bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
         char tmp[64];
         snprintf(tmp, sizeof(tmp), "%lld", ch_value);
         if (has_flag(type_name_from_node(dst->type), "$endian:big")) {
            make_be_int(tmp, bytes, dst->size);
         }
         else {
            make_le_int(tmp, bytes, dst->size);
         }
         emit_store_immediate_to_fp(dst->offset, bytes, dst->size);
         free(bytes);
      }
      else {
         const char *label = emit_pointer_initializer_backing_object(dst ? dst->type : NULL,
               dst ? dst->declarator : NULL, expr);
         if (!label) {
            label = remember_string_literal(expr->strval);
         }
         emit_store_label_address_to_fp(dst->offset, dst->size, label);
      }
      return true;
   }

   {
      const char *ident = expr_bare_identifier_name(expr);
      if (ident) {
         ContextEntry *entry = ctx_lookup(ctx, ident);
         if (entry && entry_is_absolute_ref(entry)) {
            LValueRef lv = { .name = entry->name, .type = entry->type, .declarator = entry->declarator, .base_type = entry->type, .base_declarator = entry->declarator, .is_static = entry->is_static, .is_zeropage = entry->is_zeropage, .is_global = entry->is_global, .is_ref = entry->is_ref, .is_absolute_ref = entry->is_absolute_ref, .read_expr = entry->read_expr, .write_expr = entry->write_expr, .offset = entry->offset, .size = entry->size };
            if (!entry_has_read_address(entry)) {
               error_user("[%s:%d.%d] absolute ref '%s' is write-only", expr->file, expr->line, expr->column, ident);
            }
            if (dst->size == lv.size && dst->type == lv.type) {
               return emit_copy_lvalue_to_fp(ctx, dst->offset, &lv, lv.size);
            }
            remember_runtime_import("pushN");
            emit(&es_code, "    lda #$%02x\n", lv.size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _pushN\n");
            if (!emit_copy_lvalue_to_fp(ctx, ctx->locals, &lv, lv.size)) {
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", lv.size & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
               return false;
            }
            emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, ctx->locals, lv.size, lv.type);
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", lv.size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return true;
         }
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
            const ASTNode *g = global_decl_lookup(ident);
            if (g && g->count >= 3) {
               ContextEntry gentry;
               if (init_context_entry_from_global_decl(&gentry, ident, g) && entry_is_absolute_ref(&gentry)) {
                  LValueRef lv = { .name = gentry.name, .type = gentry.type, .declarator = gentry.declarator, .base_type = gentry.type, .base_declarator = gentry.declarator, .is_static = gentry.is_static, .is_zeropage = gentry.is_zeropage, .is_global = gentry.is_global, .is_ref = gentry.is_ref, .is_absolute_ref = gentry.is_absolute_ref, .read_expr = gentry.read_expr, .write_expr = gentry.write_expr, .offset = gentry.offset, .size = gentry.size };
                  if (!entry_has_read_address(&gentry)) {
                     error_user("[%s:%d.%d] absolute ref '%s' is write-only", expr->file, expr->line, expr->column, ident);
                  }
                  if (dst->size == lv.size && dst->type == lv.type) {
                     return emit_copy_lvalue_to_fp(ctx, dst->offset, &lv, lv.size);
                  }
                  remember_runtime_import("pushN");
                  emit(&es_code, "    lda #$%02x\n", lv.size & 0xff);
                  emit(&es_code, "    sta arg0\n");
                  emit(&es_code, "    jsr _pushN\n");
                  if (!emit_copy_lvalue_to_fp(ctx, ctx->locals, &lv, lv.size)) {
                     remember_runtime_import("popN");
                     emit(&es_code, "    lda #$%02x\n", lv.size & 0xff);
                     emit(&es_code, "    sta arg0\n");
                     emit(&es_code, "    jsr _popN\n");
                     return false;
                  }
                  emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, ctx->locals, lv.size, lv.type);
                  remember_runtime_import("popN");
                  emit(&es_code, "    lda #$%02x\n", lv.size & 0xff);
                  emit(&es_code, "    sta arg0\n");
                  emit(&es_code, "    jsr _popN\n");
                  return true;
               }
               else {
                  char sym[256];
                  int gsize = declarator_storage_size(g->children[1], decl_node_declarator(g));
                  format_user_asm_symbol(ident, sym, sizeof(sym));
                  emit_copy_symbol_to_fp_convert(dst->offset, dst->size, dst->type, sym, gsize, g->children[1]);
                  return true;
               }
            }
         }
         {
            const ASTNode *target_type = NULL;
            const ASTNode *target_decl = NULL;
            const ASTNode *fn;
            if (dst && dst->declarator && declarator_has_parameter_list(dst->declarator) && declarator_function_pointer_depth(dst->declarator) > 0) {
               target_type = dst->type;
               target_decl = dst->declarator;
            }
            fn = resolve_function_designator_target(ident, target_type, target_decl);
            if (fn) {
               char sym[256];
               if (function_has_static_parameters(fn)) {
                  error_user("[%s:%d.%d] cannot create a pointer to function '%s' because it has symbol-backed parameters", expr->file, expr->line, expr->column, ident);
               }
               if (!function_symbol_name(fn, ident, sym, sizeof(sym))) {
                  return false;
               }
               {
                  char label[sizeof(sym) + 2];
                  snprintf(label, sizeof(label), "%s", sym);
                  emit_store_label_address_to_fp(dst->offset, dst->size, label);
               }
               return true;
            }
         }
      }
   }

   if (expr->count == 1 && !strcmp(expr->name, "&")) {
      LValueRef lv;
      ASTNode *inner = (ASTNode *) unwrap_expr_node(expr->children[0]);
      if (inner && !strcmp(inner->name, "lvalue") && resolve_lvalue(ctx, inner, &lv)) {
         if (!emit_prepare_lvalue_ptr(ctx, &lv, LVALUE_ACCESS_ADDRESS)) {
            if (lv.is_absolute_ref) {
               error_user("[%s:%d.%d] absolute ref '%s' does not have a single address", inner->file, inner->line, inner->column, lv.name ? lv.name : "<unnamed>");
            }
            return false;
         }
         emit_store_ptr_to_fp(dst->offset, 0, dst->size);
         return true;
      }
      {
         const char *ident = expr_bare_identifier_name(inner);
         if (ident) {
            const ASTNode *target_type = NULL;
            const ASTNode *target_decl = NULL;
            const ASTNode *fn;
            if (dst && dst->declarator && declarator_has_parameter_list(dst->declarator) && declarator_function_pointer_depth(dst->declarator) > 0) {
               target_type = dst->type;
               target_decl = dst->declarator;
            }
            fn = resolve_function_designator_target(ident, target_type, target_decl);
            if (fn) {
               char sym[256];
               if (function_has_static_parameters(fn)) {
                  error_user("[%s:%d.%d] cannot create a pointer to function '%s' because it has symbol-backed parameters", inner->file, inner->line, inner->column, ident);
               }
               if (!function_symbol_name(fn, ident, sym, sizeof(sym))) {
                  return false;
               }
               emit_store_label_address_to_fp(dst->offset, dst->size, sym);
               return true;
            }
         }
      }
      {
         const char *label = emit_pointer_initializer_backing_object(dst ? dst->type : NULL,
               dst ? dst->declarator : NULL, expr);
         InitConstValue value = {0};
         if (label) {
            emit_store_label_address_to_fp(dst->offset, dst->size, label);
            return true;
         }
         if (eval_constant_initializer_expr(inner, &value) && value.kind == INIT_CONST_INT) {
            unsigned char *bytes = (unsigned char *) calloc(dst->size ? dst->size : 1, sizeof(unsigned char));
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%lld", value.i);
            if (has_flag(type_name_from_node(dst->type), "$endian:big")) {
               make_be_int(tmp, bytes, dst->size);
            }
            else {
               make_le_int(tmp, bytes, dst->size);
            }
            emit_store_immediate_to_fp(dst->offset, bytes, dst->size);
            free(bytes);
            return true;
         }
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
      if (lv.is_absolute_ref) {
         if (!lv.read_expr) {
            error_user("[%s:%d.%d] absolute ref '%s' is write-only", expr->file, expr->line, expr->column, lv.name ? lv.name : "<unnamed>");
         }
         if (!lv.write_expr) {
            error_user("[%s:%d.%d] absolute ref '%s' is read-only", expr->file, expr->line, expr->column, lv.name ? lv.name : "<unnamed>");
         }
      }
      classify_incdec_lvalue_expr(expr, &inc, &pre);
      ofn = resolve_incdec_overload_expr(expr, ctx);
      if (!ofn && type_has_exactops(lv.type)) {
         error_user("[%s:%d.%d] type '%s' uses '$exactops' and requires visible overload '%s'",
                    expr->file, expr->line, expr->column,
                    type_name_from_node(lv.type), inc ? "operator++" : "operator--");
      }
      if (ofn) {
         const ASTNode *rtype = function_return_type(ofn);
         const ASTNode *rdecl = function_declarator_node(ofn);
         int old_size = lv.size > 0 ? lv.size : dst->size;
         int result_size = declarator_storage_size(rtype, rdecl);
         int store_size = lv.size > 0 ? lv.size : old_size;
         int saved_locals = ctx ? ctx->locals : 0;
         int result_offset;
         int store_offset;
         int tmp_total;
         ContextEntry result_tmp;
         LValueRef store_lv = lv;
         ASTNode *operand;
         ASTNode *argv[1] = { NULL };
         ASTNode *call;

         if (result_size <= 0) {
            result_size = type_size_from_node(rtype);
         }
         if (result_size <= 0) {
            error_user("[%s:%d.%d] overloaded %s has unknown return size", expr->file, expr->line, expr->column, inc ? "operator++" : "operator--");
         }
         result_offset = saved_locals + old_size;
         store_offset = result_offset + result_size;
         tmp_total = old_size + result_size + store_size;
         result_tmp = (ContextEntry){ .name = "$incdec_result", .type = rtype, .declarator = rdecl, .is_static = false, .is_zeropage = false, .is_global = false, .offset = result_offset, .size = result_size };
         if (!store_lv.is_static && !store_lv.is_global && !store_lv.is_absolute_ref) {
            store_lv.offset += tmp_total;
         }
         operand = make_synthetic_incdec_operand(expr);
         if (!operand) {
            return false;
         }
         argv[0] = operand;
         call = make_synthetic_call_expr(expr, declarator_name(function_declarator_node(ofn)), argv, 1);
         if (!call) {
            return false;
         }

         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         if (!emit_copy_lvalue_to_fp(ctx, saved_locals, &lv, old_size)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         if (!pre) {
            emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, saved_locals, old_size, lv.type);
         }
         if (ctx) {
            ctx->locals = saved_locals + tmp_total;
         }
         if (!compile_call_expr_to_slot(call, ctx, &result_tmp)) {
            if (ctx) {
               ctx->locals = saved_locals;
            }
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         if (ctx) {
            ctx->locals = saved_locals;
         }
         emit_copy_fp_to_fp_convert(store_offset, store_size, lv.type, result_offset, result_size, rtype);
         if (!emit_copy_fp_to_lvalue(ctx, &store_lv, store_offset, store_size)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
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
         if (!emit_copy_lvalue_to_fp(ctx, tmp.offset, &lv, tmp.size)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
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
         if (!emit_copy_fp_to_lvalue(ctx, &lv, tmp.offset, tmp.size)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
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
         int load_size = lv.size < dst->size ? lv.size : dst->size;
         if (lv.size == dst->size && !strcmp(type_name_from_node(lv.type), type_name_from_node(dst->type)) &&
             declarator_pointer_depth(lv.declarator) == declarator_pointer_depth(dst->declarator) &&
             declarator_array_count(lv.declarator) == declarator_array_count(dst->declarator)) {
            return emit_copy_lvalue_to_fp(ctx, dst->offset, &lv, lv.size);
         }
         if (!emit_copy_lvalue_to_fp(ctx, dst->offset, &lv, load_size)) {
            return false;
         }
         emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, dst->offset, load_size, lv.type);
         return true;
      }
   }

   if (!strcmp(expr->name, "comma_expr") && expr->count > 0) {
      for (int i = 0; i < expr->count - 1; i++) {
         compile_expr(expr->children[i], ctx);
      }
      return compile_expr_to_slot(expr->children[expr->count - 1], ctx, dst);
   }

   if (expr_is_ternary_node(expr)) {
      ASTNode *test_expr = expr_ternary_test(expr);
      ASTNode *true_expr = expr_ternary_true(expr);
      ASTNode *false_expr = expr_ternary_false(expr);
      const char *false_label = next_label("ternary_false");
      const char *end_label = next_label("ternary_end");
      bool ok;
      if (!test_expr || !true_expr || !false_expr || !false_label || !end_label) {
         free((void *) false_label);
         free((void *) end_label);
         return false;
      }
      if (!compile_condition_branch_false(test_expr, ctx, false_label)) {
         free((void *) false_label);
         free((void *) end_label);
         return false;
      }
      ok = compile_expr_to_slot(true_expr, ctx, dst);
      emit(&es_code, "    jmp %s\n", end_label);
      emit(&es_code, "%s:\n", false_label);
      if (ok) {
         ok = compile_expr_to_slot(false_expr, ctx, dst);
      }
      emit(&es_code, "%s:\n", end_label);
      free((void *) false_label);
      free((void *) end_label);
      return ok;
   }

   {
      require_no_mixed_signed_integer_binary_expr(expr, ctx);
      require_no_mixed_endian_integer_binary_expr(expr, ctx);
      require_no_mixed_exactops_operator_expr(expr, ctx);
      const ASTNode *ofn = resolve_operator_overload_expr(expr, ctx);
      if (ofn) {
         ASTNode *argv[2] = { NULL, NULL };
         ASTNode *call;
         argv[0] = expr->children[0];
         if (expr->count > 1) {
            argv[1] = expr->children[1];
         }
         call = make_synthetic_call_expr(expr, declarator_name(function_declarator_node(ofn)), argv, expr->count);
         return call ? compile_call_expr_to_slot(call, ctx, dst) : false;
      }
      require_exactops_operator_expr(expr, ctx);
   }

   {
      const char *opname = NULL;
      const ASTNode *ret_type = NULL;
      const ASTNode *ret_decl = NULL;
      int ret_size = 0;
      int arg_count = 0;
      ASTNode *arg_exprs[2] = { NULL, NULL };
      const ASTNode *arg_types[2] = { NULL, NULL };
      const ASTNode *arg_decls[2] = { NULL, NULL };
      char sym[256];
      if (expr_eligible_for_weak_builtin_operator(expr, ctx, &opname, &ret_type, &ret_decl, &ret_size, &arg_count, arg_exprs, arg_types, arg_decls) &&
          weak_builtin_operator_symbol_name(opname, arg_count, arg_types, arg_decls, sym, sizeof(sym))) {
         return compile_weak_builtin_operator_call_to_slot(sym, ret_type, ret_decl, ret_size, arg_count, arg_exprs, arg_types, arg_decls, ctx, dst);
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
      const ASTNode *neg_type = expr_value_type(expr, ctx);
      if (!compile_expr_to_slot(expr->children[0], ctx, dst)) {
         return false;
      }
      if (!neg_type) {
         neg_type = dst->type;
      }
      emit_prepare_fp_ptr(0, dst->offset);
      emit_prepare_fp_ptr(1, dst->offset);
      emit(&es_code, "    lda #$%02x\n", dst->size & 0xff);
      emit(&es_code, "    sta arg0\n");
      remember_runtime_import(int_comp2_helper_name(neg_type));
      emit(&es_code, "    jsr _%s\n", int_comp2_helper_name(neg_type));
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
      const ASTNode *lhs_type = NULL;
      const ASTNode *lhs_decl = NULL;
      const ASTNode *rhs_type = NULL;
      const ASTNode *rhs_decl = NULL;
      const ASTNode *work_type = expr_value_type(expr, ctx);
      int work_size = expr_value_size(expr, ctx);
      int pointer_scale = 1;

      expr_match_signature(expr->children[0], ctx, &lhs_type, &lhs_decl);
      expr_match_signature(expr->children[1], ctx, &rhs_type, &rhs_decl);

      bool scaled_pointer_arith = lhs_decl && declarator_pointer_depth(lhs_decl) > 0;

      if (scaled_pointer_arith) {
         require_no_mixed_endian_pointer_index_expr(expr, (ASTNode *) rhs, ctx, expr->name);
         work_size = declarator_storage_size(lhs_type, lhs_decl);
         if (work_size <= 0) {
            work_size = dst->size;
         }
      }
      else if (!strcmp(expr->name, "+") && rhs_decl && declarator_pointer_depth(rhs_decl) > 0) {
         require_no_mixed_endian_pointer_index_expr(expr, expr->children[0], ctx, expr->name);
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

      if (!strcmp(expr->name, "-") && lhs_decl && declarator_pointer_depth(lhs_decl) > 0 && rhs_decl && declarator_pointer_depth(rhs_decl) > 0) {
         int ptr_size = declarator_storage_size(lhs_type, lhs_decl);
         int elem_size = pointer_scale > 0 ? pointer_scale : 1;
         int tmp_total = ptr_size * 3;
         int saved_locals = ctx ? ctx->locals : 0;
         int lhs_off = saved_locals;
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
         if (ctx) {
            ctx->locals = saved_locals + tmp_total;
         }
         if (!compile_expr_to_slot(expr->children[0], ctx, &lhs_tmp) || !compile_expr_to_slot((ASTNode *) rhs, ctx, &rhs_tmp)) {
            if (ctx) {
               ctx->locals = saved_locals;
            }
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         if (ctx) {
            ctx->locals = saved_locals;
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
         remember_runtime_import(int_div_helper_name(lhs_type));
         emit(&es_code, "    jsr _%s\n", int_div_helper_name(lhs_type));
         emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, quo_off, ptr_size, dst->type ? dst->type : lhs_type);
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return true;
      }

      {
         int saved_locals = ctx ? ctx->locals : 0;
         int lhs_offset = saved_locals;
         int rhs_offset = lhs_offset + work_size;
         int factor_offset = 0;
         int scaled_offset = 0;
         int value_offset = rhs_offset;
         int tmp_total = work_size * 2;
         const ASTNode *rhs_slot_type = scaled_pointer_arith ? (expr_is_literal_node(rhs) ? work_type : rhs_type) : work_type;
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
         if (ctx) {
            ctx->locals = saved_locals + tmp_total;
         }

         if (!compile_expr_to_slot(expr->children[0], ctx, &lhs_tmp) ||
             !compile_expr_to_slot((ASTNode *) rhs, ctx, &rhs_tmp)) {
            if (ctx) {
               ctx->locals = saved_locals;
            }
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         if (ctx) {
            ctx->locals = saved_locals;
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
            emit_runtime_binary_fp_fp(int_mul_helper_name(factor_type ? factor_type : work_type), scaled_offset, rhs_offset, factor_offset, work_size);
            value_offset = int_mul_result_offset(factor_type ? factor_type : work_type, scaled_offset, work_size);
         }

         if (work_type && type_is_float_like(work_type)) {
            int expbits = type_float_expbits(work_type);
            if (expbits < 0) {
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
               error_user("[%s:%d.%d] unsupported float style/size for runtime arithmetic", expr->file, expr->line, expr->column);
               return false;
            }
            emit_runtime_float_binary_fp_fp(!strcmp(expr->name, "+") ? "faddN" : "fsubN", lhs_offset, lhs_offset, value_offset, work_size, expbits);
         }
         else if (!strcmp(expr->name, "+")) {
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
      const ASTNode *rhs_type = expr_value_type(expr->children[1], ctx);
      const ASTNode *op_type = expr_value_type(expr, ctx);
      const ASTNode *rhs_slot_type = expr_is_literal_node(expr->children[1]) ? op_type : (rhs_type ? rhs_type : op_type);
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

      diagnose_constant_shift_count(expr->children[1], lhs_size * 8);

      tmp_total = lhs_size + rhs_size + lhs_size;
      int saved_locals = ctx ? ctx->locals : 0;
      lhs_offset = saved_locals;
      rhs_offset = lhs_offset + lhs_size;
      aux_offset = rhs_offset + rhs_size;
      lhs_tmp = (ContextEntry){ .name = "$lhs", .type = op_type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = lhs_offset, .size = lhs_size };
      rhs_tmp = (ContextEntry){ .name = "$rhs", .type = rhs_slot_type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = rhs_offset, .size = rhs_size };

      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      if (ctx) {
         ctx->locals = saved_locals + tmp_total;
      }

      if (!compile_expr_to_slot(expr->children[0], ctx, &lhs_tmp) ||
          !compile_expr_to_slot(expr->children[1], ctx, &rhs_tmp)) {
         if (ctx) {
            ctx->locals = saved_locals;
         }
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return false;
      }
      if (ctx) {
         ctx->locals = saved_locals;
      }

      helper = int_shift_helper_name(op_type, !strcmp(op, "<<"));
      emit_runtime_shift_fp(helper, lhs_offset, aux_offset, rhs_offset, rhs_type, rhs_size, lhs_size);

      emit_copy_fp_to_fp_convert(dst->offset, dst->size, dst->type, aux_offset, lhs_size, op_type);
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

      int saved_locals = ctx ? ctx->locals : 0;
      lhs_offset = saved_locals;
      rhs_offset = lhs_offset + op_size;
      aux_offset = rhs_offset + op_size;
      lhs_tmp = (ContextEntry){ .name = "$lhs", .type = op_type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = lhs_offset, .size = op_size };
      rhs_tmp = (ContextEntry){ .name = "$rhs", .type = op_type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = rhs_offset, .size = op_size };

      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      if (ctx) {
         ctx->locals = saved_locals + tmp_total;
      }

      if (!compile_expr_to_slot(expr->children[0], ctx, &lhs_tmp) ||
          !compile_expr_to_slot(expr->children[1], ctx, &rhs_tmp)) {
         if (ctx) {
            ctx->locals = saved_locals;
         }
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return false;
      }
      if (ctx) {
         ctx->locals = saved_locals;
      }

      if (!strcmp(op, "&")) helper = "bit_andN";
      else if (!strcmp(op, "|")) helper = "bit_orN";
      else if (!strcmp(op, "^")) helper = "bit_xorN";

      if (helper) {
         emit_runtime_binary_fp_fp(helper, lhs_offset, lhs_offset, rhs_offset, op_size);
      }
      else if (!strcmp(op, "*")) {
         if (op_type && type_is_float_like(op_type)) {
            int expbits = type_float_expbits(op_type);
            if (expbits < 0) {
               if (ctx) {
                  ctx->locals = saved_locals;
               }
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
               error_user("[%s:%d.%d] unsupported float style/size for runtime arithmetic", expr->file, expr->line, expr->column);
               return false;
            }
            emit_runtime_float_binary_fp_fp("fmulN", aux_offset, lhs_offset, rhs_offset, op_size, expbits);
         }
         else {
            emit_runtime_binary_fp_fp(int_mul_helper_name(op_type), aux_offset, lhs_offset, rhs_offset, op_size);
         }
         emit_copy_fp_to_fp(lhs_offset, int_mul_result_offset(op_type, aux_offset, op_size), op_size);
      }
      else if (!strcmp(op, "/") || !strcmp(op, "%")) {
         if (!strcmp(op, "/") && op_type && type_is_float_like(op_type)) {
            int expbits = type_float_expbits(op_type);
            if (expbits < 0) {
               if (ctx) {
                  ctx->locals = saved_locals;
               }
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
               error_user("[%s:%d.%d] unsupported float style/size for runtime arithmetic", expr->file, expr->line, expr->column);
               return false;
            }
            emit_runtime_float_binary_fp_fp("fdivN", aux_offset, lhs_offset, rhs_offset, op_size, expbits);
            emit_copy_fp_to_fp(lhs_offset, aux_offset, op_size);
         }
         else {
            int rem_offset = aux_offset + op_size;
            emit_prepare_fp_ptr(0, lhs_offset);
            emit_prepare_fp_ptr(1, rhs_offset);
            emit_prepare_fp_ptr(2, aux_offset);
            emit_prepare_fp_ptr(3, rem_offset);
            emit(&es_code, "    lda #$%02x\n", op_size & 0xff);
            emit(&es_code, "    sta arg0\n");
            remember_runtime_import(int_div_helper_name(op_type));
            emit(&es_code, "    jsr _%s\n", int_div_helper_name(op_type));
            emit_copy_fp_to_fp(lhs_offset, !strcmp(op, "/") ? aux_offset : rem_offset, op_size);
         }
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




const char *next_label(const char *prefix) {
   char buf[64];
   snprintf(buf, sizeof(buf), "@%s_%d", prefix, label_counter++);
   return strdup(buf);
}

static bool compile_truthy_expr_branch_false(ASTNode *expr, Context *ctx,
                                             const ASTNode *type,
                                             const ASTNode *declarator,
                                             int size,
                                             const char *false_label) {
   int saved_locals = ctx ? ctx->locals : 0;
   ContextEntry tmp;

   if (size <= 0) {
      size = expr_value_size(expr, ctx);
   }
   if (size <= 0) {
      size = 1;
   }
   if (!type) {
      type = expr_value_type(expr, ctx);
   }

   tmp = (ContextEntry){ .name = "$tmp", .type = type, .declarator = declarator, .is_static = false, .is_zeropage = false, .is_global = false, .offset = saved_locals, .size = size };

   remember_runtime_import("pushN");
   emit(&es_code, "    lda #$%02x\n", size & 0xff);
   emit(&es_code, "    sta arg0\n");
   emit(&es_code, "    jsr _pushN\n");
   if (ctx) {
      ctx->locals = saved_locals + size;
   }

   if (!compile_expr_to_slot(expr, ctx, &tmp)) {
      if (ctx) {
         ctx->locals = saved_locals;
      }
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      return false;
   }
   if (ctx) {
      ctx->locals = saved_locals;
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

bool compile_condition_branch_false(ASTNode *expr, Context *ctx, const char *false_label) {
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
      require_no_mixed_signed_integer_binary_expr(expr, ctx);
      require_no_mixed_endian_integer_binary_expr(expr, ctx);
      require_no_mixed_exactops_operator_expr(expr, ctx);
      const ASTNode *ofn = resolve_operator_overload_expr(expr, ctx);
      if (ofn) {
         const ASTNode *rtype = function_return_type(ofn);
         const ASTNode *rdecl = function_declarator_node(ofn);
         int rsize = declarator_storage_size(rtype, rdecl);
         ASTNode *argv[2] = { NULL, NULL };
         ASTNode *call;
         if (rsize <= 0) {
            rsize = type_size_from_node(rtype);
         }
         if (rsize <= 0) {
            error_user("[%s:%d.%d] overloaded operator '%s' has unknown return size", expr->file, expr->line, expr->column, expr->name);
         }
         argv[0] = expr->children[0];
         if (expr->count > 1) {
            argv[1] = expr->children[1];
         }
         call = make_synthetic_call_expr(expr, declarator_name(function_declarator_node(ofn)), argv, expr->count);
         if (!call) {
            return false;
         }
         return compile_truthy_expr_branch_false(call, ctx, rtype, rdecl, rsize, false_label);
      }
   }

   {
      const ASTNode *tfn = resolve_truthiness_overload(expr, ctx);
      if (tfn) {
         ASTNode *argv[1] = { expr };
         ASTNode *call = make_synthetic_call_expr(expr, declarator_name(function_declarator_node(tfn)), argv, 1);
         const ASTNode *rtype = function_return_type(tfn);
         const ASTNode *rdecl = function_declarator_node(tfn);
         int rsize = declarator_storage_size(rtype, rdecl);
         if (!call) {
            return false;
         }
         if (rsize <= 0) {
            rsize = type_size_from_node(rtype);
         }
         if (rsize <= 0) {
            error_user("[%s:%d.%d] truthiness overload has unknown return size", expr->file, expr->line, expr->column);
         }
         if (!type_is_bool(rtype)) {
            error_user("[%s:%d.%d] operator{} must return bool", expr->file, expr->line, expr->column);
         }
         return compile_truthy_expr_branch_false(call, ctx, rtype, rdecl, rsize, false_label);
      }
      require_exactops_operator_expr(expr, ctx);
      require_exactops_truthiness_expr(expr, ctx);
   }

   {
      const char *opname = NULL;
      const ASTNode *ret_type = NULL;
      const ASTNode *ret_decl = NULL;
      int ret_size = 0;
      int arg_count = 0;
      ASTNode *arg_exprs[2] = { NULL, NULL };
      const ASTNode *arg_types[2] = { NULL, NULL };
      const ASTNode *arg_decls[2] = { NULL, NULL };
      if (expr_eligible_for_weak_builtin_operator(expr, ctx, &opname, &ret_type, &ret_decl, &ret_size, &arg_count, arg_exprs, arg_types, arg_decls)) {
         return compile_truthy_expr_branch_false(expr, ctx, ret_type, ret_decl, ret_size, false_label);
      }
   }

   if (expr->count == 2 &&
       (!strcmp(expr->name, "==") || !strcmp(expr->name, "!=") ||
        !strcmp(expr->name, "<")  || !strcmp(expr->name, ">")  ||
        !strcmp(expr->name, "<=") || !strcmp(expr->name, ">="))) {
      const ASTNode *lhs_type = expr_value_type(expr->children[0], ctx);
      const ASTNode *rhs_type = expr_value_type(expr->children[1], ctx);
      const ASTNode *type = NULL;
      int size;
      int compare_size;
      ContextEntry lhs;
      ContextEntry rhs;
      const char *helper = NULL;
      bool invert = false;
      bool is_float_compare;
      int expbits = -1;

      if ((lhs_type && type_is_float_like(lhs_type)) || (rhs_type && type_is_float_like(rhs_type))) {
         int lhs_size = lhs_type ? type_size_from_node(lhs_type) : 0;
         int rhs_size = rhs_type ? type_size_from_node(rhs_type) : 0;
         if (lhs_type && type_is_float_like(lhs_type) && (!rhs_type || !type_is_float_like(rhs_type) || lhs_size >= rhs_size)) {
            type = lhs_type;
         }
         else {
            type = rhs_type;
         }
      }
      else {
         type = binary_integer_work_type(expr->children[0], expr->children[1], ctx, expr);
      }
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
      int saved_locals = ctx ? ctx->locals : 0;
      lhs = (ContextEntry){ .name = "$lhs", .type = type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = saved_locals, .size = size };
      rhs = (ContextEntry){ .name = "$rhs", .type = type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = saved_locals + size, .size = size };
      is_float_compare = type_is_float_like(type);
      if (is_float_compare) {
         expbits = type_float_expbits(type);
         if (expbits <= 0) {
            error_user("[%s:%d.%d] unsupported float style/size for runtime comparison", expr->file, expr->line, expr->column);
         }
      }

      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", compare_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      if (ctx) {
         ctx->locals = saved_locals + compare_size;
      }

      if (!compile_expr_to_slot(expr->children[0], ctx, &lhs) ||
          !compile_expr_to_slot(expr->children[1], ctx, &rhs)) {
         if (ctx) {
            ctx->locals = saved_locals;
         }
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", compare_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return false;
      }
      if (ctx) {
         ctx->locals = saved_locals;
      }

      if (is_float_compare) {
         emit_runtime_float_compare(lhs.offset, rhs.offset, size, expbits);

         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", compare_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         emit(&es_code, "    lda arg1\n");
         if (!strcmp(expr->name, "==")) {
            emit(&es_code, "    bne %s\n", false_label);
         }
         else if (!strcmp(expr->name, "!=")) {
            emit(&es_code, "    beq %s\n", false_label);
         }
         else if (!strcmp(expr->name, "<")) {
            emit(&es_code, "    cmp #$ff\n");
            emit(&es_code, "    bne %s\n", false_label);
         }
         else if (!strcmp(expr->name, ">")) {
            emit(&es_code, "    cmp #$01\n");
            emit(&es_code, "    bne %s\n", false_label);
         }
         else if (!strcmp(expr->name, "<=")) {
            emit(&es_code, "    cmp #$01\n");
            emit(&es_code, "    beq %s\n", false_label);
         }
         else if (!strcmp(expr->name, ">=")) {
            emit(&es_code, "    cmp #$ff\n");
            emit(&es_code, "    beq %s\n", false_label);
         }
         return true;
      }

      if (!strcmp(expr->name, "==")) {
         helper = "eqN";
      }
      else if (!strcmp(expr->name, "!=")) {
         helper = "eqN";
         invert = true;
      }
      else if (!strcmp(expr->name, "<")) {
         helper = int_compare_helper_name(type, expr->name);
      }
      else if (!strcmp(expr->name, ">")) {
         helper = int_compare_helper_name(type, expr->name);
         ContextEntry t = lhs; lhs = rhs; rhs = t;
      }
      else if (!strcmp(expr->name, "<=")) {
         helper = int_compare_helper_name(type, expr->name);
      }
      else if (!strcmp(expr->name, ">=")) {
         helper = int_compare_helper_name(type, expr->name);
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
      return compile_truthy_expr_branch_false(expr, ctx, type, NULL, size, false_label);
   }
}

void compile_expr(ASTNode *node, Context *ctx) {
   if (!node || is_empty(node)) {
      return;
   }

   node = (ASTNode *) unwrap_expr_node(node);

   if (!strcmp(node->name, "()")) {
      if (!compile_call_expr_to_slot(node, ctx, NULL)) {
         error_unreachable("[%s:%d.%d] call expression not compiled yet", node->file, node->line, node->column);
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
         error_unreachable("[%s:%d.%d] expression not compiled yet", node->file, node->line, node->column);
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
   ASTNode *urhs = (ASTNode *) unwrap_expr_node(rhs);
   if (initializer_is_list(urhs)) {
      error_user("[%s:%d.%d] braced initializer not valid in assignment", urhs->file, urhs->line, urhs->column);
   }
   if (!resolve_lvalue(ctx, node->children[1], &lv)) {
      error_unreachable("[%s:%d.%d] assignment target not compiled yet", node->file, node->line, node->column);
      return;
   }
   dst_store = (ContextEntry){ .name = lv.name, .type = lv.type, .declarator = lv.declarator, .is_static = lv.is_static, .is_zeropage = lv.is_zeropage, .is_global = lv.is_global, .is_ref = lv.is_ref, .is_absolute_ref = lv.is_absolute_ref, .read_expr = lv.read_expr, .write_expr = lv.write_expr, .offset = lv.offset, .size = lv.size };
   dst = &dst_store;

   if (lv.is_absolute_ref && (!op || !strcmp(op, ":="))) {
      if (!entry_has_write_address(dst)) {
         error_user("[%s:%d.%d] absolute ref '%s' is read-only", node->file, node->line, node->column, lv.name ? lv.name : "<unnamed>");
      }
   }
   else if (lv.is_absolute_ref) {
      if (!entry_has_read_address(dst)) {
         error_user("[%s:%d.%d] absolute ref '%s' is write-only", node->file, node->line, node->column, lv.name ? lv.name : "<unnamed>");
      }
      if (!entry_has_write_address(dst)) {
         error_user("[%s:%d.%d] absolute ref '%s' is read-only", node->file, node->line, node->column, lv.name ? lv.name : "<unnamed>");
      }
   }

   if (!op || !strcmp(op, ":=")) {
      if (!lv.is_bitfield && !lv.is_absolute_ref && !lv.indirect && !lv.needs_runtime_address && (dst->is_static || dst->is_zeropage || dst->is_global)) {
         char sym[256];
         LValueRef rhs_lv;
         if (!entry_symbol_name(ctx, dst, sym, sizeof(sym))) {
            error_unreachable("[%s:%d.%d] assignment target not compiled yet", node->file, node->line, node->column);
            return;
         }
         if (resolve_ref_argument_lvalue(ctx, rhs, &rhs_lv) && rhs_lv.size == dst->size && !strcmp(type_name_from_node(rhs_lv.type), type_name_from_node(dst->type)) && !rhs_lv.is_bitfield) {
            if (!emit_copy_lvalue_to_symbol(ctx, sym, lv.offset, &rhs_lv, dst->size)) {
               error_unreachable("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
            }
            return;
         }
         if (!compile_expr_to_slot(rhs, ctx, &(ContextEntry){ .name = "$tmp", .type = dst->type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = dst->size })) {
            error_unreachable("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
            return;
         }
         emit_copy_fp_to_symbol_offset(sym, lv.offset, ctx->locals, dst->size);
         return;
      }
      if (lv.is_bitfield || lv.indirect || lv.needs_runtime_address || lv.is_absolute_ref) {
         int tmp_size = dst->size > 0 ? dst->size : expr_value_size(rhs, ctx);
         if (tmp_size <= 0) {
            tmp_size = 1;
         }
         ContextEntry tmp = { .name = "$tmp", .type = dst->type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = tmp_size };
         int saved_locals = ctx ? ctx->locals : 0;
         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         if (ctx) {
            ctx->locals = saved_locals + tmp_size;
         }
         if (!compile_expr_to_slot(rhs, ctx, &tmp)) {
            if (ctx) {
               ctx->locals = saved_locals;
            }
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            error_unreachable("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
            return;
         }
         if (ctx) {
            ctx->locals = saved_locals;
         }
         if (!emit_copy_fp_to_lvalue(ctx, &lv, tmp.offset, tmp.size)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            error_unreachable("[%s:%d.%d] assignment target not compiled yet", node->file, node->line, node->column);
            return;
         }
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
      }
      else if (!compile_expr_to_slot(rhs, ctx, dst)) {
         error_unreachable("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
      }
      return;
   }

   rhs = (ASTNode *) unwrap_expr_node(rhs);
   if (!rhs) {
      error_unreachable("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
      return;
   }

   if (!strcmp(op, "+=") || !strcmp(op, "-=") || !strcmp(op, "&=") || !strcmp(op, "|=") ||
       !strcmp(op, "^=") || !strcmp(op, "*=") || !strcmp(op, "/=") || !strcmp(op, "%=") ||
       !strcmp(op, "<<=") || !strcmp(op, ">>=")) {
      const char *base_op = NULL;
      const ASTNode *arg_types[2];
      const ASTNode *arg_decls[2];
      const ASTNode *arg_exprs[2] = { node->children[0], rhs };
      bool arg_lvalues[2];
      const ASTNode *ofn = NULL;
      char opname[32];

      if (!strcmp(op, "+=")) {
         base_op = "+";
      }
      else if (!strcmp(op, "-=")) {
         base_op = "-";
      }
      else if (!strcmp(op, "&=")) {
         base_op = "&";
      }
      else if (!strcmp(op, "|=")) {
         base_op = "|";
      }
      else if (!strcmp(op, "^=")) {
         base_op = "^";
      }
      else if (!strcmp(op, "*=")) {
         base_op = "*";
      }
      else if (!strcmp(op, "/=")) {
         base_op = "/";
      }
      else if (!strcmp(op, "%=")) {
         base_op = "%";
      }
      else if (!strcmp(op, "<<=")) {
         base_op = "<<";
      }
      else if (!strcmp(op, ">>=")) {
         base_op = ">>";
      }

      if (base_op) {
         arg_types[0] = dst->type;
         arg_decls[0] = dst->declarator;
         arg_lvalues[0] = true;
         expr_match_signature(rhs, ctx, &arg_types[1], &arg_decls[1]);
         arg_lvalues[1] = resolve_ref_argument_lvalue(ctx, rhs, NULL);
         if (arg_types[0] && arg_types[1]) {
            snprintf(opname, sizeof(opname), "operator%s", base_op);
            ofn = lookup_operator_overload(opname, 2, arg_types, arg_decls, arg_lvalues, arg_exprs);
         }
      }

      if (!ofn && base_op) {
         const ASTNode *exact_type = NULL;
         const ASTNode *other_type = NULL;
         if (mixed_exactops_value_types(dst->type, dst->declarator, arg_types[1], arg_decls[1], &exact_type, &other_type)) {
            const char *exact_name = type_name_from_node(exact_type);
            const char *other_name = type_name_from_node(other_type);
            error_user("[%s:%d.%d] type '%s' uses '$exactops' and cannot participate in mixed-type operator '%s' with type '%s'",
                       node->file, node->line, node->column,
                       exact_name && *exact_name ? exact_name : "<unnamed>",
                       base_op,
                       other_name && *other_name ? other_name : "<unnamed>");
         }
         if (same_named_value_type(dst->type, dst->declarator, arg_types[1], arg_decls[1]) && type_has_exactops(dst->type)) {
            error_user("[%s:%d.%d] type '%s' uses '$exactops' and requires visible overload '%s' for same-type operands",
                       node->file, node->line, node->column, type_name_from_node(dst->type), opname);
         }
      }

      if (ofn) {
         const ASTNode *rtype = function_return_type(ofn);
         const ASTNode *rdecl = function_declarator_node(ofn);
         int rsize = declarator_storage_size(rtype, rdecl);
         int dst_size = dst->size > 0 ? dst->size : rsize;
         ContextEntry tmp;
         ASTNode *argv[2] = { node->children[1], rhs };
         ASTNode *call = NULL;

         if (rsize <= 0) {
            rsize = type_size_from_node(rtype);
         }
         if (rsize <= 0) {
            error_user("[%s:%d.%d] overloaded compound assignment '%s' has unknown return size", node->file, node->line, node->column, op);
         }
         if (dst_size <= 0) {
            dst_size = rsize;
         }

         tmp = (ContextEntry){ .name = "$tmp", .type = rtype, .declarator = rdecl, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = rsize };
         call = make_synthetic_call_expr(node, declarator_name(function_declarator_node(ofn)), argv, 2);
         if (!call) {
            error_unreachable("[%s:%d.%d] overloaded compound assignment '%s' not compiled yet", node->file, node->line, node->column, op);
            return;
         }

         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", rsize & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         if (!compile_call_expr_to_slot(call, ctx, &tmp)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", rsize & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            error_unreachable("[%s:%d.%d] overloaded compound assignment '%s' not compiled yet", node->file, node->line, node->column, op);
            return;
         }

         if (!lv.is_bitfield && !lv.indirect && !lv.needs_runtime_address && (dst->is_static || dst->is_zeropage || dst->is_global)) {
            char sym[256];
            if (!entry_symbol_name(ctx, dst, sym, sizeof(sym))) {
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", rsize & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
               error_unreachable("[%s:%d.%d] compound assignment target not compiled yet", node->file, node->line, node->column);
               return;
            }
            if (dst_size != rsize || dst->type != rtype) {
               int store_offset = ctx->locals + rsize;
               remember_runtime_import("pushN");
               emit(&es_code, "    lda #$%02x\n", dst_size & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _pushN\n");
               emit_copy_fp_to_fp_convert(store_offset, dst_size, dst->type, tmp.offset, rsize, rtype);
               emit_copy_fp_to_symbol_offset(sym, lv.offset, store_offset, dst_size);
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", dst_size & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
            }
            else {
               emit_copy_fp_to_symbol_offset(sym, lv.offset, tmp.offset, dst_size);
            }
         }
         else if (lv.is_bitfield || lv.indirect || lv.needs_runtime_address) {
            if (dst_size != rsize || dst->type != rtype) {
               int store_offset = ctx->locals + rsize;
               remember_runtime_import("pushN");
               emit(&es_code, "    lda #$%02x\n", dst_size & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _pushN\n");
               emit_copy_fp_to_fp_convert(store_offset, dst_size, dst->type, tmp.offset, rsize, rtype);
               if (!emit_copy_fp_to_lvalue(ctx, &lv, store_offset, dst_size)) {
                  remember_runtime_import("popN");
                  emit(&es_code, "    lda #$%02x\n", dst_size & 0xff);
                  emit(&es_code, "    sta arg0\n");
                  emit(&es_code, "    jsr _popN\n");
                  error_unreachable("[%s:%d.%d] compound assignment target not compiled yet", node->file, node->line, node->column);
                  return;
               }
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", dst_size & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
            }
            else {
               if (!emit_copy_fp_to_lvalue(ctx, &lv, tmp.offset, dst_size)) {
                  remember_runtime_import("popN");
                  emit(&es_code, "    lda #$%02x\n", rsize & 0xff);
                  emit(&es_code, "    sta arg0\n");
                  emit(&es_code, "    jsr _popN\n");
                  error_unreachable("[%s:%d.%d] compound assignment target not compiled yet", node->file, node->line, node->column);
                  return;
               }
            }
         }
         else {
            emit_copy_fp_to_fp_convert(dst->offset, dst_size, dst->type, tmp.offset, rsize, rtype);
         }

         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", rsize & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return;
      }
   }

   if (!strcmp(op, "+=") || !strcmp(op, "-=") || !strcmp(op, "&=") || !strcmp(op, "|=") ||
       !strcmp(op, "^=") || !strcmp(op, "*=") || !strcmp(op, "/=") || !strcmp(op, "%=") ||
       !strcmp(op, "<<=") || !strcmp(op, ">>=")) {
      char dst_sym[256];
      bool dst_symbol = !lv.is_bitfield && !lv.indirect && !lv.needs_runtime_address && (dst->is_static || dst->is_zeropage || dst->is_global) && entry_symbol_name(ctx, dst, dst_sym, sizeof(dst_sym));
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

      if (dst->type && rhs_type && !expr_is_literal_node(rhs) && ordinary_integer_endian_conflict(dst->type, rhs_type)) {
         error_user("[%s:%d.%d] mixed-endian ordinary integer operator '%c' is not supported; use an explicit cast or matching endianness",
                    node->file, node->line, node->column, op ? op[0] : '?');
      }

      if (scaled_pointer_assign) {
         require_no_mixed_endian_pointer_index_expr(node, rhs, ctx, op);
         work_type = dst->type;
         rhs_slot_type = expr_is_literal_node(rhs) ? work_type : (rhs_type ? rhs_type : work_type);
         work_size = dst->size;
         pointer_scale = declarator_first_element_size(dst->type, dst->declarator);
         if (pointer_scale <= 0) {
            pointer_scale = 1;
         }
      }
      else if (!strcmp(op, "<<=") || !strcmp(op, ">>=")) {
         work_type = dst->type ? dst->type : rhs_type;
         rhs_slot_type = expr_is_literal_node(rhs) ? work_type : (rhs_type ? rhs_type : work_type);
         work_size = work_type ? type_size_from_node(work_type) : 0;
         rhs_work_size = rhs_slot_type ? type_size_from_node(rhs_slot_type) : 0;
      }
      else {
         if (dst->type && rhs_type && type_is_promotable_integer(dst->type) && type_is_promotable_integer(rhs_type) &&
             !type_has_exactops(dst->type) && !type_has_exactops(rhs_type) &&
             !type_is_bool(dst->type) && !type_is_bool(rhs_type) &&
             !type_is_float_like(dst->type) && !type_is_float_like(rhs_type) &&
             !expr_is_literal_node(rhs) && type_is_signed_integer(dst->type) != type_is_signed_integer(rhs_type)) {
            error_user("[%s:%d.%d] mixed signed/unsigned ordinary integer operator '%c' requires an explicit cast",
                       node->file, node->line, node->column, op ? op[0] : '?');
         }
         if (dst->type && rhs_type && !expr_is_literal_node(rhs) && ordinary_integer_endian_conflict(dst->type, rhs_type)) {
            error_user("[%s:%d.%d] mixed-endian ordinary integer operator '%c' is not supported; use an explicit cast or matching endianness",
                       node->file, node->line, node->column, op ? op[0] : '?');
         }
         work_type = compound_integer_work_type(dst->type, dst->declarator, rhs, ctx, node);
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

      if (!strcmp(op, "<<=") || !strcmp(op, ">>=")) {
         diagnose_constant_shift_count(rhs, work_size * 8);
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

      need_store_tmp = dst_symbol || lv.is_bitfield || lv.indirect || lv.needs_runtime_address;
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
         emit_copy_symbol_to_fp_convert_offset(lhs_tmp_offset, work_size, work_type, dst_sym, lv.offset, dst->size, dst->type);
      }
      else if (lv.is_bitfield || lv.indirect || lv.needs_runtime_address) {
         int lhs_src_size = dst->size < work_size ? dst->size : work_size;
         if (!emit_copy_lvalue_to_fp(ctx, lhs_tmp_offset, &lv, lhs_src_size)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            error_unreachable("[%s:%d.%d] compound assignment target not compiled yet", node->file, node->line, node->column);
            return;
         }
         emit_copy_fp_to_fp_convert(lhs_tmp_offset, work_size, work_type, lhs_tmp_offset, lhs_src_size, dst->type);
      }
      else {
         emit_copy_fp_to_fp_convert(lhs_tmp_offset, work_size, work_type, dst->offset, dst->size, dst->type);
      }

      if (ctx) {
         ctx->locals = lhs_tmp_offset + tmp_total;
      }
      if (!compile_expr_to_slot(rhs, ctx, &rhs_tmp)) {
         if (ctx) {
            ctx->locals = lhs_tmp_offset;
         }
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         error_unreachable("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
         return;
      }
      if (ctx) {
         ctx->locals = lhs_tmp_offset;
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
         emit_runtime_binary_fp_fp(int_mul_helper_name(factor_type ? factor_type : work_type), scaled_rhs_offset, rhs_tmp_offset, factor_offset, work_size);
         rhs_value_offset = int_mul_result_offset(factor_type ? factor_type : work_type, scaled_rhs_offset, work_size);
      }

      if ((!strcmp(op, "+=") || !strcmp(op, "-=")) && work_type && type_is_float_like(work_type)) {
         int expbits = type_float_expbits(work_type);
         if (expbits < 0) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            error_user("[%s:%d.%d] unsupported float style/size for runtime arithmetic", node->file, node->line, node->column);
            return;
         }
         emit_runtime_float_binary_fp_fp(!strcmp(op, "+=") ? "faddN" : "fsubN", lhs_tmp_offset, lhs_tmp_offset, rhs_value_offset, work_size, expbits);
      }
      else if (!strcmp(op, "+=")) {
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
         if (work_type && type_is_float_like(work_type)) {
            int expbits = type_float_expbits(work_type);
            if (expbits < 0) {
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
               error_user("[%s:%d.%d] unsupported float style/size for runtime arithmetic", node->file, node->line, node->column);
               return;
            }
            emit_runtime_float_binary_fp_fp("fmulN", aux_offset, lhs_tmp_offset, rhs_tmp_offset, work_size, expbits);
         }
         else {
            emit_runtime_binary_fp_fp(int_mul_helper_name(work_type), aux_offset, lhs_tmp_offset, rhs_tmp_offset, work_size);
         }
         emit_copy_fp_to_fp(lhs_tmp_offset, int_mul_result_offset(work_type, aux_offset, work_size), work_size);
      }
      else if (!strcmp(op, "/=") || !strcmp(op, "%=")) {
         if (!strcmp(op, "/=") && work_type && type_is_float_like(work_type)) {
            int expbits = type_float_expbits(work_type);
            if (expbits < 0) {
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
               error_user("[%s:%d.%d] unsupported float style/size for runtime arithmetic", node->file, node->line, node->column);
               return;
            }
            emit_runtime_float_binary_fp_fp("fdivN", aux_offset, lhs_tmp_offset, rhs_tmp_offset, work_size, expbits);
            emit_copy_fp_to_fp(lhs_tmp_offset, aux_offset, work_size);
         }
         else {
            int quo_offset = aux_offset;
            int rem_offset = aux_offset + work_size;
            emit_prepare_fp_ptr(0, lhs_tmp_offset);
            emit_prepare_fp_ptr(1, rhs_tmp_offset);
            emit_prepare_fp_ptr(2, quo_offset);
            emit_prepare_fp_ptr(3, rem_offset);
            emit(&es_code, "    lda #$%02x\n", work_size & 0xff);
            emit(&es_code, "    sta arg0\n");
            remember_runtime_import(int_div_helper_name(work_type));
            emit(&es_code, "    jsr _%s\n", int_div_helper_name(work_type));
            emit_copy_fp_to_fp(lhs_tmp_offset, !strcmp(op, "/=") ? quo_offset : rem_offset, work_size);
         }
      }
      else if (!strcmp(op, "<<=") || !strcmp(op, ">>=")) {
         helper = int_shift_helper_name(work_type, !strcmp(op, "<<="));
         emit_runtime_shift_fp(helper, lhs_tmp_offset, aux_offset, rhs_tmp_offset, rhs_slot_type, rhs_work_size, work_size);
         emit_copy_fp_to_fp(lhs_tmp_offset, aux_offset, work_size);
      }
      else {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         error_unreachable("[%s:%d.%d] expression '%s' not compiled yet", node->file, node->line, node->column, op);
         return;
      }

      if (need_store_tmp) {
         emit_copy_fp_to_fp_convert(store_offset, dst->size, dst->type, lhs_tmp_offset, work_size, work_type);
         if (dst_symbol) {
            emit_copy_fp_to_symbol_offset(dst_sym, lv.offset, store_offset, dst->size);
         }
         else {
            if (!emit_copy_fp_to_lvalue(ctx, &lv, store_offset, dst->size)) {
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
               error_unreachable("[%s:%d.%d] compound assignment target not compiled yet", node->file, node->line, node->column);
               return;
            }
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

   error_unreachable("[%s:%d.%d] expression '%s' not compiled yet", node->file, node->line, node->column, op ? op : "?");
}


