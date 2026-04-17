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
#include "compile_decl.h"
#include "compile_init.h"
#include "compile_internal.h"
#include "compile_lvalue.h"
#include "compile_overload.h"
#include "compile_stmt.h"
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
Pair *enumbackings = NULL;

Set *globals = NULL;
Set *functions = NULL;
Set *runtime_imports = NULL;
Set *imported_symbols = NULL;
Set *string_literals = NULL;
static int label_counter = 0;

const char *next_label(const char *prefix);




typedef struct CallGraphNode {
   const ASTNode *fn;
   char *sym;
   bool has_static_params;
} CallGraphNode;

typedef struct CallGraphEdge {
   int from;
   int to;
} CallGraphEdge;

typedef struct VaListLayout {
   const ASTNode *type;
   int size;
   int args_offset;
   int args_size;
   int bytes_offset;
   int bytes_size;
   int offset_offset;
   int offset_size;
} VaListLayout;

#define SYMBOL_BACKED_META_PREFIX "__sbpmeta$"
#define VARIADIC_HIDDEN_ARGS_NAME "__va_args"
#define VARIADIC_HIDDEN_BYTES_NAME "__va_arg_bytes"
#define BUILTIN_VA_START_NAME "va_start"
#define BUILTIN_VA_ARG_NAME "va_arg"
#define BUILTIN_VA_END_NAME "va_end"
#define BUILTIN_VA_LIST_TYPE_NAME "va_list"
#define BUILTIN_VA_LIST_ARGS_FIELD "args"
#define BUILTIN_VA_LIST_BYTES_FIELD "bytes"
#define BUILTIN_VA_LIST_OFFSET_FIELD "offset"


static CallGraphNode *call_graph_nodes = NULL;
static int call_graph_node_count = 0;
static CallGraphEdge *call_graph_edges = NULL;
static int call_graph_edge_count = 0;
int current_call_graph_node = -1;
const ASTNode *current_call_graph_function = NULL;

const ASTNode *global_decl_lookup(const char *name);
bool entry_symbol_name(Context *ctx, const ContextEntry *entry, char *buf, size_t bufsize);
void emit_copy_fp_to_fp(int dst_offset, int src_offset, int size);

void remember_runtime_import(const char *name);
void remember_symbol_import(const char *name);
static bool function_parameter_symbol_name(const ASTNode *fn, const ASTNode *parameter, int index,
                                           char *buf, size_t bufsize, bool *is_zeropage_out);
static ASTNode *make_synthetic_call_expr(ASTNode *origin, const char *callee_name, ASTNode *args[], int argc);
static ASTNode *make_synthetic_incdec_operand(ASTNode *origin);
static bool entry_has_read_address(const ContextEntry *entry);
static bool entry_has_write_address(const ContextEntry *entry);
static bool entry_is_absolute_ref(const ContextEntry *entry);
bool init_context_entry_from_global_decl(ContextEntry *entry, const char *name, const ASTNode *g);
static void add_variadic_hidden_locals(Context *ctx);
void emit_variadic_hidden_local_setup(const ASTNode *node, Context *ctx);
static bool variadic_hidden_name_reserved(const char *name);
void validate_nonreserved_variadic_name(const char *name, const ASTNode *node);
void validate_function_nonreserved_variadic_names(const ASTNode *fn);
static bool builtin_variadic_call_name(const char *name);
static bool get_builtin_va_list_layout(VaListLayout *out);
static bool compile_builtin_va_start_expr(ASTNode *expr, Context *ctx);
static bool compile_builtin_va_arg_expr(ASTNode *expr, Context *ctx);
static bool compile_builtin_va_end_expr(ASTNode *expr, Context *ctx);
const ASTNode *cast_expr_target_type(const ASTNode *expr);
const ASTNode *cast_expr_target_declarator(const ASTNode *expr);
static int cast_expr_target_size(const ASTNode *expr);
bool compile_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst);
bool compile_condition_branch_false(ASTNode *expr, Context *ctx, const char *false_label);
const char *next_label(const char *prefix);
void compile_expr(ASTNode *node, Context *ctx);
bool function_has_static_parameters(const ASTNode *fn);
static bool compile_indirect_call_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst,
                                               ASTNode *callee, ASTNode *args,
                                               const ASTNode *ret_type,
                                               const ASTNode *callable_decl);
int call_graph_node_index_for_function(const ASTNode *fn);
static void record_call_graph_edge(const ASTNode *caller, const ASTNode *callee);
static bool symbol_backed_metadata_function_name(char *buf, size_t bufsize, const char *sym);
static bool symbol_backed_metadata_edge_name(char *buf, size_t bufsize, const char *caller_sym, const char *callee_sym);
static void emit_symbol_backed_call_graph_metadata(void);
static void analyze_static_parameter_call_graph(void);
static bool is_identifier_spelling(const char *s);
bool compile_constant_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst);
bool eval_constant_initializer_expr(ASTNode *expr, InitConstValue *out);
void emit_prepare_fp_ptr(int ptrno, int offset);
void emit_add_fp_to_ptr(int ptrno, int src_offset, int src_size);
void emit_load_address_to_ptr(int ptrno, const char *symbol, int addend);
const char *assembler_address_expr(const char *expr, char *buf, size_t buf_size);
void emit_load_expr_address_to_ptr(int ptrno, const char *expr, int addend);
void emit_load_ptr_from_symbol(int ptrno, const char *symbol, int addend);
void emit_deref_ptr(int ptrno);
static int expr_byte_index(const ASTNode *type, int size, int i);
void emit_fill_fp_bytes(int dst_offset, int start, int count, unsigned char value);
void emit_runtime_fill_ptr1(int count, unsigned char value);
const char *runtime_copy_convert_helper_name(int dst_size, const ASTNode *dst_type, int src_size, const ASTNode *src_type);
void emit_runtime_copy_ptr0_to_ptr1(const char *helper, int src_size, int dst_size);
void emit_store_immediate_to_fp(int dst_offset, const unsigned char *bytes, int size);
bool decode_char_constant_value(const char *text, long long *value_out);
void emit_copy_fp_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, int src_offset, int src_size, const ASTNode *src_type);
static void emit_copy_symbol_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, const char *symbol, int src_size, const ASTNode *src_type);
bool emit_copy_lvalue_to_fp(Context *ctx, int dst_offset, const LValueRef *src, int size);
bool emit_copy_fp_to_lvalue(Context *ctx, const LValueRef *dst, int src_offset, int size);
void emit_runtime_binary_fp_fp(const char *helper, int dst_offset, int lhs_offset, int rhs_offset, int size);
void emit_runtime_fixed_binary_fp_fp(const char *helper, int dst_offset, int lhs_offset, int rhs_offset);
const char *int_addsub_helper_name(const ASTNode *type, int size, bool subtract, bool *is_generic_out);
static unsigned char *decode_string_literal_bytes(const char *text, int *out_len) {
   size_t raw_len;
   unsigned char *buf;

   if (!text) {
      text = "";
   }
   raw_len = strlen(text);
   buf = (unsigned char *) malloc(raw_len + 1);
   if (!buf) {
      error_unreachable("out of memory");
   }
   if (raw_len > 0) {
      memcpy(buf, text, raw_len);
   }
   buf[raw_len] = 0;

   if (out_len) {
      *out_len = (int) raw_len;
   }
   return buf;
}

bool string_literal_is_char_constant(const char *text) {
   size_t len;

   if (!text) {
      return false;
   }
   len = strlen(text);
   return len >= 2 && text[0] == '\'' && text[len - 1] == '\'';
}

bool decode_char_constant_value(const char *text, long long *value_out) {
   unsigned char *bytes;
   char *raw;
   size_t len;
   int count = 0;
   bool ok = false;

   if (!string_literal_is_char_constant(text) || !value_out) {
      return false;
   }
   len = strlen(text);
   raw = (char *) malloc(len > 1 ? len - 1 : 1);
   if (!raw) {
      error_unreachable("out of memory");
   }
   memcpy(raw, text + 1, len - 2);
   raw[len - 2] = '\0';
   bytes = decode_string_literal_bytes(raw, &count);
   free(raw);
   if (bytes && count == 1) {
      *value_out = bytes[0];
      ok = true;
   }
   free(bytes);
   return ok;
}

const char *remember_string_literal(const char *text);
static const char *emit_data_literal_object(const unsigned char *bytes, int size);
static const char *emit_data_string_object(const char *text);
bool pointer_initializer_uses_backing_object(const ASTNode *type, const ASTNode *declarator, const ASTNode *expr);
const char *emit_pointer_initializer_backing_object(const ASTNode *type, const ASTNode *declarator, const ASTNode *expr);
static void emit_store_label_address_to_fp(int dst_offset, int dst_size, const char *label);

ContextEntry *ctx_lookup(Context *ctx, const char *name) {
   return ctx ? (ContextEntry *) set_get(ctx->vars, name) : NULL;
}


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

static bool function_parameter_symbol_name(const ASTNode *fn, const ASTNode *parameter, int index,
                                           char *buf, size_t bufsize, bool *is_zeropage_out) {
   const ASTNode *ptype;
   const ASTNode *pdecl;
   const ASTNode *decl_specs;
   const ASTNode *modifiers;
   const char *pname;
   char callee_sym[256];
   Context callee_ctx;
   ContextEntry pentry;

   if (!fn || !parameter || !buf || bufsize == 0 || !parameter_has_symbol_storage(parameter)) {
      return false;
   }

   ptype = parameter_type(parameter);
   pdecl = parameter_declarator(parameter);
   decl_specs = parameter_decl_specifiers(parameter);
   modifiers = (decl_specs && decl_specs->count > 0) ? decl_specs->children[0] : NULL;
   pname = parameter_name(parameter, index);
   if (!ptype || !pname) {
      return false;
   }

   if (!function_symbol_name(fn, declarator_name(function_declarator_node(fn)), callee_sym, sizeof(callee_sym))) {
      return false;
   }

   memset(&callee_ctx, 0, sizeof(callee_ctx));
   callee_ctx.name = callee_sym;

   pentry.name = (char *) pname;
   pentry.type = ptype;
   pentry.declarator = pdecl;
   pentry.is_static = has_modifier((ASTNode *) modifiers, "static") || modifiers_imply_named_nonzeropage(modifiers);
   pentry.is_zeropage = modifiers_imply_zeropage(modifiers);
   pentry.is_global = false;
   pentry.is_ref = parameter_is_ref(parameter);
   pentry.is_absolute_ref = false;
   pentry.read_expr = NULL;
   pentry.write_expr = NULL;
   pentry.offset = 0;
   pentry.size = declarator_storage_size(ptype, pdecl);

   if (is_zeropage_out) {
      *is_zeropage_out = pentry.is_zeropage;
   }

   return entry_symbol_name(&callee_ctx, &pentry, buf, bufsize);
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

static const ASTNode *decl_node_declarator(const ASTNode *node) {
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

static bool entry_has_read_address(const ContextEntry *entry) {
   return entry && entry->is_absolute_ref && entry->read_expr && *entry->read_expr;
}

static bool entry_has_write_address(const ContextEntry *entry) {
   return entry && entry->is_absolute_ref && entry->write_expr && *entry->write_expr;
}

static bool entry_is_absolute_ref(const ContextEntry *entry) {
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

static void emit_copy_fp_to_symbol_offset(const char *symbol, int symbol_offset, int src_offset, int size) {
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


static bool emit_copy_lvalue_to_symbol(Context *ctx, const char *symbol, int symbol_offset, const LValueRef *src, int size) {
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

const char *remember_string_literal(const char *text) {
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
      error_unreachable("out of memory");
   }
   snprintf(label, 64, "__str_%d", label_counter++);
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

static const char *emit_data_literal_object(const unsigned char *bytes, int size) {
   char *label;

   if (size < 0) {
      return NULL;
   }

   label = (char *) malloc(64);
   if (!label) {
      error_unreachable("out of memory");
   }
   snprintf(label, 64, "__data_%d", label_counter++);
   emit(&es_data, "%s:\n", label);
   if (size <= 0) {
      emit(&es_data, "\t.byte $00\n");
      return label;
   }
   emit_initializer_bytes_line(&es_data, bytes, size);
   return label;
}

static const char *emit_data_string_object(const char *text) {
   unsigned char *bytes;
   unsigned char *buf;
   const char *label;
   int n = 0;

   if (!text) {
      text = "";
   }
   bytes = decode_string_literal_bytes(text, &n);
   buf = (unsigned char *) calloc((size_t) n + 1u, 1);
   if (!buf) {
      free(bytes);
      error_unreachable("out of memory");
   }
   if (n > 0) {
      memcpy(buf, bytes, (size_t) n);
   }
   free(bytes);
   label = emit_data_literal_object(buf, n + 1);
   free(buf);
   return label;
}

bool pointer_initializer_uses_backing_object(const ASTNode *type, const ASTNode *declarator, const ASTNode *expr) {
   const ASTNode *uexpr = unwrap_expr_node((ASTNode *) expr);

   (void) type;
   if (!uexpr || is_empty(uexpr) || !declarator) {
      return false;
   }
   if (declarator_function_pointer_depth(declarator) > 0) {
      return false;
   }
   if (declarator_pointer_depth(declarator) <= 0 && (!type || strcmp(type_name_from_node(type), "*"))) {
      return false;
   }
   if (uexpr->kind == AST_STRING && !string_literal_is_char_constant(uexpr->strval)) {
      return true;
   }
   if (uexpr->count == 1 && !strcmp(uexpr->name, "&")) {
      ASTNode *inner = (ASTNode *) unwrap_expr_node(uexpr->children[0]);
      InitConstValue value = {0};
      const char *ident = expr_bare_identifier_name(inner);

      if (inner && !strcmp(inner->name, "lvalue")) {
         return false;
      }
      if (ident && resolve_function_designator_target(ident, NULL, NULL)) {
         return false;
      }
      return eval_constant_initializer_expr(inner, &value) && value.kind == INIT_CONST_INT;
   }
   return false;
}

const char *emit_pointer_initializer_backing_object(const ASTNode *type, const ASTNode *declarator, const ASTNode *expr) {
   const ASTNode *uexpr = unwrap_expr_node((ASTNode *) expr);

   if (!pointer_initializer_uses_backing_object(type, declarator, uexpr)) {
      return NULL;
   }

   if (uexpr->kind == AST_STRING) {
      return emit_data_string_object(uexpr->strval);
   }

   if (uexpr->count == 1 && !strcmp(uexpr->name, "&")) {
      ASTNode *inner = (ASTNode *) unwrap_expr_node(uexpr->children[0]);
      InitConstValue value = {0};
      const ASTNode *obj_type = NULL;
      const ASTNode *obj_decl = NULL;
      const ASTNode *annotated = NULL;
      const char *obj_name = NULL;
      unsigned char *bytes;
      int obj_size = 0;

      if (!eval_constant_initializer_expr(inner, &value) || value.kind != INIT_CONST_INT) {
         return NULL;
      }

      if (declarator && declarator_pointer_depth(declarator) > 0) {
         obj_decl = declarator_after_deref(declarator);
         obj_type = type;
         obj_name = type ? type_name_from_node(type) : NULL;
         if (!obj_name || !strcmp(obj_name, "void") || !strcmp(obj_name, "*")) {
            obj_type = NULL;
            obj_decl = NULL;
         }
      }
      if (!obj_type) {
         annotated = literal_annotation_type(inner);
         if (annotated && type_size_from_node(annotated) > 0) {
            obj_type = annotated;
         }
         else {
            obj_type = required_typename_node("int");
         }
      }
      if (obj_decl) {
         obj_size = declarator_storage_size(obj_type, obj_decl);
      }
      else {
         obj_size = type_size_from_node(obj_type);
      }
      if (obj_size <= 0) {
         obj_type = required_typename_node("int");
         obj_decl = NULL;
         obj_size = type_size_from_node(obj_type);
      }
      bytes = (unsigned char *) calloc((size_t) obj_size, 1);
      if (!bytes) {
         error_unreachable("out of memory");
      }
      if (!encode_init_const_int_value(&value, bytes, obj_size, obj_type)) {
         free(bytes);
         return NULL;
      }
      {
         const char *label = emit_data_literal_object(bytes, obj_size);
         free(bytes);
         return label;
      }
   }

   return NULL;
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

bool emit_string_initializer_to_fp(const ASTNode *type, const ASTNode *declarator, int base_offset, int total_size, const char *text) {
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
      const char *label = emit_data_string_object(text);
      emit_store_label_address_to_fp(base_offset, total_size > 0 ? total_size : declarator_storage_size(type, declarator), label);
      return true;
   }
   return false;
}

bool emit_string_initializer_bytes(unsigned char *buf, int buf_size, int base_offset, const ASTNode *type, const ASTNode *declarator, int total_size, const char *text) {
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

static void emit_copy_symbol_to_fp_convert_offset(int dst_offset, int dst_size, const ASTNode *dst_type, const char *symbol, int src_offset, int src_size, const ASTNode *src_type) {
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

static void emit_copy_symbol_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, const char *symbol, int src_size, const ASTNode *src_type) {
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

static void remember_symbol_import_mode(const char *name, bool is_zeropage) {
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


static void ctx_shove(Context *ctx, const ASTNode *type, const char *name) {
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
   ctx->params -= entry->size;
   entry->offset = ctx->params;
   debug("[%s:%d] ctx_shove(%s, %s, %d, %d)", __FILE__, __LINE__, type->strval, name, entry->size, entry->offset);
   set_add(ctx->vars, strdup(name), entry);
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


static bool variadic_hidden_name_reserved(const char *name) {
   return name && (!strcmp(name, VARIADIC_HIDDEN_ARGS_NAME) || !strcmp(name, VARIADIC_HIDDEN_BYTES_NAME));
}

void validate_nonreserved_variadic_name(const char *name, const ASTNode *node) {
   if (!node || !variadic_hidden_name_reserved(name)) {
      return;
   }
   error_user("[%s:%d.%d] '%s' is a reserved implementation name", node->file, node->line, node->column, name);
}

void validate_function_nonreserved_variadic_names(const ASTNode *fn) {
   const ASTNode *declarator;
   const ASTNode *params;

   if (!fn) {
      return;
   }

   declarator = function_declarator_node(fn);
   if (declarator) {
      validate_nonreserved_variadic_name(declarator_name(declarator), fn);
      params = declarator_parameter_list(declarator);
      if (params && !is_empty(params)) {
         for (int i = 0; i < params->count; i++) {
            const ASTNode *parameter = params->children[i];
            const ASTNode *pdecl = parameter ? parameter_declarator(parameter) : NULL;
            validate_nonreserved_variadic_name(pdecl ? declarator_name(pdecl) : NULL, parameter ? parameter : fn);
         }
      }
   }
}

static bool builtin_variadic_call_name(const char *name) {
   return name && (!strcmp(name, BUILTIN_VA_START_NAME) || !strcmp(name, BUILTIN_VA_ARG_NAME) || !strcmp(name, BUILTIN_VA_END_NAME));
}

static bool get_builtin_va_list_layout(VaListLayout *out) {
   const ASTNode *type = NULL;
   AggregateMemberInfo info;
   int ptr_size = get_size("*");

   if (out) {
      memset(out, 0, sizeof(*out));
   }

   if (!typename_exists(BUILTIN_VA_LIST_TYPE_NAME)) {
      error_user("builtin variadic support requires type '%s'; include \"stdarg.n\"", BUILTIN_VA_LIST_TYPE_NAME);
   }

   type = required_typename_node(BUILTIN_VA_LIST_TYPE_NAME);
   if (!type) {
      return false;
   }
   if (!find_aggregate_member_info(type, BUILTIN_VA_LIST_ARGS_FIELD, &info)) {
      error_user("type '%s' must define member '%s'", BUILTIN_VA_LIST_TYPE_NAME, BUILTIN_VA_LIST_ARGS_FIELD);
   }
   if (type_name_from_node(info.type) == NULL || strcmp(type_name_from_node(info.type), "char") || declarator_pointer_depth(info.declarator) <= 0 || info.storage_size != ptr_size) {
      error_user("type '%s' member '%s' must be declared as 'char *'", BUILTIN_VA_LIST_TYPE_NAME, BUILTIN_VA_LIST_ARGS_FIELD);
   }

   if (out) {
      out->type = type;
      out->size = type_size_from_node(type);
      out->args_offset = info.byte_offset;
      out->args_size = info.storage_size;
   }

   if (!find_aggregate_member_info(type, BUILTIN_VA_LIST_BYTES_FIELD, &info)) {
      error_user("type '%s' must define member '%s'", BUILTIN_VA_LIST_TYPE_NAME, BUILTIN_VA_LIST_BYTES_FIELD);
   }
   if (type_name_from_node(info.type) == NULL || strcmp(type_name_from_node(info.type), "char") || declarator_pointer_depth(info.declarator) <= 0 || info.storage_size != ptr_size) {
      error_user("type '%s' member '%s' must be declared as 'char *'", BUILTIN_VA_LIST_TYPE_NAME, BUILTIN_VA_LIST_BYTES_FIELD);
   }
   if (out) {
      out->bytes_offset = info.byte_offset;
      out->bytes_size = info.storage_size;
   }

   if (!find_aggregate_member_info(type, BUILTIN_VA_LIST_OFFSET_FIELD, &info)) {
      error_user("type '%s' must define member '%s'", BUILTIN_VA_LIST_TYPE_NAME, BUILTIN_VA_LIST_OFFSET_FIELD);
   }
   if (type_name_from_node(info.type) == NULL || strcmp(type_name_from_node(info.type), "char") || declarator_pointer_depth(info.declarator) <= 0 || info.storage_size != ptr_size) {
      error_user("type '%s' member '%s' must be declared as 'char *'", BUILTIN_VA_LIST_TYPE_NAME, BUILTIN_VA_LIST_OFFSET_FIELD);
   }
   if (out) {
      out->offset_offset = info.byte_offset;
      out->offset_size = info.storage_size;
   }

   return true;
}

static void add_variadic_hidden_locals(Context *ctx) {
   ContextEntry *entry;
   ASTNode *ptr_decl;

   if (!ctx) {
      return;
   }

   ctx_push(ctx, required_typename_node("char"), VARIADIC_HIDDEN_ARGS_NAME);
   entry = (ContextEntry *) set_get(ctx->vars, VARIADIC_HIDDEN_ARGS_NAME);
   ptr_decl = make_named_pointer_declarator(VARIADIC_HIDDEN_ARGS_NAME);
   if (entry) {
      entry->declarator = ptr_decl;
      ctx_resize_last_push(ctx, required_typename_node("char"), ptr_decl, VARIADIC_HIDDEN_ARGS_NAME);
   }

   ctx_push(ctx, required_typename_node("*"), VARIADIC_HIDDEN_BYTES_NAME);
}

void build_function_context(const ASTNode *node, Context *ctx) {
   const ASTNode *declarator = node->children[1];
   const ASTNode *params = declarator_parameter_list(declarator);
   int i = 0;

   if (params && !is_empty(params)) {
      for (int j = 0; j < params->count; j++) {
         const ASTNode *parameter = params->children[j];
         const ASTNode *type = parameter_type(parameter);
         const char *name = parameter_name(parameter, i);
         const ASTNode *decl_specs = parameter_decl_specifiers(parameter);
         const ASTNode *modifiers = (decl_specs && decl_specs->count > 0) ? decl_specs->children[0] : NULL;
         const ASTNode *param_decl = call_adjusted_parameter_declarator(parameter_declarator(parameter), parameter_is_ref(parameter));
         int size;
         int slot_size;
         ContextEntry *entry;

         if (!type || parameter_is_void(parameter)) {
            continue;
         }

         size = declarator_storage_size(type, param_decl);
         slot_size = parameter_storage_size(parameter);
         if (has_modifier((ASTNode *) modifiers, "static") || modifiers_imply_named_nonzeropage(modifiers)) {
            ctx_static(ctx, type, name);
            entry = (ContextEntry *) set_get(ctx->vars, name);
            entry->size = slot_size;
            entry->declarator = param_decl;
            entry->is_ref = parameter_is_ref(parameter);
         }
         else if (modifiers_imply_zeropage(modifiers)) {
            ctx_zeropage(ctx, type, name);
            entry = (ContextEntry *) set_get(ctx->vars, name);
            entry->size = slot_size;
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

   if (parameter_list_is_variadic(params)) {
      ctx->params -= get_size("*") + get_size("*");
   }

   ctx_shove(ctx, node->children[0]->children[1], "$$");
   ctx_resize_last_shove(ctx, node->children[0]->children[1], declarator, "$$");

   if (parameter_list_is_variadic(params)) {
      add_variadic_hidden_locals(ctx);
   }
}

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


bool expr_is_ternary_node(const ASTNode *expr) {
   expr = unwrap_expr_node(expr);

   if (!expr) {
      return false;
   }

   if ((!strcmp(expr->name, "conditional_expr") || !strcmp(expr->name, "case_conditional_expr")) &&
       expr->count == 4 && expr->children[0] && expr->children[0]->kind == AST_IDENTIFIER &&
       !strcmp(expr->children[0]->strval, "?:")) {
      return true;
   }

   if (!strcmp(expr->name, "?:") && expr->count >= 3) {
      return true;
   }

   return false;
}

ASTNode *expr_ternary_test(ASTNode *expr) {
   expr = (ASTNode *) unwrap_expr_node(expr);

   if (!expr_is_ternary_node(expr)) {
      return NULL;
   }

   return (!strcmp(expr->name, "?:")) ? expr->children[0] : expr->children[1];
}

ASTNode *expr_ternary_true(ASTNode *expr) {
   expr = (ASTNode *) unwrap_expr_node(expr);

   if (!expr_is_ternary_node(expr)) {
      return NULL;
   }

   return (!strcmp(expr->name, "?:")) ? expr->children[1] : expr->children[2];
}

ASTNode *expr_ternary_false(ASTNode *expr) {
   expr = (ASTNode *) unwrap_expr_node(expr);

   if (!expr_is_ternary_node(expr)) {
      return NULL;
   }

   return (!strcmp(expr->name, "?:")) ? expr->children[2] : expr->children[3];
}

const ASTNode *cast_expr_target_type(const ASTNode *expr) {
   const ASTNode *cast_type;
   const ASTNode *specifiers;

   expr = unwrap_expr_node(expr);
   if (!expr || strcmp(expr->name, "cast") || expr->count < 2) {
      return NULL;
   }

   cast_type = expr->children[0];
   if (!cast_type || strcmp(cast_type->name, "cast_type") || cast_type->count < 2) {
      return NULL;
   }

   specifiers = cast_type->children[0];
   if (!specifiers || specifiers->count < 2) {
      return NULL;
   }

   return specifiers->children[1];
}

const ASTNode *cast_expr_target_declarator(const ASTNode *expr) {
   const ASTNode *cast_type;

   expr = unwrap_expr_node(expr);
   if (!expr || strcmp(expr->name, "cast") || expr->count < 2) {
      return NULL;
   }

   cast_type = expr->children[0];
   if (!cast_type || strcmp(cast_type->name, "cast_type") || cast_type->count < 2) {
      return NULL;
   }

   return cast_type->children[1];
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

bool function_has_static_parameters(const ASTNode *fn) {
   const ASTNode *declarator = function_declarator_node(fn);
   const ASTNode *params = declarator_parameter_list(declarator);

   if (!params || is_empty(params)) {
      return false;
   }

   for (int i = 0; i < params->count; i++) {
      const ASTNode *parameter = params->children[i];
      if (parameter_has_symbol_storage(parameter)) {
         return true;
      }
   }

   return false;
}

int call_graph_node_index_for_function(const ASTNode *fn) {
   char sym[256];

   if (!fn) {
      return -1;
   }

   for (int i = 0; i < call_graph_node_count; i++) {
      if (call_graph_nodes[i].fn == fn) {
         return i;
      }
   }

   if (!function_symbol_name(fn, declarator_name(function_declarator_node(fn)), sym, sizeof(sym))) {
      return -1;
   }

   call_graph_nodes = (CallGraphNode *) realloc(call_graph_nodes, sizeof(CallGraphNode) * (call_graph_node_count + 1));
   if (!call_graph_nodes) {
      error_unreachable("out of memory");
   }
   call_graph_nodes[call_graph_node_count].fn = fn;
   call_graph_nodes[call_graph_node_count].sym = strdup(sym);
   call_graph_nodes[call_graph_node_count].has_static_params = function_has_static_parameters(fn);
   return call_graph_node_count++;
}

static bool symbol_backed_metadata_function_name(char *buf, size_t bufsize, const char *sym) {
   if (!buf || bufsize == 0 || !sym || !*sym) {
      return false;
   }
   if ((size_t) snprintf(buf, bufsize, SYMBOL_BACKED_META_PREFIX "F$%s", sym) >= bufsize) {
      return false;
   }
   return true;
}

static bool symbol_backed_metadata_edge_name(char *buf, size_t bufsize, const char *caller_sym, const char *callee_sym) {
   if (!buf || bufsize == 0 || !caller_sym || !*caller_sym || !callee_sym || !*callee_sym) {
      return false;
   }
   if ((size_t) snprintf(buf, bufsize, SYMBOL_BACKED_META_PREFIX "E$%s$%s", caller_sym, callee_sym) >= bufsize) {
      return false;
   }
   return true;
}

static void record_call_graph_edge(const ASTNode *caller, const ASTNode *callee) {
   int from = call_graph_node_index_for_function(caller);
   int to = call_graph_node_index_for_function(callee);

   if (from < 0 || to < 0) {
      return;
   }

   for (int i = 0; i < call_graph_edge_count; i++) {
      if (call_graph_edges[i].from == from && call_graph_edges[i].to == to) {
         return;
      }
   }

   call_graph_edges = (CallGraphEdge *) realloc(call_graph_edges, sizeof(CallGraphEdge) * (call_graph_edge_count + 1));
   if (!call_graph_edges) {
      error_unreachable("out of memory");
   }
   call_graph_edges[call_graph_edge_count].from = from;
   call_graph_edges[call_graph_edge_count].to = to;
   call_graph_edge_count++;
}

static void call_graph_tarjan_visit(int v, int *index_counter, int *stack, int *stack_top,
                                    int *indices, int *lowlink, unsigned char *onstack,
                                    int *component, int *component_sizes, int *component_count) {
   indices[v] = *index_counter;
   lowlink[v] = *index_counter;
   (*index_counter)++;
   stack[(*stack_top)++] = v;
   onstack[v] = 1;

   for (int i = 0; i < call_graph_edge_count; i++) {
      if (call_graph_edges[i].from != v) {
         continue;
      }
      int w = call_graph_edges[i].to;
      if (indices[w] < 0) {
         call_graph_tarjan_visit(w, index_counter, stack, stack_top, indices, lowlink, onstack, component, component_sizes, component_count);
         if (lowlink[w] < lowlink[v]) {
            lowlink[v] = lowlink[w];
         }
      }
      else if (onstack[w] && indices[w] < lowlink[v]) {
         lowlink[v] = indices[w];
      }
   }

   if (lowlink[v] == indices[v]) {
      int cid = (*component_count)++;
      component_sizes[cid] = 0;
      for (;;) {
         int w = stack[--(*stack_top)];
         onstack[w] = 0;
         component[w] = cid;
         component_sizes[cid]++;
         if (w == v) {
            break;
         }
      }
   }
}

static void analyze_static_parameter_call_graph(void) {
   int n = call_graph_node_count;
   int *indices;
   int *lowlink;
   int *stack;
   int *component;
   int *component_sizes;
   unsigned char *onstack;
   unsigned char *component_has_static;
   unsigned char *component_has_cycle;
   int stack_top = 0;
   int index_counter = 0;
   int component_count = 0;

   if (n <= 0) {
      return;
   }

   indices = (int *) malloc(sizeof(int) * n);
   lowlink = (int *) malloc(sizeof(int) * n);
   stack = (int *) malloc(sizeof(int) * n);
   component = (int *) malloc(sizeof(int) * n);
   component_sizes = (int *) calloc(n, sizeof(int));
   onstack = (unsigned char *) calloc(n, sizeof(unsigned char));
   component_has_static = (unsigned char *) calloc(n, sizeof(unsigned char));
   component_has_cycle = (unsigned char *) calloc(n, sizeof(unsigned char));
   if (!indices || !lowlink || !stack || !component || !component_sizes || !onstack || !component_has_static || !component_has_cycle) {
      error_unreachable("out of memory");
   }

   for (int i = 0; i < n; i++) {
      indices[i] = -1;
      lowlink[i] = -1;
      component[i] = -1;
   }

   for (int i = 0; i < n; i++) {
      if (indices[i] < 0) {
         call_graph_tarjan_visit(i, &index_counter, stack, &stack_top, indices, lowlink, onstack, component, component_sizes, &component_count);
      }
   }

   for (int i = 0; i < n; i++) {
      if (component[i] >= 0 && call_graph_nodes[i].has_static_params) {
         component_has_static[component[i]] = 1;
      }
   }
   for (int i = 0; i < component_count; i++) {
      if (component_sizes[i] > 1) {
         component_has_cycle[i] = 1;
      }
   }
   for (int i = 0; i < call_graph_edge_count; i++) {
      if (component[call_graph_edges[i].from] == component[call_graph_edges[i].to]) {
         component_has_cycle[component[call_graph_edges[i].from]] = 1;
      }
   }

   for (int i = 0; i < component_count; i++) {
      if (!component_has_cycle[i] || !component_has_static[i]) {
         continue;
      }

      for (int j = 0; j < n; j++) {
         if (component[j] != i || !call_graph_nodes[j].has_static_params) {
            continue;
         }
         error_user("call graph cycle reaches function '%s' with symbol-backed parameters", declarator_name(function_declarator_node((ASTNode *) call_graph_nodes[j].fn)));
      }
   }

   free(indices);
   free(lowlink);
   free(stack);
   free(component);
   free(component_sizes);
   free(onstack);
   free(component_has_static);
   free(component_has_cycle);
}

static void emit_symbol_backed_call_graph_metadata(void) {
   char meta[768];

   for (int i = 0; i < call_graph_node_count; i++) {
      if (!call_graph_nodes[i].has_static_params || !function_has_body(call_graph_nodes[i].fn)) {
         continue;
      }
      if (!symbol_backed_metadata_function_name(meta, sizeof(meta), call_graph_nodes[i].sym)) {
         error_user("symbol-backed metadata name too long for function '%s'", call_graph_nodes[i].sym);
      }
      emit(&es_export, ".export %s\n", meta);
      emit(&es_export, "%s = 0\n", meta);
   }

   for (int i = 0; i < call_graph_edge_count; i++) {
      int from = call_graph_edges[i].from;
      int to = call_graph_edges[i].to;

      if (from < 0 || from >= call_graph_node_count || to < 0 || to >= call_graph_node_count) {
         continue;
      }
      if (!function_has_body(call_graph_nodes[from].fn)) {
         continue;
      }
      if (!symbol_backed_metadata_edge_name(meta, sizeof(meta), call_graph_nodes[from].sym, call_graph_nodes[to].sym)) {
         error_user("symbol-backed metadata edge name too long for '%s' -> '%s'", call_graph_nodes[from].sym, call_graph_nodes[to].sym);
      }
      emit(&es_export, ".export %s\n", meta);
      emit(&es_export, "%s = 0\n", meta);
   }
}

void emit_function_parameter_storage(const ASTNode *node, Context *ctx) {
   const ASTNode *declarator = node->children[1];
   const ASTNode *params = declarator_parameter_list(declarator);

   if (!params || is_empty(params)) {
      return;
   }

   for (int i = 0; i < params->count; i++) {
      const ASTNode *parameter = params->children[i];
      const ASTNode *type = parameter_type(parameter);
      const ASTNode *decl_specs = parameter_decl_specifiers(parameter);
      const ASTNode *modifiers = (decl_specs && decl_specs->count > 0) ? decl_specs->children[0] : NULL;
      const char *name = parameter_name(parameter, i);
      const ContextEntry *entry;
      char sym[256];

      if (!type || parameter_is_void(parameter) || !parameter_has_symbol_storage(parameter)) {
         continue;
      }

      entry = (const ContextEntry *) set_get(ctx->vars, name);
      if (!entry) {
         continue;
      }
      if (!entry_symbol_name(ctx, entry, sym, sizeof(sym))) {
         continue;
      }

      if (entry->is_zeropage) {
         emit(&es_zp, "%s:\n", sym);
         emit(&es_zp, "\t.res %d\n", entry->size);
      }
      else {
         char segbuf[256];
         build_named_storage_segment(segbuf, sizeof(segbuf), modifiers, "BSS");
         emit(&es_bss, ".segment \"%s\"\n", segbuf);
         emit(&es_bss, "%s:\n", sym);
         emit(&es_bss, "\t.res %d\n", entry->size);
      }
   }
}

void emit_function_parameter_exports(const ASTNode *node) {
   const ASTNode *declarator = node->children[1];
   const ASTNode *params = declarator_parameter_list(declarator);

   if (!params || is_empty(params)) {
      return;
   }

   for (int i = 0; i < params->count; i++) {
      const ASTNode *parameter = params->children[i];
      char sym[256];
      bool is_zeropage = false;

      if (!parameter || parameter_is_void(parameter) || !parameter_has_symbol_storage(parameter)) {
         continue;
      }
      if (!function_parameter_symbol_name(node, parameter, i, sym, sizeof(sym), &is_zeropage)) {
         continue;
      }
      emit(&es_export,
           is_zeropage ? ".zpexport %s\n" : ".export %s\n",
           sym);
   }
}

void emit_variadic_hidden_local_setup(const ASTNode *node, Context *ctx) {
   ContextEntry *args_entry;
   ContextEntry *bytes_entry;
   int ptr_size;
   int len_size;
   int fixed_stack_bytes;
   int hidden_ptr_offset;
   int hidden_len_offset;

   if (!node || !ctx || !function_is_variadic(node)) {
      return;
   }

   args_entry = (ContextEntry *) set_get(ctx->vars, VARIADIC_HIDDEN_ARGS_NAME);
   bytes_entry = (ContextEntry *) set_get(ctx->vars, VARIADIC_HIDDEN_BYTES_NAME);
   if (!args_entry || !bytes_entry) {
      return;
   }

   ptr_size = get_size("*");
   len_size = get_size("*");
   fixed_stack_bytes = function_fixed_parameter_stack_bytes(node);
   hidden_len_offset = -(fixed_stack_bytes + len_size);
   hidden_ptr_offset = -(fixed_stack_bytes + len_size + ptr_size);

   emit_load_ptr_from_fpvar(0, hidden_ptr_offset);
   emit_store_ptr_to_fp(args_entry->offset, 0, args_entry->size);
   emit_copy_fp_to_fp_convert(bytes_entry->offset, bytes_entry->size, bytes_entry->type,
                              hidden_len_offset, len_size, required_typename_node("*"));
}

static bool compile_builtin_va_start_expr(ASTNode *expr, Context *ctx) {
   ASTNode *args;
   LValueRef ap_lv;
   VaListLayout layout;
   ContextEntry *hidden_args;
   ContextEntry *hidden_bytes;
   int saved_locals;
   int scratch_offset;
   unsigned char zeroes[32] = {0};

   if (!expr || strcmp(expr->name, "()") || expr->count < 2) {
      return false;
   }
   args = expr->children[1];
   if (!args || is_empty(args) || args->count != 1) {
      error_user("[%s:%d.%d] %s expects exactly 1 argument", expr->file, expr->line, expr->column, BUILTIN_VA_START_NAME);
   }
   if (!ctx || !current_call_graph_function || !function_is_variadic(current_call_graph_function)) {
      error_user("[%s:%d.%d] %s may only be used inside a variadic function", expr->file, expr->line, expr->column, BUILTIN_VA_START_NAME);
   }
   hidden_args = (ContextEntry *) set_get(ctx->vars, VARIADIC_HIDDEN_ARGS_NAME);
   hidden_bytes = (ContextEntry *) set_get(ctx->vars, VARIADIC_HIDDEN_BYTES_NAME);
   if (!hidden_args || !hidden_bytes) {
      error_user("[%s:%d.%d] variadic metadata is unavailable in this function", expr->file, expr->line, expr->column);
   }
   if (!resolve_ref_argument_lvalue(ctx, args->children[0], &ap_lv)) {
      error_user("[%s:%d.%d] %s argument must be an lvalue", args->children[0]->file, args->children[0]->line, args->children[0]->column, BUILTIN_VA_START_NAME);
   }
   if (!get_builtin_va_list_layout(&layout)) {
      return false;
   }
   if (ap_lv.size != layout.size || !ap_lv.type || !type_name_from_node(ap_lv.type) || strcmp(type_name_from_node(ap_lv.type), BUILTIN_VA_LIST_TYPE_NAME)) {
      error_user("[%s:%d.%d] %s argument must have type '%s'", args->children[0]->file, args->children[0]->line, args->children[0]->column, BUILTIN_VA_START_NAME, BUILTIN_VA_LIST_TYPE_NAME);
   }

   if (layout.size > (int) sizeof(zeroes)) {
      error_unreachable("va_list too large for %s", BUILTIN_VA_START_NAME);
   }

   saved_locals = ctx->locals;
   scratch_offset = saved_locals;
   ctx->locals = scratch_offset + layout.size;
   emit_store_immediate_to_fp(scratch_offset, zeroes, layout.size);
   emit_copy_fp_to_fp(scratch_offset + layout.args_offset, hidden_args->offset, hidden_args->size);
   emit_copy_fp_to_fp_convert(scratch_offset + layout.bytes_offset, layout.bytes_size, required_typename_node("*"),
                              hidden_bytes->offset, hidden_bytes->size, hidden_bytes->type);
   emit_copy_fp_to_lvalue(ctx, &ap_lv, scratch_offset, layout.size);
   ctx->locals = saved_locals;
   return true;
}

static bool compile_builtin_va_arg_expr(ASTNode *expr, Context *ctx) {
   ASTNode *args;
   LValueRef ap_lv;
   LValueRef out_lv;
   VaListLayout layout;
   int ptr_size = get_size("*");
   int saved_locals;
   int ap_tmp;
   int src_tmp;
   int out_tmp;
   int out_size;
   unsigned char add_bytes[16] = {0};

   if (!expr || strcmp(expr->name, "()") || expr->count < 2) {
      return false;
   }
   args = expr->children[1];
   if (!args || is_empty(args) || args->count != 2) {
      error_user("[%s:%d.%d] %s expects exactly 2 arguments", expr->file, expr->line, expr->column, BUILTIN_VA_ARG_NAME);
   }
   if (!resolve_ref_argument_lvalue(ctx, args->children[0], &ap_lv)) {
      error_user("[%s:%d.%d] first %s argument must be an lvalue", args->children[0]->file, args->children[0]->line, args->children[0]->column, BUILTIN_VA_ARG_NAME);
   }
   if (!resolve_ref_argument_lvalue(ctx, args->children[1], &out_lv)) {
      error_user("[%s:%d.%d] second %s argument must be an lvalue", args->children[1]->file, args->children[1]->line, args->children[1]->column, BUILTIN_VA_ARG_NAME);
   }
   if (!get_builtin_va_list_layout(&layout)) {
      return false;
   }
   if (ap_lv.size != layout.size || !ap_lv.type || !type_name_from_node(ap_lv.type) || strcmp(type_name_from_node(ap_lv.type), BUILTIN_VA_LIST_TYPE_NAME)) {
      error_user("[%s:%d.%d] first %s argument must have type '%s'", args->children[0]->file, args->children[0]->line, args->children[0]->column, BUILTIN_VA_ARG_NAME, BUILTIN_VA_LIST_TYPE_NAME);
   }
   out_size = out_lv.size;
   if (out_size <= 0) {
      error_user("[%s:%d.%d] second %s argument has no runtime storage", args->children[1]->file, args->children[1]->line, args->children[1]->column, BUILTIN_VA_ARG_NAME);
   }
   if (ptr_size > (int) sizeof(add_bytes)) {
      error_unreachable("pointer size too large for %s", BUILTIN_VA_ARG_NAME);
   }

   saved_locals = ctx->locals;
   ap_tmp = saved_locals;
   src_tmp = ap_tmp + layout.size;
   out_tmp = src_tmp + ptr_size;
   ctx->locals = out_tmp + out_size;

   emit_copy_lvalue_to_fp(ctx, ap_tmp, &ap_lv, layout.size);
   emit_copy_fp_to_fp(src_tmp, ap_tmp + layout.args_offset, layout.args_size);
   emit_add_fp_to_fp(required_typename_node("*"), src_tmp, ap_tmp + layout.offset_offset, ptr_size);
   emit_load_ptr_from_fpvar(0, src_tmp);
   for (int i = 0; i < out_size; i++) {
      emit(&es_code, "    ldy #%d\n", i);
      emit(&es_code, "    lda (ptr0),y\n");
      emit(&es_code, "    ldy #%d\n", out_tmp + i);
      emit(&es_code, "    sta (fp),y\n");
   }
   emit_copy_fp_to_lvalue(ctx, &out_lv, out_tmp, out_size);
   {
      char add_buf[32];
      snprintf(add_buf, sizeof(add_buf), "%d", out_size);
      if (type_is_big_endian(required_typename_node("*"))) {
         make_be_int(add_buf, add_bytes, ptr_size);
      }
      else {
         make_le_int(add_buf, add_bytes, ptr_size);
      }
   }
   emit_add_immediate_to_fp(required_typename_node("*"), ap_tmp + layout.offset_offset, add_bytes, ptr_size);
   emit_copy_fp_to_lvalue(ctx, &ap_lv, ap_tmp, layout.size);
   ctx->locals = saved_locals;
   return true;
}

static bool compile_builtin_va_end_expr(ASTNode *expr, Context *ctx) {
   ASTNode *args;
   LValueRef ap_lv;
   VaListLayout layout;
   int saved_locals;
   int scratch_offset;
   unsigned char zeroes[32] = {0};

   if (!expr || strcmp(expr->name, "()") || expr->count < 2) {
      return false;
   }
   args = expr->children[1];
   if (!args || is_empty(args) || args->count != 1) {
      error_user("[%s:%d.%d] %s expects exactly 1 argument", expr->file, expr->line, expr->column, BUILTIN_VA_END_NAME);
   }
   if (!resolve_ref_argument_lvalue(ctx, args->children[0], &ap_lv)) {
      error_user("[%s:%d.%d] %s argument must be an lvalue", args->children[0]->file, args->children[0]->line, args->children[0]->column, BUILTIN_VA_END_NAME);
   }
   if (!get_builtin_va_list_layout(&layout)) {
      return false;
   }
   if (layout.size > (int) sizeof(zeroes)) {
      error_unreachable("va_list too large for %s", BUILTIN_VA_END_NAME);
   }
   if (ap_lv.size != layout.size || !ap_lv.type || !type_name_from_node(ap_lv.type) || strcmp(type_name_from_node(ap_lv.type), BUILTIN_VA_LIST_TYPE_NAME)) {
      error_user("[%s:%d.%d] %s argument must have type '%s'", args->children[0]->file, args->children[0]->line, args->children[0]->column, BUILTIN_VA_END_NAME, BUILTIN_VA_LIST_TYPE_NAME);
   }

   saved_locals = ctx->locals;
   scratch_offset = saved_locals;
   ctx->locals = scratch_offset + layout.size;
   emit_store_immediate_to_fp(scratch_offset, zeroes, layout.size);
   emit_copy_fp_to_lvalue(ctx, &ap_lv, scratch_offset, layout.size);
   ctx->locals = saved_locals;
   return true;
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

static bool is_identifier_spelling(const char *s) {
   int i;

   if (!s || !*s) {
      return false;
   }
   if (!((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z') || s[0] == '_')) {
      return false;
   }
   for (i = 1; s[i]; i++) {
      if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '_')) {
         return false;
      }
   }
   return true;
}

const char *expr_bare_identifier_name(ASTNode *expr) {
   ASTNode *base;

   expr = (ASTNode *) unwrap_expr_node(expr);
   if (!expr || is_empty(expr)) {
      return NULL;
   }
   if (expr->kind == AST_IDENTIFIER) {
      return expr->strval;
   }
   if (strcmp(expr->name, "lvalue") || expr->count != 2) {
      return NULL;
   }

   base = expr->children[0];
   if (!base || strcmp(base->name, "lvalue_base") || base->count <= 0 || !base->children[0] || base->children[0]->kind != AST_IDENTIFIER) {
      return NULL;
   }
   if (!expr->children[1] || !is_empty(expr->children[1])) {
      return NULL;
   }

   return base->children[0]->strval;
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


const ASTNode *expr_value_type(ASTNode *expr, Context *ctx) {
   const ASTNode *lhs_type;
   const ASTNode *rhs_type;

   expr = (ASTNode *) unwrap_expr_node(expr);

   if (!expr || is_empty(expr)) {
      return NULL;
   }

   if (expr->kind == AST_INTEGER) {
      const ASTNode *annotated = literal_annotation_type(expr);
      if (annotated) {
         return annotated;
      }
      if (typename_exists("int")) {
         return required_typename_node("int");
      }
      return NULL;
   }

   if (expr->kind == AST_FLOAT) {
      const ASTNode *annotated = literal_annotation_type(expr);
      if (annotated) {
         return annotated;
      }
      if (typename_exists("float")) {
         return required_typename_node("float");
      }
      return NULL;
   }

   if (expr->kind == AST_STRING) {
      if (string_literal_is_char_constant(expr->strval)) {
         return required_typename_node("char");
      }
      return required_typename_node("*");
   }

   if (!strcmp(expr->name, "cast")) {
      return cast_expr_target_type(expr);
   }

   if (!strcmp(expr->name, "flag_cast")) {
      return flag_cast_target_type(expr, ctx);
   }

   if (!strcmp(expr->name, "sizeof")) {
      return required_typename_node("int");
   }

   {
      const char *ident = expr_bare_identifier_name(expr);
      if (ident) {
         ContextEntry *entry = ctx_lookup(ctx, ident);
         if (entry) {
            return entry->type;
         }
         {
            const ASTNode *g = global_decl_lookup(ident);
            if (g && g->count >= 3) {
               return g->children[1];
            }
         }
         {
            const ASTNode *fn = resolve_function_designator_target(ident, NULL, NULL);
            if (fn) {
               return function_return_type(fn);
            }
         }
      }
   }

   if (expr->count == 1 && !strcmp(expr->name, "&")) {
      LValueRef lv;
      ASTNode *inner = (ASTNode *) unwrap_expr_node(expr->children[0]);
      if (inner && !strcmp(inner->name, "lvalue") && resolve_lvalue(ctx, inner, &lv)) {
         return lv.type;
      }
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
      {
         const char *callee_name = expr_bare_identifier_name(callee);
         if (callee_name && builtin_variadic_call_name(callee_name)) {
            return required_typename_node("void");
         }
         if (callee_name) {
            fn = resolve_function_call_target(callee_name, expr, args, ctx);
         }
      }
      if (fn) {
         const ASTNode *ret = function_return_type(fn);
         if (ret) {
            return ret;
         }
      }
      else {
         const ASTNode *callable_decl = expr_value_declarator(callee, ctx);
         const ASTNode *callable_type = expr_value_type(callee, ctx);
         if (callable_decl && declarator_has_parameter_list(callable_decl) && declarator_function_pointer_depth(callable_decl) > 0) {
            return callable_type;
         }
      }
   }

   if (!strcmp(expr->name, "comma_expr") && expr->count > 0) {
      return expr_value_type(expr->children[expr->count - 1], ctx);
   }

   if (expr_is_ternary_node(expr)) {
      lhs_type = expr_value_type(expr_ternary_true(expr), ctx);
      rhs_type = expr_value_type(expr_ternary_false(expr), ctx);
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
         if (expr_is_literal_node(expr->children[0]) && rhs_type && type_is_promotable_integer(rhs_type) &&
             !type_is_bool(rhs_type) && !type_has_exactops(rhs_type) && !type_is_float_like(rhs_type)) {
            return rhs_type;
         }
         return lhs_type ? lhs_type : rhs_type;
      }
      {
         const ASTNode *work_type = binary_integer_work_type(expr->children[0], expr->children[1], ctx, expr);
         if (work_type) {
            return work_type;
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

const ASTNode *expr_value_declarator(ASTNode *expr, Context *ctx) {
   expr = (ASTNode *) unwrap_expr_node(expr);

   if (!expr || is_empty(expr)) {
      return NULL;
   }

   {
      const char *ident = expr_bare_identifier_name(expr);
      if (ident) {
         ContextEntry *entry = ctx_lookup(ctx, ident);
         if (entry) {
            return entry->declarator;
         }
         {
            const ASTNode *g = global_decl_lookup(ident);
            if (g && g->count >= 3) {
               return g->children[2];
            }
         }
         {
            const ASTNode *fn = resolve_function_designator_target(ident, NULL, NULL);
            if (fn) {
               return function_pointer_declarator_from_callable(function_declarator_node(fn));
            }
         }
      }
   }

   if (!strcmp(expr->name, "cast")) {
      return cast_expr_target_declarator(expr);
   }

   if (!strcmp(expr->name, "flag_cast")) {
      return flag_cast_target_declarator(expr, ctx);
   }

   if (!strcmp(expr->name, "sizeof")) {
      return NULL;
   }

   if (!strcmp(expr->name, "lvalue") && expr->count > 0) {
      LValueRef lv;
      if (resolve_lvalue(ctx, expr, &lv)) {
         return lv.declarator;
      }
   }

   if (expr->count == 1 && !strcmp(expr->name, "&")) {
      LValueRef lv;
      ASTNode *inner = (ASTNode *) unwrap_expr_node(expr->children[0]);
      if (inner && !strcmp(inner->name, "lvalue") && resolve_lvalue(ctx, inner, &lv) && lv.declarator) {
         const ASTNode *value_decl = declarator_value_declarator(lv.declarator);
         int start = declarator_suffix_start_index(value_decl ? value_decl : lv.declarator);
         return clone_declarator_variant(value_decl ? value_decl : lv.declarator,
               declarator_pointer_depth(lv.declarator) + 1, start);
      }
      {
         const char *ident = expr_bare_identifier_name(inner);
         const ASTNode *fn = ident ? resolve_function_designator_target(ident, NULL, NULL) : NULL;
         if (fn) {
            return function_pointer_declarator_from_callable(function_declarator_node(fn));
         }
      }
   }

   if (!strcmp(expr->name, "()")) {
      ASTNode *callee = expr->children[0];
      ASTNode *args = (expr->count > 1) ? expr->children[1] : NULL;
      const ASTNode *fn = NULL;
      {
         const char *callee_name = expr_bare_identifier_name(callee);
         if (callee_name) {
            fn = resolve_function_call_target(callee_name, expr, args, ctx);
         }
      }
      if (fn) {
         return function_return_declarator_from_callable(function_declarator_node(fn));
      }
      else {
         const ASTNode *callable_decl = expr_value_declarator(callee, ctx);
         if (callable_decl && declarator_has_parameter_list(callable_decl) && declarator_function_pointer_depth(callable_decl) > 0) {
            return function_return_declarator_from_callable(callable_decl);
         }
      }
   }

   if (!strcmp(expr->name, "comma_expr") && expr->count > 0) {
      return expr_value_declarator(expr->children[expr->count - 1], ctx);
   }

   if (expr_is_ternary_node(expr)) {
      return expr_value_declarator(expr_ternary_true(expr), ctx);
   }

   return NULL;
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


static void compile(ASTNode *program) {

   if (!program) {
      error_unreachable("internal NULL program node");
      // error calls exit()
   }

   if (strcmp(program->name, "program")) {
      error_unreachable("internal non program node '%s' [%s:%d.%d]",
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
      if (!strcmp(node->name, "enum_decl_stmt")) {
         node->handled = true;
         compile_enum_decl_stmt(node);
      }
   }

   if (!typename_exists("bool")) {
      error_unreachable("type bool is not defined");
   }
   if (!typename_exists("void")) {
      error_unreachable("type void is not defined");
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
         error_unreachable("[%s:%d.%d] unrecognized AST node '%s'",
               node->file, node->line, node->column,
               node->name);
         // error calls exit()
      }
   }
}

void do_compile(FILE *out) {

   typesizes = pair_create();
   enumbackings = pair_create();

   emit(&es_header, "; this file produced by \"n65cc\" compiler\n");
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
   analyze_static_parameter_call_graph();
   emit_symbol_backed_call_graph_metadata();
   emit_runtime_global_init_function();
   emit_peephole_optimize(&es_code);

   emit_print(&es_header, out);
   fprintf(out, "\n");

   emit_print(&es_import, out);
   fprintf(out, "\n");

   emit_print(&es_export, out);
   fprintf(out, "\n");

   emit_print(&es_zp, out);
   fprintf(out, "\n");

   emit_print(&es_zpdata, out);
   fprintf(out, "\n");

   emit_print(&es_bss, out);
   fprintf(out, "\n");

   emit_print(&es_data, out);
   fprintf(out, "\n");

   emit_print(&es_rodata, out);
   fprintf(out, "\n");

   emit_print(&es_code, out);
}
