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
static Pair *enumbackings = NULL;

Set *globals = NULL;
Set *functions = NULL;
Set *runtime_imports = NULL;
Set *imported_symbols = NULL;
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

static const char *next_label(const char *prefix);

typedef struct OperatorOverload {
   const char *name;
   const ASTNode *node;
} OperatorOverload;

typedef struct OrdinaryFunction {
   const char *name;
   const ASTNode *node;
} OrdinaryFunction;

static OperatorOverload *operator_overloads = NULL;
static int operator_overload_count = 0;
static OrdinaryFunction *ordinary_functions = NULL;
static int ordinary_function_count = 0;

typedef struct ContextEntry {
   const char *name;
   const ASTNode *type;
   const ASTNode *declarator;
   bool is_static;
   bool is_zeropage;
   bool is_global;
   bool is_ref;
   bool is_absolute_ref;
   const char *read_expr;
   const char *write_expr;
   int offset;
   int size;
} ContextEntry;

typedef struct LValueRef {
   const char *name;
   const ASTNode *type;
   const ASTNode *declarator;
   const ASTNode *base_type;
   const ASTNode *base_declarator;
   const ASTNode *suffixes;
   bool is_static;
   bool is_zeropage;
   bool is_global;
   bool is_ref;
   bool is_absolute_ref;
   const char *read_expr;
   const char *write_expr;
   bool indirect;
   int deref_depth;
   bool needs_runtime_address;
   int base_offset;
   int offset;
   int size;
   int ptr_adjust;
   bool is_bitfield;
   int bit_offset;
   int bit_width;
   int bit_storage_size;
} LValueRef;

typedef struct AggregateMemberInfo {
   const ASTNode *type;
   const ASTNode *declarator;
   int byte_offset;
   int bit_offset;
   int bit_width;
   int storage_size;
   bool is_bitfield;
} AggregateMemberInfo;

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
   const char *int_text;
} InitConstValue;

typedef struct PendingGlobalInit {
   const char *name;
   const char *symbol;
   const ASTNode *type;
   const ASTNode *declarator;
   ASTNode *expression;
   int size;
   bool is_zeropage;
   bool is_absolute_ref;
   const char *read_expr;
   const char *write_expr;
} PendingGlobalInit;

static PendingGlobalInit *pending_global_inits = NULL;
static int pending_global_init_count = 0;
static int pending_global_init_max_size = 0;
static char runtime_global_init_symbol_buf[64];
static bool runtime_global_init_symbol_ready = false;

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

typedef enum LValueAccessMode {
   LVALUE_ACCESS_READ = 0,
   LVALUE_ACCESS_WRITE,
   LVALUE_ACCESS_ADDRESS
} LValueAccessMode;

static CallGraphNode *call_graph_nodes = NULL;
static int call_graph_node_count = 0;
static CallGraphEdge *call_graph_edges = NULL;
static int call_graph_edge_count = 0;
static int current_call_graph_node = -1;
static const ASTNode *current_call_graph_function = NULL;

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
static bool type_is_big_endian(const ASTNode *type);
static int endian_mem_index_for_significance(int size, bool big_endian, int significance_index);
static const ASTNode *promoted_integer_type_for_binary(const ASTNode *lhs_type, const ASTNode *rhs_type, ASTNode *origin);
static const ASTNode *literal_annotation_type(const ASTNode *expr);
static int type_size_from_node(const ASTNode *type);
static const char *find_mem_modifier_name(const ASTNode *modifiers);
static const ASTNode *find_mem_modifier_node(const ASTNode *modifiers);
static bool mem_decl_is_zeropage(const ASTNode *mem_decl);
static bool modifiers_imply_zeropage(const ASTNode *modifiers);
static bool modifiers_imply_mem_storage(const ASTNode *modifiers);
static bool modifiers_imply_named_nonzeropage(const ASTNode *modifiers);
static void build_named_storage_segment(char *buf, size_t bufsize, const ASTNode *modifiers, const char *base_segment);
static int integer_literal_min_size(const ASTNode *expr);
static bool has_flag(const char *type, const char *flag);
static bool has_flag_prefix(const char *type, const char *prefix);
static const char *enum_backing_type_name(const char *type);
static const char *parse_float_style_flag_text(const char *text);
static bool type_is_float_like(const ASTNode *type);
static const char *type_float_style(const ASTNode *type);
static int type_float_expbits(const ASTNode *type);
static bool has_modifier(ASTNode *node, const char *modifier);
static void emit_copy_fp_to_fp(int dst_offset, int src_offset, int size);
static const ASTNode *function_modifiers_node(const ASTNode *fn);
static bool function_has_body(const ASTNode *fn);
static bool function_same_signature(const ASTNode *a, const ASTNode *b);
static bool function_same_declaration(const ASTNode *a, const ASTNode *b);
static bool declarator_is_plain_value(const ASTNode *declarator);
static bool integer_type_can_represent_type(const ASTNode *formal_type, const ASTNode *actual_type);
static int integer_promotion_conversion_cost(const ASTNode *actual_type, const ASTNode *actual_decl,
                                             const ASTNode *formal_type, const ASTNode *formal_decl);
static const ASTNode *declarator_value_declarator(const ASTNode *declarator);
static const ASTNode *clone_declarator_variant(const ASTNode *declarator, int new_ptr_depth, int first_array_child);
static void expr_match_signature(ASTNode *expr, Context *ctx, const ASTNode **type_out, const ASTNode **decl_out);
static int parameter_argument_conversion_cost(const ASTNode *ptype, const ASTNode *pdecl, bool pref,
                                              const ASTNode *atype, const ASTNode *adecl, bool arg_lvalue);

static void remember_function(const ASTNode *node, const char *name);
static bool is_operator_function_name(const char *name);
static bool ordinary_function_name_is_overloaded(const char *name);
static bool function_symbol_name(const ASTNode *fn, const char *fallback_name, char *buf, size_t bufsize);
static bool format_user_asm_symbol(const char *name, char *buf, size_t bufsize);
static void remember_runtime_import(const char *name);
static void remember_symbol_import(const char *name);
static bool function_parameter_symbol_name(const ASTNode *fn, const ASTNode *parameter, int index,
                                           char *buf, size_t bufsize, bool *is_zeropage_out);
static const ASTNode *resolve_function_designator_target(const char *name, const ASTNode *expected_type, const ASTNode *expected_decl);
static ASTNode *make_synthetic_call_expr(ASTNode *origin, const char *callee_name, ASTNode *args[], int argc);
static ASTNode *make_synthetic_incdec_operand(ASTNode *origin);
static bool classify_incdec_lvalue_expr(ASTNode *expr, bool *inc, bool *pre);
static const ASTNode *resolve_function_call_target(const char *name, ASTNode *args, Context *ctx);
static const ASTNode *resolve_operator_overload_expr(ASTNode *expr, Context *ctx);
static const ASTNode *resolve_incdec_overload_expr(ASTNode *expr, Context *ctx);
static const ASTNode *resolve_truthiness_overload(ASTNode *expr, Context *ctx);
static const ASTNode *parameter_type(const ASTNode *parameter);
static const ASTNode *parameter_declarator(const ASTNode *parameter);
static const ASTNode *parameter_decl_specifiers(const ASTNode *parameter);
static const char *parameter_name(const ASTNode *parameter, int i);
static bool parameter_has_symbol_storage(const ASTNode *parameter);
static const ASTNode *decl_subitem_declarator(const ASTNode *node);
static const ASTNode *decl_subitem_address_spec(const ASTNode *node);
static const ASTNode *decl_node_declarator(const ASTNode *node);
static const ASTNode *decl_node_address_spec(const ASTNode *node);
static const char *address_spec_read_expr(const ASTNode *node);
static const char *address_spec_write_expr(const ASTNode *node);
static bool address_spec_has_read(const ASTNode *node);
static bool address_spec_has_write(const ASTNode *node);
static bool entry_has_read_address(const ContextEntry *entry);
static bool entry_has_write_address(const ContextEntry *entry);
static bool entry_is_absolute_ref(const ContextEntry *entry);
static bool init_context_entry_from_global_decl(ContextEntry *entry, const char *name, const ASTNode *g);
static bool parameter_is_ref(const ASTNode *parameter);
static bool parameter_is_ellipsis(const ASTNode *parameter);
static int parameter_storage_size(const ASTNode *parameter);
static bool parameter_is_void(const ASTNode *parameter);
static bool parameter_list_is_variadic(const ASTNode *params);
static bool function_is_variadic(const ASTNode *fn);
static int fixed_parameter_stack_bytes_from_params(const ASTNode *params);
static int function_fixed_parameter_stack_bytes(const ASTNode *fn);
static void add_variadic_hidden_locals(Context *ctx);
static void emit_variadic_hidden_local_setup(const ASTNode *node, Context *ctx);
static bool variadic_hidden_name_reserved(const char *name);
static void validate_nonreserved_variadic_name(const char *name, const ASTNode *node);
static void validate_function_nonreserved_variadic_names(const ASTNode *fn);
static bool builtin_variadic_call_name(const char *name);
static bool get_builtin_va_list_layout(VaListLayout *out);
static bool compile_builtin_va_start_expr(ASTNode *expr, Context *ctx);
static bool compile_builtin_va_arg_expr(ASTNode *expr, Context *ctx);
static bool compile_builtin_va_end_expr(ASTNode *expr, Context *ctx);
static bool resolve_ref_argument_lvalue(Context *ctx, ASTNode *expr, LValueRef *out);
static bool emit_prepare_lvalue_ptr(Context *ctx, const LValueRef *lv, LValueAccessMode mode);
static const ASTNode *unwrap_expr_node(const ASTNode *expr);
static const ASTNode *cast_expr_target_type(const ASTNode *expr);
static const ASTNode *cast_expr_target_declarator(const ASTNode *expr);
static int cast_expr_target_size(const ASTNode *expr);
static bool eval_constant_cast_expr(ASTNode *expr, InitConstValue *out);
static void predeclare_top_level_functions(ASTNode *program);
static int declarator_storage_size(const ASTNode *type, const ASTNode *declarator);
static int declarator_value_size(const ASTNode *type, const ASTNode *declarator);
static int declarator_pointer_depth(const ASTNode *declarator);
static int declarator_array_count(const ASTNode *declarator);
static int declarator_first_element_size(const ASTNode *type, const ASTNode *declarator);
static const ASTNode *declarator_after_subscript(const ASTNode *declarator);
static const ASTNode *declarator_after_deref(const ASTNode *declarator);
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
static void compile_local_decl_item(ASTNode *node, Context *ctx);
static void compile_label_stmt(ASTNode *node, Context *ctx);
static void compile_goto_stmt(ASTNode *node, Context *ctx);
static void compile_switch_stmt(ASTNode *node, Context *ctx);
static void compile_return_stmt(ASTNode *node, Context *ctx);
static void compile_asm_stmt(ASTNode *node, Context *ctx);
static void compile_expr(ASTNode *node, Context *ctx);
static const ASTNode *function_return_type(const ASTNode *fn);
static const ASTNode *function_declarator_node(const ASTNode *fn);
static int declarator_pointer_node_count(const ASTNode *declarator);
static const ASTNode *declarator_name_node(const ASTNode *declarator);
static const char *declarator_name(const ASTNode *declarator);
static int declarator_suffix_start_index(const ASTNode *declarator);
static const ASTNode *declarator_parameter_list(const ASTNode *declarator);
static bool declarator_has_parameter_list(const ASTNode *declarator);
static int declarator_function_pointer_depth(const ASTNode *declarator);
static const ASTNode *function_pointer_declarator_from_callable(const ASTNode *declarator);
static const ASTNode *function_return_declarator_from_callable(const ASTNode *declarator);
static bool function_has_static_parameters(const ASTNode *fn);
static bool compile_indirect_call_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst,
                                               ASTNode *callee, ASTNode *args,
                                               const ASTNode *ret_type,
                                               const ASTNode *callable_decl);
static int call_graph_node_index_for_function(const ASTNode *fn);
static void record_call_graph_edge(const ASTNode *caller, const ASTNode *callee);
static bool symbol_backed_metadata_function_name(char *buf, size_t bufsize, const char *sym);
static bool symbol_backed_metadata_edge_name(char *buf, size_t bufsize, const char *caller_sym, const char *callee_sym);
static void emit_symbol_backed_call_graph_metadata(void);
static void analyze_static_parameter_call_graph(void);
static bool is_identifier_spelling(const char *s);
static const char *expr_bare_identifier_name(ASTNode *expr);
static bool declarator_is_function(const ASTNode *declarator);
static bool resolve_lvalue(Context *ctx, ASTNode *node, LValueRef *out);
static void calculate_struct_union_sizes(ASTNode *program);
static bool compile_initializer_to_fp(const ASTNode *init, Context *ctx, const ASTNode *type, const ASTNode *declarator, int base_offset, int total_size);
static bool build_initializer_bytes(unsigned char *buf, int buf_size, int base_offset, const ASTNode *init, const ASTNode *type, const ASTNode *declarator, int total_size);
static bool compile_constant_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst);
static bool eval_constant_initializer_expr(ASTNode *expr, InitConstValue *out);
static int constant_shift_width_bits(ASTNode *expr);
static const ASTNode *declarator_bitfield_node(const ASTNode *declarator);
static int declarator_bitfield_width(const ASTNode *declarator);
static bool find_aggregate_member_info(const ASTNode *type, const char *member, AggregateMemberInfo *out);
static bool emit_copy_bitfield_lvalue_to_fp(Context *ctx, int dst_offset, const LValueRef *src, int size);
static bool emit_copy_fp_to_bitfield_lvalue(Context *ctx, const LValueRef *dst, int src_offset, int size);
static void diagnose_constant_shift_count(ASTNode *count_expr, int lhs_bits);
static long long arithmetic_right_shift_ll(long long value, unsigned int count);
static bool encode_integer_initializer_value(long long value, unsigned char *buf, int size, const ASTNode *type);
static bool encode_init_const_int_value(const InitConstValue *value, unsigned char *buf, int size, const ASTNode *type);
static bool encode_float_initializer_value(double value, unsigned char *buf, int size, const ASTNode *type);
static bool emit_symbol_address_initializer(EmitSink *es, int size, const ASTNode *type, const char *symbol, long long addend);
static void emit_initializer_bytes_line(EmitSink *es, const unsigned char *bytes, int size);
static bool emit_global_initializer(EmitSink *es, const ASTNode *type, const ASTNode *declarator, ASTNode *expression, int size);
static void emit_sink_append(EmitSink *dst, const EmitSink *src);
static void remember_pending_global_init(const char *name, const char *symbol, const ASTNode *type, const ASTNode *declarator, ASTNode *expression, int size, bool is_zeropage, bool is_absolute_ref, const char *read_expr, const char *write_expr);
static const char *runtime_global_init_symbol(void);
static void emit_runtime_global_init_function(void);
static const ASTNode *expr_value_type(ASTNode *expr, Context *ctx);
static int expr_value_size(ASTNode *expr, Context *ctx);
static const ASTNode *expr_value_declarator(ASTNode *expr, Context *ctx);
static void emit_prepare_fp_ptr(int ptrno, int offset);
static void emit_add_fp_to_ptr(int ptrno, int src_offset, int src_size);
static void emit_load_address_to_ptr(int ptrno, const char *symbol, int addend);
static const char *assembler_address_expr(const char *expr, char *buf, size_t buf_size);
static void emit_load_expr_address_to_ptr(int ptrno, const char *expr, int addend);
static void emit_load_ptr_from_symbol(int ptrno, const char *symbol, int addend);
static void emit_deref_ptr(int ptrno);
static int expr_byte_index(const ASTNode *type, int size, int i);
static void emit_fill_fp_bytes(int dst_offset, int start, int count, unsigned char value);
static void emit_runtime_fill_ptr1(int count, unsigned char value);
static const char *runtime_copy_convert_helper_name(int dst_size, const ASTNode *dst_type, int src_size, const ASTNode *src_type);
static void emit_runtime_copy_ptr0_to_ptr1(const char *helper, int src_size, int dst_size);
static void emit_store_immediate_to_fp(int dst_offset, const unsigned char *bytes, int size);
static int scalar_storage_size(const ASTNode *type, const ASTNode *declarator, int fallback);
static bool string_literal_is_char_constant(const char *text);
static bool decode_char_constant_value(const char *text, long long *value_out);
static void emit_copy_fp_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, int src_offset, int src_size, const ASTNode *src_type);
static void emit_copy_symbol_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, const char *symbol, int src_size, const ASTNode *src_type);
static bool emit_copy_lvalue_to_fp(Context *ctx, int dst_offset, const LValueRef *src, int size);
static bool emit_copy_fp_to_lvalue(Context *ctx, const LValueRef *dst, int src_offset, int size);
static void emit_runtime_binary_fp_fp(const char *helper, int dst_offset, int lhs_offset, int rhs_offset, int size);
static void emit_runtime_fixed_binary_fp_fp(const char *helper, int dst_offset, int lhs_offset, int rhs_offset);
static const char *int_addsub_helper_name(const ASTNode *type, int size, bool subtract, bool *is_generic_out);
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
      error_unreachable("out of memory");
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

static bool string_literal_is_char_constant(const char *text) {
   size_t len;

   if (!text) {
      return false;
   }
   len = strlen(text);
   return len >= 2 && text[0] == '\'' && text[len - 1] == '\'';
}

static bool decode_char_constant_value(const char *text, long long *value_out) {
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

static const char *remember_string_literal(const char *text);
static const char *emit_data_literal_object(const unsigned char *bytes, int size);
static const char *emit_data_string_object(const char *text);
static bool pointer_initializer_uses_backing_object(const ASTNode *type, const ASTNode *declarator, const ASTNode *expr);
static const char *emit_pointer_initializer_backing_object(const ASTNode *type, const ASTNode *declarator, const ASTNode *expr);
static void emit_store_label_address_to_fp(int dst_offset, int dst_size, const char *label);
static bool emit_string_initializer_to_fp(const ASTNode *type, const ASTNode *declarator, int base_offset, int total_size, const char *text);
static bool emit_string_initializer_bytes(unsigned char *buf, int buf_size, int base_offset, const ASTNode *type, const ASTNode *declarator, int total_size, const char *text);

static ContextEntry *ctx_lookup(Context *ctx, const char *name) {
   return ctx ? (ContextEntry *) set_get(ctx->vars, name) : NULL;
}



static bool is_operator_function_name(const char *name) {
   return name && !strncmp(name, "operator", 8);
}

static const ASTNode *function_modifiers_node(const ASTNode *fn) {
   if (!fn) {
      return NULL;
   }
   if (fn->count == 3 && fn->children[0] && fn->children[0]->count > 0) {
      return fn->children[0]->children[0];
   }
   if (fn->count == 4) {
      return fn->children[0];
   }
   return NULL;
}

static bool function_has_body(const ASTNode *fn) {
   return fn && fn->count == 3;
}

static bool parameter_is_ellipsis(const ASTNode *parameter) {
   return parameter && parameter->name && !strcmp(parameter->name, "ellipsis");
}

static bool parameter_list_is_variadic(const ASTNode *params) {
   if (!params || is_empty(params)) {
      return false;
   }

   for (int i = 0; i < params->count; i++) {
      if (parameter_is_ellipsis(params->children[i])) {
         return true;
      }
   }

   return false;
}

static bool function_is_variadic(const ASTNode *fn) {
   const ASTNode *declarator = function_declarator_node(fn);
   return parameter_list_is_variadic(declarator_parameter_list(declarator));
}

static int fixed_parameter_stack_bytes_from_params(const ASTNode *params) {
   int total = 0;

   if (!params || is_empty(params)) {
      return 0;
   }

   for (int i = 0; i < params->count; i++) {
      const ASTNode *parameter = params->children[i];
      if (!parameter || parameter_is_void(parameter) || parameter_is_ellipsis(parameter) || parameter_has_symbol_storage(parameter)) {
         continue;
      }
      total += parameter_storage_size(parameter);
   }

   return total;
}

static int function_fixed_parameter_stack_bytes(const ASTNode *fn) {
   const ASTNode *declarator = function_declarator_node(fn);
   return fixed_parameter_stack_bytes_from_params(declarator_parameter_list(declarator));
}

static int function_fixed_param_count(const ASTNode *fn) {
   const ASTNode *declarator = function_declarator_node(fn);
   const ASTNode *params = declarator_parameter_list(declarator);
   int count = 0;

   if (params && !is_empty(params)) {
      for (int i = 0; i < params->count; i++) {
         const ASTNode *parameter = params->children[i];
         if (!parameter || parameter_is_void(parameter) || parameter_is_ellipsis(parameter)) {
            continue;
         }
         if (parameter_type(parameter)) {
            count++;
         }
      }
   }

   return count;
}

static bool declarator_array_signature_matches_from(const ASTNode *actual, const ASTNode *formal, int start_child) {
   int ai = start_child;
   int fi = start_child;

   while (1) {
      while (actual && ai < actual->count && (!actual->children[ai] || actual->children[ai]->kind != AST_INTEGER)) {
         ai++;
      }
      while (formal && fi < formal->count && (!formal->children[fi] || formal->children[fi]->kind != AST_INTEGER)) {
         fi++;
      }
      if ((!actual || ai >= actual->count) && (!formal || fi >= formal->count)) {
         return true;
      }
      if (!actual || ai >= actual->count || !formal || fi >= formal->count) {
         return false;
      }
      if (strcmp(actual->children[ai]->strval, formal->children[fi]->strval)) {
         return false;
      }
      ai++;
      fi++;
   }
}

static bool declarator_signature_matches(const ASTNode *actual, const ASTNode *formal) {
   if (declarator_pointer_depth(actual) != declarator_pointer_depth(formal)) {
      return false;
   }
   if (declarator_array_count(actual) != declarator_array_count(formal)) {
      return false;
   }
   return declarator_array_signature_matches_from(actual, formal, 2);
}

static bool declarator_is_plain_value(const ASTNode *declarator) {
   return declarator_pointer_depth(declarator) == 0 && declarator_array_count(declarator) == 0;
}

static bool integer_type_can_represent_type(const ASTNode *formal_type, const ASTNode *actual_type) {
   int formal_size;
   int actual_size;
   bool formal_signed;
   bool actual_signed;

   if (!type_is_promotable_integer(formal_type) || !type_is_promotable_integer(actual_type)) {
      return false;
   }

   formal_size = type_size_from_node(formal_type);
   actual_size = type_size_from_node(actual_type);
   formal_signed = type_is_signed_integer(formal_type);
   actual_signed = type_is_signed_integer(actual_type);

   if (formal_size <= 0 || actual_size <= 0) {
      return false;
   }

   if (formal_signed == actual_signed) {
      return formal_size >= actual_size;
   }

   if (formal_signed && !actual_signed) {
      return formal_size >= actual_size + 1;
   }

   return false;
}

static int integer_promotion_conversion_cost(const ASTNode *actual_type, const ASTNode *actual_decl,
                                             const ASTNode *formal_type, const ASTNode *formal_decl) {
   int cost = 0;
   int formal_size;
   int actual_size;
   const char *actual_endian;
   const char *formal_endian;

   if (!actual_type || !formal_type) {
      return -1;
   }
   if (actual_decl) {
      if (!declarator_signature_matches(actual_decl, formal_decl)) {
         return -1;
      }
      if (!declarator_is_plain_value(actual_decl)) {
         return -1;
      }
   }
   else if (!declarator_is_plain_value(formal_decl)) {
      return -1;
   }
   if (!declarator_is_plain_value(formal_decl)) {
      return -1;
   }
   if (!type_is_promotable_integer(actual_type) || !type_is_promotable_integer(formal_type)) {
      return -1;
   }
   if (!integer_type_can_represent_type(formal_type, actual_type)) {
      return -1;
   }

   formal_size = type_size_from_node(formal_type);
   actual_size = type_size_from_node(actual_type);
   if (formal_size < actual_size) {
      return -1;
   }

   cost += (formal_size - actual_size) * 16;
   if (type_is_signed_integer(formal_type) != type_is_signed_integer(actual_type)) {
      cost += 4;
   }

   actual_endian = type_endian_name(actual_type);
   formal_endian = type_endian_name(formal_type);
   if (formal_size > 1 && actual_endian && formal_endian && strcmp(actual_endian, formal_endian)) {
      cost += 1;
   }

   return cost;
}

static const ASTNode *make_synthetic_pointer_declarator(int ptr_depth) {
   ASTNode *decl;
   char depth_buf[32];

   if (ptr_depth < 0) {
      return NULL;
   }

   decl = make_node("declarator", NULL);
   if (!decl) {
      return NULL;
   }

   snprintf(depth_buf, sizeof(depth_buf), "%d", ptr_depth);
   decl = append_child(decl, make_integer_leaf(strdup(depth_buf)));
   decl->children[0]->name = "pointer";
   decl = append_child(decl, make_empty_leaf());
   return decl;
}

static const ASTNode *decayed_array_declarator(const ASTNode *declarator) {
   const ASTNode *value_decl;
   int start;

   if (!declarator || declarator_pointer_depth(declarator) > 0 || declarator_array_count(declarator) <= 0) {
      return declarator;
   }

   value_decl = declarator_value_declarator(declarator);
   start = declarator_suffix_start_index(value_decl ? value_decl : declarator);
   return clone_declarator_variant(value_decl ? value_decl : declarator, 1, start + 1);
}

static const ASTNode *call_adjusted_parameter_declarator(const ASTNode *declarator, bool is_ref) {
   if (!is_ref && declarator && declarator_pointer_depth(declarator) == 0 && declarator_array_count(declarator) > 0) {
      return decayed_array_declarator(declarator);
   }
   return declarator;
}

static void expr_match_signature(ASTNode *expr, Context *ctx, const ASTNode **type_out, const ASTNode **decl_out) {
   const ASTNode *type;
   const ASTNode *decl;

   expr = (ASTNode *) unwrap_expr_node(expr);
   if (!expr || is_empty(expr)) {
      if (type_out) *type_out = NULL;
      if (decl_out) *decl_out = NULL;
      return;
   }

   type = expr_value_type(expr, ctx);
   decl = expr_value_declarator(expr, ctx);

   if (expr->kind == AST_STRING && !string_literal_is_char_constant(expr->strval)) {
      type = required_typename_node("char");
      decl = make_synthetic_pointer_declarator(1);
   }
   else if (decl && declarator_pointer_depth(decl) == 0 && declarator_array_count(decl) > 0) {
      decl = decayed_array_declarator(decl);
   }
   else if (expr->count == 2 && (!strcmp(expr->name, "+") || !strcmp(expr->name, "-"))) {
      const ASTNode *lhs_type = NULL;
      const ASTNode *lhs_decl = NULL;
      const ASTNode *rhs_type = NULL;
      const ASTNode *rhs_decl = NULL;

      expr_match_signature(expr->children[0], ctx, &lhs_type, &lhs_decl);
      expr_match_signature(expr->children[1], ctx, &rhs_type, &rhs_decl);

      if (lhs_decl && declarator_pointer_depth(lhs_decl) > 0 && !(rhs_decl && declarator_pointer_depth(rhs_decl) > 0)) {
         type = lhs_type;
         decl = lhs_decl;
      }
      else if (!strcmp(expr->name, "+") && rhs_decl && declarator_pointer_depth(rhs_decl) > 0 && !(lhs_decl && declarator_pointer_depth(lhs_decl) > 0)) {
         type = rhs_type;
         decl = rhs_decl;
      }
   }

   if (type_out) *type_out = type;
   if (decl_out) *decl_out = decl;
}

static int parameter_argument_conversion_cost(const ASTNode *ptype, const ASTNode *pdecl, bool pref,
                                              const ASTNode *atype, const ASTNode *adecl, bool arg_lvalue) {
   const char *pname;
   const char *aname;
   bool decl_match = false;
   int promo_cost;

   pdecl = call_adjusted_parameter_declarator(pdecl, pref);

   if (!ptype || !atype) {
      return -1;
   }

   pname = type_name_from_node(ptype);
   aname = type_name_from_node(atype);
   if (!pname || !aname) {
      return -1;
   }

   if (adecl) {
      decl_match = declarator_signature_matches(adecl, pdecl);
   }
   else if (declarator_is_plain_value(pdecl)) {
      decl_match = true;
   }

   if (!strcmp(pname, aname) && decl_match) {
      if (pref) {
         return arg_lvalue ? 0 : -1;
      }
      return 1;
   }

   if (pref) {
      return -1;
   }

   promo_cost = integer_promotion_conversion_cost(atype, adecl, ptype, pdecl);
   if (promo_cost >= 0) {
      return 32 + promo_cost;
   }

   if (!pref && type_is_promotable_integer(atype) && type_is_promotable_integer(ptype) &&
       type_size_from_node(atype) == type_size_from_node(ptype) && declarator_is_plain_value(pdecl) &&
       (!adecl || declarator_is_plain_value(adecl))) {
      int cost = 96;
      if (type_is_signed_integer(atype) != type_is_signed_integer(ptype)) {
         cost += 4;
      }
      {
         const char *actual_endian = type_endian_name(atype);
         const char *formal_endian = type_endian_name(ptype);
         if (actual_endian && formal_endian && strcmp(actual_endian, formal_endian)) {
            cost += 1;
         }
      }
      return cost;
   }

   return -1;
}

static bool function_same_declaration(const ASTNode *a, const ASTNode *b) {
   const ASTNode *atype;
   const ASTNode *btype;
   const ASTNode *adecl;
   const ASTNode *bdecl;
   const char *aname;
   const char *bname;

   if (!a || !b) {
      return false;
   }

   atype = function_return_type(a);
   btype = function_return_type(b);
   aname = type_name_from_node(atype);
   bname = type_name_from_node(btype);
   if ((!aname || !bname) && aname != bname) {
      return false;
   }
   if (aname && bname && strcmp(aname, bname)) {
      return false;
   }

   adecl = function_declarator_node(a);
   bdecl = function_declarator_node(b);
   if (declarator_pointer_depth(adecl) != declarator_pointer_depth(bdecl)) {
      return false;
   }
   if (!declarator_array_signature_matches_from(adecl, bdecl, 3)) {
      return false;
   }
   if (has_modifier((ASTNode *) function_modifiers_node(a), "static") !=
       has_modifier((ASTNode *) function_modifiers_node(b), "static")) {
      return false;
   }

   return function_same_signature(a, b);
}

static int function_signature_match_cost(const ASTNode *fn, int arg_count, const ASTNode **arg_types, const ASTNode **arg_decls, const bool *arg_lvalues) {
   const ASTNode *declarator = function_declarator_node(fn);
   const ASTNode *params = declarator_parameter_list(declarator);
   int seen = 0;
   int cost = 0;
   bool variadic = parameter_list_is_variadic(params);

   if (!declarator) {
      return -1;
   }

   if ((!variadic && function_fixed_param_count(fn) != arg_count) ||
       (variadic && arg_count < function_fixed_param_count(fn))) {
      return -1;
   }

   if (params && !is_empty(params)) {
      for (int i = 0; i < params->count; i++) {
         const ASTNode *parameter = params->children[i];
         const ASTNode *ptype;
         const ASTNode *pdecl;
         bool pref;
         int param_cost;

         if (!parameter || parameter_is_void(parameter) || parameter_is_ellipsis(parameter)) {
            continue;
         }

         ptype = parameter_type(parameter);
         pdecl = parameter_declarator(parameter);
         pref = parameter_is_ref(parameter);
         if (!ptype || seen >= arg_count || !arg_types[seen]) {
            return -1;
         }

         param_cost = parameter_argument_conversion_cost(
               ptype, pdecl, pref,
               arg_types[seen], arg_decls[seen],
               arg_lvalues ? arg_lvalues[seen] : false);
         if (param_cost < 0) {
            return -1;
         }
         cost += param_cost;
         seen++;
      }
   }

   if (variadic) {
      cost += 1024 + (arg_count - seen);
   }

   return (!variadic && seen == arg_count) || (variadic && seen <= arg_count) ? cost : -1;
}


static bool function_same_signature(const ASTNode *a, const ASTNode *b) {
   if (!a || !b) {
      return false;
   }
   if (function_is_variadic(a) != function_is_variadic(b)) {
      return false;
   }
   if (function_fixed_param_count(a) != function_fixed_param_count(b)) {
      return false;
   }

   {
      const ASTNode *adecl = function_declarator_node(a);
      const ASTNode *bdecl = function_declarator_node(b);
      const ASTNode *aparams = declarator_parameter_list(adecl);
      const ASTNode *bparams = declarator_parameter_list(bdecl);
      int ai = 0;
      int bi = 0;

      while ((aparams && !is_empty(aparams) && ai < aparams->count) ||
             (bparams && !is_empty(bparams) && bi < bparams->count)) {
         const ASTNode *aparam = NULL;
         const ASTNode *bparam = NULL;
         while (aparams && !is_empty(aparams) && ai < aparams->count) {
            aparam = aparams->children[ai++];
            if (aparam && !parameter_is_void(aparam) && !parameter_is_ellipsis(aparam) && parameter_type(aparam)) {
               break;
            }
            aparam = NULL;
         }
         while (bparams && !is_empty(bparams) && bi < bparams->count) {
            bparam = bparams->children[bi++];
            if (bparam && !parameter_is_void(bparam) && !parameter_is_ellipsis(bparam) && parameter_type(bparam)) {
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
      if (value == node) {
         return;
      }
      if (!function_same_signature(value, node)) {
         continue;
      }
      if (!function_same_declaration(value, node)) {
         error_user("[%s:%d.%d] vs [%s:%d.%d] conflicting declarations for overloaded '%s'",
               node->file, node->line, node->column,
               value->file, value->line, value->column,
               name);
      }
      if (function_has_body(value) && function_has_body(node)) {
         error_user("[%s:%d.%d] vs [%s:%d.%d] multiple definitions for '%s'",
               node->file, node->line, node->column,
               value->file, value->line, value->column,
               name);
      }
      if (!function_has_body(value) && function_has_body(node)) {
         operator_overloads[i].node = node;
      }
      return;
   }

   operator_overloads = (OperatorOverload *) realloc(operator_overloads,
         sizeof(*operator_overloads) * (operator_overload_count + 1));
   if (!operator_overloads) {
      error_unreachable("out of memory");
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

static void append_callable_signature_mangle(char *buf, size_t bufsize, const ASTNode *declarator) {
   const ASTNode *params = declarator_parameter_list(declarator);
   bool saw_param = false;

   if (params && !is_empty(params)) {
      for (int i = 0; i < params->count; i++) {
         const ASTNode *parameter = params->children[i];
         const ASTNode *ptype;
         const ASTNode *pdecl;
         char tmp[64];
         if (!parameter || parameter_is_void(parameter) || parameter_is_ellipsis(parameter)) {
            continue;
         }
         saw_param = true;
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
      if (parameter_list_is_variadic(params)) {
         saw_param = true;
         strncat(buf, "__var", bufsize - strlen(buf) - 1);
      }
   }
   if (!saw_param) {
      strncat(buf, "__void", bufsize - strlen(buf) - 1);
   }
}



static bool is_named_weak_builtin_operator_type(const ASTNode *type) {
   const char *name;
   int size;
   if (!type) {
      return false;
   }
   name = type_name_from_node(type);
   if (!name || !*name) {
      return false;
   }
   size = type_size_from_node(type);
   if (size <= 0) {
      return false;
   }
   if (!strcmp(name, "char") || !strcmp(name, "bool") || !strcmp(name, "int") ||
       !strcmp(name, "long") || !strcmp(name, "longlong") ||
       !strcmp(name, "half") || !strcmp(name, "float") || !strcmp(name, "double")) {
      return !has_flag(name, "$endian:big");
   }
   if ((name[0] == 's' || name[0] == 'u') && name[1] >= '1' && name[1] <= '9') {
      char *end = NULL;
      long n = strtol(name + 1, &end, 10);
      if (end && !*end && n >= 1 && n <= 16 && size == (int) n && !has_flag(name, "$endian:big")) {
         return true;
      }
   }
   return false;
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
   const ASTNode *lhs_type;
   const ASTNode *rhs_type;
   const ASTNode *lhs_decl = NULL;
   const ASTNode *rhs_decl = NULL;
   const ASTNode *work_type;
   const ASTNode *bool_type;
   int ret_size;
   expr = (ASTNode *) unwrap_expr_node(expr);
   if (!expr || is_empty(expr)) {
      return false;
   }

   if (expr->count == 1 && !strcmp(expr->name, "-")) {
      work_type = expr_value_type(expr->children[0], ctx);
      if (!work_type || !is_named_weak_builtin_operator_type(work_type) || expr_value_declarator(expr->children[0], ctx)) {
         return false;
      }
      if (opname_out) *opname_out = "operator-";
      if (ret_type_out) *ret_type_out = work_type;
      if (ret_decl_out) *ret_decl_out = NULL;
      ret_size = type_size_from_node(work_type);
      if (ret_size_out) *ret_size_out = ret_size;
      if (arg_count_out) *arg_count_out = 1;
      if (arg_exprs_out) arg_exprs_out[0] = expr->children[0];
      if (arg_types_out) arg_types_out[0] = work_type;
      if (arg_decls_out) arg_decls_out[0] = NULL;
      return ret_size > 0;
   }
   if (expr->count == 1 && !strcmp(expr->name, "~")) {
      work_type = expr_value_type(expr->children[0], ctx);
      if (!work_type || type_is_float_like(work_type) || !is_named_weak_builtin_operator_type(work_type) || expr_value_declarator(expr->children[0], ctx)) {
         return false;
      }
      if (opname_out) *opname_out = "operator~";
      if (ret_type_out) *ret_type_out = work_type;
      if (ret_decl_out) *ret_decl_out = NULL;
      ret_size = type_size_from_node(work_type);
      if (ret_size_out) *ret_size_out = ret_size;
      if (arg_count_out) *arg_count_out = 1;
      if (arg_exprs_out) arg_exprs_out[0] = expr->children[0];
      if (arg_types_out) arg_types_out[0] = work_type;
      if (arg_decls_out) arg_decls_out[0] = NULL;
      return ret_size > 0;
   }

   if (expr->count != 2) {
      return false;
   }

   expr_match_signature(expr->children[0], ctx, &lhs_type, &lhs_decl);
   expr_match_signature(expr->children[1], ctx, &rhs_type, &rhs_decl);

   if (!strcmp(expr->name, "+") || !strcmp(expr->name, "-") ||
       !strcmp(expr->name, "*") || !strcmp(expr->name, "/") ||
       !strcmp(expr->name, "%") || !strcmp(expr->name, "&") ||
       !strcmp(expr->name, "|") || !strcmp(expr->name, "^") ||
       !strcmp(expr->name, "<<") || !strcmp(expr->name, ">>") ||
       !strcmp(expr->name, "==") || !strcmp(expr->name, "!=") ||
       !strcmp(expr->name, "<") || !strcmp(expr->name, ">") ||
       !strcmp(expr->name, "<=") || !strcmp(expr->name, ">=")) {
      if ((lhs_type && type_is_float_like(lhs_type)) || (rhs_type && type_is_float_like(rhs_type))) {
         int lhs_size = lhs_type ? type_size_from_node(lhs_type) : 0;
         int rhs_size = rhs_type ? type_size_from_node(rhs_type) : 0;
         if (!strcmp(expr->name, "%") || !strcmp(expr->name, "&") || !strcmp(expr->name, "|") ||
             !strcmp(expr->name, "^") || !strcmp(expr->name, "<<") || !strcmp(expr->name, ">>")) {
            return false;
         }
         work_type = (lhs_type && type_is_float_like(lhs_type) && (!rhs_type || !type_is_float_like(rhs_type) || lhs_size >= rhs_size)) ? lhs_type : rhs_type;
      }
      else {
         if (!strcmp(expr->name, "+") || !strcmp(expr->name, "-")) {
            if ((lhs_decl && declarator_pointer_depth(lhs_decl) > 0) || (rhs_decl && declarator_pointer_depth(rhs_decl) > 0)) {
               return false;
            }
         }
         if (!strcmp(expr->name, "==") || !strcmp(expr->name, "!=") ||
             !strcmp(expr->name, "<") || !strcmp(expr->name, ">") ||
             !strcmp(expr->name, "<=") || !strcmp(expr->name, ">=")) {
            work_type = promoted_integer_type_for_binary(lhs_type, rhs_type, expr);
         }
         else {
            work_type = expr_value_type(expr, ctx);
         }
         if (!work_type) {
            work_type = promoted_integer_type_for_binary(lhs_type, rhs_type, expr);
         }
      }
      if (!work_type || !is_named_weak_builtin_operator_type(work_type) || expr_value_declarator(expr, ctx)) {
         return false;
      }
      if (opname_out) {
         static char opname_buf[32];
         snprintf(opname_buf, sizeof(opname_buf), "operator%s", expr->name);
         *opname_out = opname_buf;
      }
      if (!strcmp(expr->name, "==") || !strcmp(expr->name, "!=") ||
          !strcmp(expr->name, "<") || !strcmp(expr->name, ">") ||
          !strcmp(expr->name, "<=") || !strcmp(expr->name, ">=")) {
         bool_type = required_typename_node("bool");
         if (!bool_type) {
            return false;
         }
         if (ret_type_out) *ret_type_out = bool_type;
         if (ret_decl_out) *ret_decl_out = NULL;
         if (ret_size_out) *ret_size_out = type_size_from_node(bool_type);
      }
      else {
         ret_size = type_size_from_node(work_type);
         if (ret_type_out) *ret_type_out = work_type;
         if (ret_decl_out) *ret_decl_out = NULL;
         if (ret_size_out) *ret_size_out = ret_size;
      }
      if (arg_count_out) *arg_count_out = 2;
      if (arg_exprs_out) {
         arg_exprs_out[0] = expr->children[0];
         arg_exprs_out[1] = expr->children[1];
      }
      if (arg_types_out) {
         arg_types_out[0] = work_type;
         arg_types_out[1] = work_type;
      }
      if (arg_decls_out) {
         arg_decls_out[0] = NULL;
         arg_decls_out[1] = NULL;
      }
      return true;
   }

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

static bool assembler_user_symbol_needs_escape(const char *name) {
   static const char *const reserved[] = {
      "a", "x", "y",
      "adc", "and", "asl", "bcc", "bcs", "beq", "bit", "bmi", "bne", "bpl", "brk", "bvc", "bvs",
      "clc", "cld", "cli", "clv", "cmp", "cpx", "cpy", "dec", "dex", "dey", "eor", "inc", "inx", "iny",
      "jmp", "jsr", "lda", "ldx", "ldy", "lsr", "nop", "ora", "pha", "php", "pla", "plp", "rol", "ror",
      "rti", "rts", "sbc", "sec", "sed", "sei", "sta", "stx", "sty", "tax", "tay", "tsx", "txa", "txs", "tya",
      "sp", "fp", "arg0", "arg1", "ptr0", "ptr1", "ptr2", "ptr3", "sbrk", "tmp0", "tmp1", "tmp2", "tmp3", "tmp4", "tmp5"
   };
   char lower[256];
   size_t n;
   if (!name || !*name) return false;
   if (strchr(name, '$') || strchr(name, '?')) return false;
   n = strlen(name);
   if (n >= sizeof(lower)) return false;
   for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)name[i]);
   lower[n] = 0;
   for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++) {
      if (!strcmp(lower, reserved[i])) return true;
   }
   return false;
}

static bool format_user_asm_symbol(const char *name, char *buf, size_t bufsize) {
   if (!name || !buf || bufsize == 0) return false;
   if (assembler_user_symbol_needs_escape(name)) {
      snprintf(buf, bufsize, "%s?", name);
   }
   else {
      snprintf(buf, bufsize, "%s", name);
   }
   return true;
}

static bool modifier_list_node_like(const ASTNode *node) {
   if (!node || is_empty(node)) {
      return false;
   }
   for (int i = 0; i < node->count; i++) {
      if (!node->children[i] || !node->children[i]->strval) {
         return false;
      }
   }
   return true;
}

static ASTNode *function_modifier_node(const ASTNode *fn) {
   ASTNode *mods;

   if (!fn || fn->count <= 0 || !fn->children[0]) {
      return NULL;
   }

   mods = fn->children[0];
   if (modifier_list_node_like(mods)) {
      return mods;
   }
   if (mods->count > 0 && mods->children[0] && modifier_list_node_like(mods->children[0])) {
      return mods->children[0];
   }
   return NULL;
}

static bool function_has_extern_nonstatic_storage(const ASTNode *fn) {
   ASTNode *mods = function_modifier_node(fn);
   return mods && has_modifier(mods, "extern") && !has_modifier(mods, "static");
}

static bool function_symbol_name(const ASTNode *fn, const char *fallback_name, char *buf, size_t bufsize) {
   const ASTNode *declarator = function_declarator_node(fn);
   const char *name = fallback_name;

   if (!buf || bufsize == 0) {
      return false;
   }
   buf[0] = 0;

   if (!name && declarator) {
      name = declarator_name(declarator);
   }
   if (!name) {
      return false;
   }

   if (fn && function_has_extern_nonstatic_storage(fn) && !strcmp(name, "sbrk")) {
      snprintf(buf, bufsize, "_sbrk");
      return true;
   }

   if (!is_operator_function_name(name) && !ordinary_function_name_is_overloaded(name)) {
      return format_user_asm_symbol(name, buf, bufsize);
   }

   append_mangled_text(buf, bufsize, name);
   if (!fn) {
      return true;
   }

   append_callable_signature_mangle(buf, bufsize, declarator);
   {
      char raw[256];
      snprintf(raw, sizeof(raw), "%s", buf);
      return format_user_asm_symbol(raw, buf, bufsize);
   }
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
   int best_cost = INT_MAX;
   bool ambiguous = false;

   for (int i = 0; i < operator_overload_count; i++) {
      int cost;
      if (strcmp(operator_overloads[i].name, name)) {
         continue;
      }
      cost = function_signature_match_cost(operator_overloads[i].node, arg_count, arg_types, arg_decls, arg_lvalues);
      if (cost < 0) {
         continue;
      }
      if (!best || cost < best_cost) {
         best = operator_overloads[i].node;
         best_cost = cost;
         ambiguous = false;
      }
      else if (cost == best_cost) {
         ambiguous = true;
      }
   }
   if (ambiguous && best) {
      error_user("ambiguous overloaded operator '%s'", name);
   }
   return best;
}

static bool ordinary_function_name_is_overloaded(const char *name) {
   int count = 0;

   if (!name) {
      return false;
   }

   for (int i = 0; i < ordinary_function_count; i++) {
      if (strcmp(ordinary_functions[i].name, name)) {
         continue;
      }
      count++;
      if (count > 1) {
         return true;
      }
   }

   return false;
}

static bool parameter_lists_same_signature(const ASTNode *lhs_params, const ASTNode *rhs_params) {
   int li = 0;
   int ri = 0;

   if (parameter_list_is_variadic(lhs_params) != parameter_list_is_variadic(rhs_params)) {
      return false;
   }

   while ((lhs_params && !is_empty(lhs_params) && li < lhs_params->count) ||
          (rhs_params && !is_empty(rhs_params) && ri < rhs_params->count)) {
      const ASTNode *lparam = NULL;
      const ASTNode *rparam = NULL;
      const char *lname;
      const char *rname;

      while (lhs_params && !is_empty(lhs_params) && li < lhs_params->count) {
         lparam = lhs_params->children[li++];
         if (lparam && !parameter_is_void(lparam) && !parameter_is_ellipsis(lparam) && parameter_type(lparam)) {
            break;
         }
         lparam = NULL;
      }
      while (rhs_params && !is_empty(rhs_params) && ri < rhs_params->count) {
         rparam = rhs_params->children[ri++];
         if (rparam && !parameter_is_void(rparam) && !parameter_is_ellipsis(rparam) && parameter_type(rparam)) {
            break;
         }
         rparam = NULL;
      }

      if (!lparam && !rparam) {
         break;
      }
      if (!lparam || !rparam) {
         return false;
      }
      lname = type_name_from_node(parameter_type(lparam));
      rname = type_name_from_node(parameter_type(rparam));
      if ((!lname || !rname) && lname != rname) {
         return false;
      }
      if (lname && rname && strcmp(lname, rname)) {
         return false;
      }
      if (parameter_is_ref(lparam) != parameter_is_ref(rparam)) {
         return false;
      }
      if (!declarator_signature_matches(parameter_declarator(lparam), parameter_declarator(rparam))) {
         return false;
      }
   }

   return true;
}

static int function_designator_match_cost(const ASTNode *fn, const ASTNode *expected_type, const ASTNode *expected_decl) {
   const ASTNode *fn_decl;
   const ASTNode *fn_ret_type;
   const ASTNode *fn_ret_decl;
   const ASTNode *expected_ret_decl;
   const char *expected_name;
   const char *fn_name;

   if (!fn || !expected_type || !expected_decl || !declarator_has_parameter_list(expected_decl)) {
      return -1;
   }

   fn_decl = function_declarator_node(fn);
   fn_ret_type = function_return_type(fn);
   fn_ret_decl = function_return_declarator_from_callable(fn_decl);
   expected_ret_decl = function_return_declarator_from_callable(expected_decl);
   expected_name = type_name_from_node(expected_type);
   fn_name = type_name_from_node(fn_ret_type);

   if ((!expected_name || !fn_name) && expected_name != fn_name) {
      return -1;
   }
   if (expected_name && fn_name && strcmp(expected_name, fn_name)) {
      return -1;
   }
   if (!declarator_signature_matches(fn_ret_decl, expected_ret_decl)) {
      return -1;
   }
   if (!parameter_lists_same_signature(declarator_parameter_list(fn_decl), declarator_parameter_list(expected_decl))) {
      return -1;
   }

   return 0;
}

static const ASTNode *lookup_ordinary_function_overload(const char *name, int arg_count, const ASTNode **arg_types, const ASTNode **arg_decls, const bool *arg_lvalues) {
   const ASTNode *best = NULL;
   int best_cost = INT_MAX;
   bool ambiguous = false;
   bool saw_name = false;

   for (int i = 0; i < ordinary_function_count; i++) {
      int cost;

      if (strcmp(ordinary_functions[i].name, name)) {
         continue;
      }
      saw_name = true;
      cost = function_signature_match_cost(ordinary_functions[i].node, arg_count, arg_types, arg_decls, arg_lvalues);
      if (cost < 0) {
         continue;
      }
      if (!best || cost < best_cost) {
         best = ordinary_functions[i].node;
         best_cost = cost;
         ambiguous = false;
      }
      else if (cost == best_cost) {
         ambiguous = true;
      }
   }

   if (ambiguous && best) {
      error_user("ambiguous call to overloaded function '%s'", name);
   }
   if (!best && saw_name) {
      error_user("no viable overload for function '%s'", name);
   }

   return best;
}

static const ASTNode *resolve_function_designator_target(const char *name, const ASTNode *expected_type, const ASTNode *expected_decl) {
   const ASTNode *best = NULL;
   const ASTNode *first = NULL;
   int matches = 0;
   int total = 0;

   if (!name) {
      return NULL;
   }

   for (int i = 0; i < ordinary_function_count; i++) {
      if (strcmp(ordinary_functions[i].name, name)) {
         continue;
      }
      if (!first) {
         first = ordinary_functions[i].node;
      }
      total++;
      if (!expected_type || !expected_decl) {
         continue;
      }
      if (function_designator_match_cost(ordinary_functions[i].node, expected_type, expected_decl) >= 0) {
         best = ordinary_functions[i].node;
         matches++;
      }
   }

   if (total == 0) {
      return NULL;
   }

   if (!expected_type || !expected_decl) {
      if (total == 1) {
         return first;
      }
      return NULL;
   }

   if (matches > 1) {
      error_user("ambiguous reference to overloaded function '%s'", name);
   }
   if (matches == 1) {
      return best;
   }
   if (total == 1) {
      return NULL;
   }

   error_user("no overload of function '%s' matches the target function pointer type", name);
   return NULL;
}

static const ASTNode *resolve_function_call_target(const char *name, ASTNode *args, Context *ctx) {
   int arg_count = (args && !is_empty(args)) ? args->count : 0;
   const ASTNode **arg_types = NULL;
   const ASTNode **arg_decls = NULL;
   bool *arg_lvalues = NULL;
   const ASTNode *ret = NULL;

   if (arg_count > 0) {
      arg_types = calloc((size_t) arg_count, sizeof(*arg_types));
      arg_decls = calloc((size_t) arg_count, sizeof(*arg_decls));
      arg_lvalues = calloc((size_t) arg_count, sizeof(*arg_lvalues));
      if (!arg_types || !arg_decls || !arg_lvalues) {
         free((void *) arg_types);
         free((void *) arg_decls);
         free(arg_lvalues);
         return NULL;
      }
   }

   for (int i = 0; i < arg_count; i++) {
      expr_match_signature(args->children[i], ctx, &arg_types[i], &arg_decls[i]);
      arg_lvalues[i] = resolve_ref_argument_lvalue(ctx, args->children[i], NULL);
   }

   if (is_operator_function_name(name)) {
      ret = lookup_operator_overload(name, arg_count, arg_types, arg_decls, arg_lvalues);
   }
   else {
      ret = lookup_ordinary_function_overload(name, arg_count, arg_types, arg_decls, arg_lvalues);
   }

   free((void *) arg_types);
   free((void *) arg_decls);
   free(arg_lvalues);
   return ret;
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
      expr_match_signature(expr->children[i], ctx, &arg_types[i], &arg_decls[i]);
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

   expr_match_signature(expr, ctx, &arg_type, &arg_decl);
   if (!arg_type) {
      return NULL;
   }
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
   expr_match_signature(expr, ctx, &arg_type, &arg_decl);
   if (!arg_type) {
      return NULL;
   }
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

static bool address_spec_has_read(const ASTNode *node) {
   return address_spec_read_expr(node) != NULL;
}

static bool address_spec_has_write(const ASTNode *node) {
   return address_spec_write_expr(node) != NULL;
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

static void warn_address_spec_without_ref(const ASTNode *node, const char *name) {
   if (!node) {
      return;
   }
   warning("[%s:%d.%d] '@' on non-ref declaration '%s' is ignored",
         node->file, node->line, node->column, name ? name : "?");
}

static bool init_context_entry_from_global_decl(ContextEntry *entry, const char *name, const ASTNode *g) {
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

static bool entry_symbol_name(Context *ctx, const ContextEntry *entry, char *buf, size_t bufsize) {
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

static void emit_copy_fp_to_symbol(const char *symbol, int src_offset, int size) {
   emit_copy_fp_to_symbol_offset(symbol, 0, src_offset, size);
}

static void emit_load_a_from_expr_address(const char *expr, int addend) {
   char expr_buf[256];
   const char *asm_expr = assembler_address_expr(expr, expr_buf, sizeof(expr_buf));

   if (addend == 0) {
      emit(&es_code, "    lda  %s\n", asm_expr);
   }
   else {
      emit(&es_code, "    lda  %s + %d\n", asm_expr, addend);
   }
}

static void emit_store_a_to_expr_address(const char *expr, int addend) {
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

static bool pointer_initializer_uses_backing_object(const ASTNode *type, const ASTNode *declarator, const ASTNode *expr) {
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

static const char *emit_pointer_initializer_backing_object(const ASTNode *type, const ASTNode *declarator, const ASTNode *expr) {
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
      const char *label = emit_data_string_object(text);
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

static void emit_runtime_fill_ptr1(int count, unsigned char value) {
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

static const char *runtime_copy_convert_helper_name(int dst_size, const ASTNode *dst_type, int src_size, const ASTNode *src_type) {
   bool src_big_endian = type_is_big_endian(src_type);
   bool dst_big_endian = type_is_big_endian(dst_type);
   bool is_signed = src_type && has_flag(type_name_from_node(src_type), "$signed");

   if (dst_size <= 0 || src_size <= 0 || dst_size == src_size || src_big_endian != dst_big_endian || src_big_endian) {
      return NULL;
   }
   return is_signed ? "copysxN" : "copyzxN";
}

static void emit_runtime_copy_ptr0_to_ptr1(const char *helper, int src_size, int dst_size) {
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

static void emit_fill_fp_bytes(int dst_offset, int start, int count, unsigned char value) {
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

static void emit_copy_fp_to_fp_convert(int dst_offset, int dst_size, const ASTNode *dst_type, int src_offset, int src_size, const ASTNode *src_type) {
   bool src_big_endian = type_is_big_endian(src_type);
   bool dst_big_endian = type_is_big_endian(dst_type);
   bool is_signed = src_type && has_flag(type_name_from_node(src_type), "$signed");
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
   bool is_signed = src_type && has_flag(type_name_from_node(src_type), "$signed");
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
      error_unreachable("[%s:%d] internal missing required type name", __FILE__, __LINE__);
   }

   node = get_typename_node(name);
   if (!node) {
      error_user("type %s is not defined", name);
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
   const ASTNode *node;
   if (!name || !strcmp(name, "*") || type_is_bool(type) || type_is_float_like(type)) {
      return false;
   }
   if (has_flag(name, "$unsigned")) {
      return false;
   }
   if (has_flag(name, "$signed")) {
      return true;
   }
   node = get_typename_node(name);
   if (node && (!strcmp(node->name, "struct_decl_stmt") || !strcmp(node->name, "union_decl_stmt"))) {
      return false;
   }
   return type_size_from_node(type) > 0;
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

static bool type_is_big_endian(const ASTNode *type) {
   return type && has_flag(type_name_from_node(type), "$endian:big");
}

static int endian_mem_index_for_significance(int size, bool big_endian, int significance_index) {
   if (significance_index < 0) {
      return 0;
   }
   if (significance_index >= size) {
      significance_index = size - 1;
   }
   return big_endian ? (size - 1 - significance_index) : significance_index;
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

   {
      const char *lhs_name = type_name_from_node(lhs_type);
      const char *rhs_name = type_name_from_node(rhs_type);
      if (lhs_name && rhs_name && !strcmp(lhs_name, rhs_name)) {
         return lhs_type;
      }
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

   {
      const char *lhs_endian = type_endian_name(lhs_type);
      const char *rhs_endian = type_endian_name(rhs_type);
      if (lhs_endian && rhs_endian && strcmp(lhs_endian, rhs_endian)) {
         preferred_endian = "little";
      }
      else if (lhs_size >= rhs_size) {
         preferred_endian = lhs_endian;
      }
      else {
         preferred_endian = rhs_endian;
      }
      if (!preferred_endian) {
         preferred_endian = lhs_endian;
      }
      if (!preferred_endian) {
         preferred_endian = rhs_endian;
      }
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
static const char *enum_backing_type_name(const char *type) {
   if (!type || !enumbackings || !pair_exists(enumbackings, type)) {
      return NULL;
   }
   return pair_get(enumbackings, type);
}

static bool has_flag(const char *type, const char *flag) {
   const ASTNode *node;
   const char *backing;

   if (!type || !flag) {
      return false;
   }

   backing = enum_backing_type_name(type);
   if (backing) {
      return has_flag(backing, flag);
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

static bool has_flag_prefix(const char *type, const char *prefix) {
   const ASTNode *node;
   const char *backing;
   size_t prefix_len;

   if (!type || !prefix) {
      return false;
   }

   backing = enum_backing_type_name(type);
   if (backing) {
      return has_flag_prefix(backing, prefix);
   }

   node = get_typename_node(type);
   if (!node || node->count < 2 || is_empty(node->children[1])) {
      return false;
   }

   prefix_len = strlen(prefix);
   const ASTNode *flags = node->children[1];
   for (int i = 0; i < flags->count; i++) {
      const char *text;
      if (!flags->children[i] || !flags->children[i]->strval) {
         continue;
      }
      text = flags->children[i]->strval;
      if (!strncmp(text, prefix, prefix_len)) {
         return true;
      }
   }
   return false;
}

static const char *parse_float_style_flag_text(const char *text) {
   if (!text || strncmp(text, "$float:", 7) || !text[7]) {
      return NULL;
   }
   return text + 7;
}

static bool type_is_float_like(const ASTNode *type) {
   const char *name = type_name_from_node(type);
   return name && has_flag_prefix(name, "$float:");
}

static const char *type_float_style(const ASTNode *type) {
   const ASTNode *node;
   const ASTNode *flags;

   if (!type) {
      return NULL;
   }

   node = get_typename_node(type_name_from_node(type));
   if (!node || node->count < 2 || is_empty(node->children[1])) {
      return NULL;
   }

   flags = node->children[1];
   for (int i = 0; i < flags->count; i++) {
      const char *style;
      if (!flags->children[i] || !flags->children[i]->strval) {
         continue;
      }
      style = parse_float_style_flag_text(flags->children[i]->strval);
      if (style) {
         return style;
      }
   }

   return NULL;
}

static int type_float_expbits(const ASTNode *type) {
   const char *style;
   int size;

   if (!type) {
      return -1;
   }

   style = type_float_style(type);
   if (!style) {
      return -1;
   }

   size = type_size_from_node(type);
   return float_style_expbits_for_size(style, size);
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
         error_user("[%s:%d.%d] multiple mem modifiers '%s' and '%s' are not allowed",
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

static bool modifiers_imply_mem_storage(const ASTNode *modifiers) {
   return find_mem_modifier_name(modifiers) != NULL;
}

static bool modifiers_imply_named_nonzeropage(const ASTNode *modifiers) {
   return modifiers_imply_mem_storage(modifiers) && !modifiers_imply_zeropage(modifiers);
}

static void build_named_storage_segment(char *buf, size_t bufsize, const ASTNode *modifiers, const char *base_segment) {
   const char *memname = find_mem_modifier_name(modifiers);

   if (!buf || bufsize == 0) {
      return;
   }

   if (modifiers_imply_named_nonzeropage(modifiers) && memname && *memname) {
      snprintf(buf, bufsize, "%s.%s", base_segment, memname);
   }
   else {
      snprintf(buf, bufsize, "%s", base_segment);
   }
}

static int get_size(const char *type) {
   const ASTNode *node;
   const char *backing;

   if (!type) {
      error_unreachable("[%s:%d] internal could not find NULL type", __FILE__, __LINE__);
   }

   if (typesizes && pair_exists(typesizes, type)) {
      return (int)(intptr_t) pair_get(typesizes, type);
   }

   backing = enum_backing_type_name(type);
   if (backing) {
      return get_size(backing);
   }

   node = get_typename_node(type);
   if (!node) {
      error_unreachable("[%s:%d] internal could not find '%s'", __FILE__, __LINE__, type);
   }

   if (!strcmp(node->name, "type_decl_stmt")) {
      if (node->count < 2 || is_empty(node->children[1])) {
         error_unreachable("[%s:%d] internal could not find '%s'", __FILE__, __LINE__, type);
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

   error_unreachable("[%s:%d] internal could not find '%s'", __FILE__, __LINE__, type);
   return -1;
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

static void ctx_push(Context *ctx, const ASTNode *type, const char *name) {
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

static void ctx_zeropage(Context *ctx, const ASTNode *type, const char *name) {
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
static const char *missing_argname(int i) {
   static char ret[16];
   sprintf(ret, "$%d", i);
   return ret;
}

static ASTNode *make_named_pointer_declarator(const char *name) {
   ASTNode *ret;

   ret = make_node("declarator", NULL);
   ret = append_child(ret, make_integer_leaf(strdup("1")));
   ret->children[0]->name = "pointer";
   ret = append_child(ret, name ? make_identifier_leaf(strdup(name)) : make_empty_leaf());
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
   return (decl_item && decl_item->count > 0) ? decl_subitem_declarator(decl_item->children[0]) : NULL;
}

static bool parameter_is_ref(const ASTNode *parameter) {
   const ASTNode *decl_specs = parameter_decl_specifiers(parameter);
   const ASTNode *modifiers = (decl_specs && decl_specs->count > 0) ? decl_specs->children[0] : NULL;
   return has_modifier((ASTNode *) modifiers, "ref");
}

static bool parameter_has_symbol_storage(const ASTNode *parameter) {
   const ASTNode *decl_specs = parameter_decl_specifiers(parameter);
   const ASTNode *modifiers = (decl_specs && decl_specs->count > 0) ? decl_specs->children[0] : NULL;
   return has_modifier((ASTNode *) modifiers, "static") || modifiers_imply_mem_storage(modifiers);
}

static int parameter_storage_size(const ASTNode *parameter) {
   const ASTNode *ptype = parameter_type(parameter);
   const ASTNode *pdecl = call_adjusted_parameter_declarator(parameter_declarator(parameter), parameter_is_ref(parameter));
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
   if (!declarator || !declarator_name(declarator)) {
      return missing_argname(i);
   }
   return declarator_name(declarator);
}

static bool parameter_is_void(const ASTNode *parameter) {
   const ASTNode *type = parameter_type(parameter);
   const ASTNode *declarator = parameter_declarator(parameter);

   if (!type || strcmp(type->strval, "void")) {
      return false;
   }

   if (declarator && declarator_name(declarator)) {
      return false;
   }

   if (declarator && !declarator_is_plain_value(declarator)) {
      return false;
   }

   return true;
}

static bool variadic_hidden_name_reserved(const char *name) {
   return name && (!strcmp(name, VARIADIC_HIDDEN_ARGS_NAME) || !strcmp(name, VARIADIC_HIDDEN_BYTES_NAME));
}

static void validate_nonreserved_variadic_name(const char *name, const ASTNode *node) {
   if (!node || !variadic_hidden_name_reserved(name)) {
      return;
   }
   error_user("[%s:%d.%d] '%s' is a reserved implementation name", node->file, node->line, node->column, name);
}

static void validate_function_nonreserved_variadic_names(const ASTNode *fn) {
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

static void build_function_context(const ASTNode *node, Context *ctx) {
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

static void emit_prepare_fp_ptr(int ptrno, int offset) {
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

static void emit_load_address_to_ptr(int ptrno, const char *symbol, int addend) {
   emit(&es_code, "    lda #<(%s + %d)\n", symbol, addend);
   emit(&es_code, "    sta ptr%d\n", ptrno);
   emit(&es_code, "    lda #>(%s + %d)\n", symbol, addend);
   emit(&es_code, "    sta ptr%d+1\n", ptrno);
}

static const char *assembler_address_expr(const char *expr, char *buf, size_t buf_size) {
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

static void emit_load_expr_address_to_ptr(int ptrno, const char *expr, int addend) {
   char expr_buf[256];
   const char *asm_expr = assembler_address_expr(expr, expr_buf, sizeof(expr_buf));

   emit(&es_code, "    lda #<(%s + %d)\n", asm_expr, addend);
   emit(&es_code, "    sta ptr%d\n", ptrno);
   emit(&es_code, "    lda #>(%s + %d)\n", asm_expr, addend);
   emit(&es_code, "    sta ptr%d+1\n", ptrno);
}

static void emit_load_ptr_from_symbol(int ptrno, const char *symbol, int addend) {
   emit(&es_code, "    ldy #0\n");
   emit(&es_code, "    lda %s + %d,y\n", symbol, addend);
   emit(&es_code, "    sta ptr%d\n", ptrno);
   emit(&es_code, "    iny\n");
   emit(&es_code, "    lda %s + %d,y\n", symbol, addend);
   emit(&es_code, "    sta ptr%d+1\n", ptrno);
}

static void emit_deref_ptr(int ptrno) {
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

static void emit_add_fp_to_ptr(int ptrno, int src_offset, int src_size) {
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


static bool compile_constant_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst) {
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
           !strcmp(expr->name, "opt_expr") ||
           !strcmp(expr->name, "case_choice") ||
           !strcmp(expr->name, "case_term"))) {
      expr = expr->children[0];
   }
   return expr;
}

static const ASTNode *cast_expr_target_type(const ASTNode *expr) {
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

static const ASTNode *cast_expr_target_declarator(const ASTNode *expr) {
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


static int declarator_pointer_node_count(const ASTNode *declarator) {
   int count = 0;

   if (!declarator) {
      return 0;
   }

   while (count < declarator->count && declarator->children[count] &&
          !strcmp(declarator->children[count]->name, "pointer")) {
      count++;
   }

   return count;
}

static const ASTNode *declarator_nested(const ASTNode *declarator) {
   int pcount = declarator_pointer_node_count(declarator);

   if (!declarator || pcount >= declarator->count) {
      return NULL;
   }

   if (declarator->children[pcount] && !strcmp(declarator->children[pcount]->name, "declarator")) {
      return declarator->children[pcount];
   }

   return NULL;
}

static const ASTNode *declarator_value_declarator(const ASTNode *declarator) {
   const ASTNode *nested = declarator_nested(declarator);

   if (nested && declarator_parameter_list(declarator)) {
      return nested;
   }

   return declarator;
}

static const ASTNode *declarator_name_node(const ASTNode *declarator) {
   const ASTNode *nested = declarator_nested(declarator);
   int pcount = declarator_pointer_node_count(declarator);
   const ASTNode *fallback = NULL;

   if (!declarator) {
      return NULL;
   }

   if (nested) {
      return declarator_name_node(nested);
   }

   for (int i = pcount; i < declarator->count; i++) {
      const ASTNode *child = declarator->children[i];
      if (!child) {
         continue;
      }
      if (child->kind == AST_IDENTIFIER) {
         return child;
      }
      if (!fallback && child->kind == AST_EMPTY) {
         fallback = child;
      }
   }

   return fallback;
}

static const char *declarator_name(const ASTNode *declarator) {
   const ASTNode *name = declarator_name_node(declarator);

   if (!name || is_empty(name) || !name->strval) {
      return NULL;
   }

   return name->strval;
}

static const ASTNode *declarator_bitfield_node(const ASTNode *declarator) {
   const ASTNode *value_decl = declarator_value_declarator(declarator);
   int start;

   if (!value_decl) {
      return NULL;
   }

   start = declarator_suffix_start_index(value_decl);
   for (int i = start; i < value_decl->count; i++) {
      const ASTNode *child = value_decl->children[i];
      if (child && !strcmp(child->name, "bitfield_width")) {
         return child;
      }
   }

   return NULL;
}

static int declarator_bitfield_width(const ASTNode *declarator) {
   const ASTNode *node = declarator_bitfield_node(declarator);

   if (!node || node->count <= 0 || !node->children[0] || !node->children[0]->strval) {
      return 0;
   }

   return atoi(node->children[0]->strval);
}

static int declarator_suffix_start_index(const ASTNode *declarator) {
   const ASTNode *nested = declarator_nested(declarator);
   const ASTNode *name = declarator_name_node(declarator);

   if (!declarator) {
      return 0;
   }

   if (nested) {
      return declarator->count;
   }

   for (int i = 0; i < declarator->count; i++) {
      if (declarator->children[i] == name) {
         return i + 1;
      }
   }

   return declarator_pointer_node_count(declarator);
}

static const ASTNode *declarator_parameter_list(const ASTNode *declarator) {
   int start = declarator_pointer_node_count(declarator) + 1;

   if (!declarator) {
      return NULL;
   }

   for (int i = start; i < declarator->count; i++) {
      if (declarator->children[i] && !strcmp(declarator->children[i]->name, "parameter_list")) {
         return declarator->children[i];
      }
   }

   return NULL;
}

static bool declarator_has_parameter_list(const ASTNode *declarator) {
   return declarator_parameter_list(declarator) != NULL;
}

static int declarator_pointer_depth(const ASTNode *declarator) {
   const ASTNode *value_decl = declarator_value_declarator(declarator);
   int pcount = declarator_pointer_node_count(value_decl);

   if (!value_decl || pcount == 0) {
      return 0;
   }

   return value_decl->children[0] && value_decl->children[0]->strval ? atoi(value_decl->children[0]->strval) : 0;
}

static int declarator_function_pointer_depth(const ASTNode *declarator) {
   const ASTNode *nested = declarator_nested(declarator);

   if (!declarator_has_parameter_list(declarator) || !nested) {
      return 0;
   }

   return declarator_pointer_depth(nested);
}

static int declarator_array_multiplier_from(const ASTNode *declarator, int start_child) {
   int mult = 1;
   const ASTNode *value_decl = declarator_value_declarator(declarator);

   if (!value_decl || declarator_is_function(declarator)) {
      return 1;
   }

   for (int i = start_child; i < value_decl->count; i++) {
      if (value_decl->children[i] && value_decl->children[i]->kind == AST_INTEGER) {
         mult *= atoi(value_decl->children[i]->strval);
      }
   }

   return mult;
}

static int declarator_array_count(const ASTNode *declarator) {
   int count = 0;
   const ASTNode *value_decl = declarator_value_declarator(declarator);
   int start = declarator_suffix_start_index(value_decl);

   if (!value_decl || declarator_is_function(declarator)) {
      return 0;
   }

   for (int i = start; i < value_decl->count; i++) {
      if (value_decl->children[i] && value_decl->children[i]->kind == AST_INTEGER) {
         count++;
      }
   }

   return count;
}

static int declarator_first_element_size(const ASTNode *type, const ASTNode *declarator) {
   const ASTNode *value_decl = declarator_value_declarator(declarator);

   if (declarator_pointer_depth(declarator) > 0) {
      return get_size(type_name_from_node(type));
   }
   return get_size(type_name_from_node(type)) * declarator_array_multiplier_from(value_decl, declarator_suffix_start_index(value_decl) + 1);
}

static const ASTNode *clone_declarator_variant(const ASTNode *declarator, int new_ptr_depth, int first_array_child) {
   ASTNode *copy;
   char depth_buf[32];
   const ASTNode *name = declarator_name_node(declarator);

   if (!declarator) {
      return NULL;
   }

   snprintf(depth_buf, sizeof(depth_buf), "%d", new_ptr_depth);
   copy = make_node(declarator->name, NULL);
   copy->file = declarator->file;
   copy->line = declarator->line;
   copy->column = declarator->column;
   copy->handled = declarator->handled;
   copy->kind = declarator->kind;

   copy = append_child(copy, make_integer_leaf(strdup(depth_buf)));
   copy->children[0]->name = "pointer";
   if (name) {
      copy = append_child(copy, (ASTNode *) name);
   }
   else {
      copy = append_child(copy, make_empty_leaf());
   }
   for (int i = first_array_child; i < declarator->count; i++) {
      if (declarator->children[i] && strcmp(declarator->children[i]->name, "parameter_list")) {
         copy = append_child(copy, (ASTNode *) declarator->children[i]);
      }
   }
   return copy;
}

static const ASTNode *function_pointer_declarator_from_callable(const ASTNode *declarator) {
   ASTNode *copy;
   ASTNode *nested;
   char depth_buf[32];
   int outer_depth = 0;
   int param_index = -1;

   if (!declarator || !declarator_has_parameter_list(declarator)) {
      return NULL;
   }

   if (declarator_function_pointer_depth(declarator) > 0) {
      return declarator;
   }

   if (declarator->children[0] && declarator->children[0]->strval) {
      outer_depth = atoi(declarator->children[0]->strval);
   }

   copy = make_node(declarator->name, NULL);
   copy->file = declarator->file;
   copy->line = declarator->line;
   copy->column = declarator->column;
   copy->handled = declarator->handled;
   copy->kind = declarator->kind;

   snprintf(depth_buf, sizeof(depth_buf), "%d", outer_depth);
   copy = append_child(copy, make_integer_leaf(strdup(depth_buf)));
   copy->children[0]->name = "pointer";
   nested = make_node("declarator", NULL);
   nested = append_child(nested, make_integer_leaf(strdup("1")));
   nested->children[0]->name = "pointer";
   nested = append_child(nested, make_empty_leaf());
   copy = append_child(copy, nested);
   for (int i = 0; i < declarator->count; i++) {
      if (declarator->children[i] && !strcmp(declarator->children[i]->name, "parameter_list")) {
         param_index = i;
         break;
      }
   }
   for (int i = param_index; param_index >= 0 && i < declarator->count; i++) {
      append_child(copy, (ASTNode *) declarator->children[i]);
   }

   return copy;
}

static const ASTNode *function_return_declarator_from_callable(const ASTNode *declarator) {
   ASTNode *copy;
   char depth_buf[32];
   int outer_depth = 0;
   int param_index = -1;

   if (!declarator || !declarator_has_parameter_list(declarator)) {
      return NULL;
   }

   if (declarator->children[0] && declarator->children[0]->strval) {
      outer_depth = atoi(declarator->children[0]->strval);
   }

   copy = make_node(declarator->name, NULL);
   copy->file = declarator->file;
   copy->line = declarator->line;
   copy->column = declarator->column;
   copy->handled = declarator->handled;
   copy->kind = declarator->kind;

   snprintf(depth_buf, sizeof(depth_buf), "%d", outer_depth);
   copy = append_child(copy, make_integer_leaf(strdup(depth_buf)));
   copy->children[0]->name = "pointer";
   copy = append_child(copy, make_empty_leaf());

   for (int i = 0; i < declarator->count; i++) {
      if (declarator->children[i] && !strcmp(declarator->children[i]->name, "parameter_list")) {
         param_index = i;
         break;
      }
   }
   for (int i = param_index + 1; param_index >= 0 && i < declarator->count; i++) {
      if (declarator->children[i] && declarator->children[i]->kind == AST_INTEGER) {
         copy = append_child(copy, (ASTNode *) declarator->children[i]);
      }
   }

   return copy;
}

static const ASTNode *declarator_after_subscript(const ASTNode *declarator) {
   int ptr_depth = declarator_pointer_depth(declarator);
   const ASTNode *value_decl = declarator_value_declarator(declarator);

   if (!value_decl) {
      return NULL;
   }
   if (ptr_depth > 0) {
      return clone_declarator_variant(value_decl, ptr_depth - 1, declarator_suffix_start_index(value_decl));
   }
   if (declarator_array_count(value_decl) > 0) {
      return clone_declarator_variant(value_decl, ptr_depth, declarator_suffix_start_index(value_decl) + 1);
   }
   return NULL;
}

static const ASTNode *declarator_after_deref(const ASTNode *declarator) {
   int ptr_depth = declarator_pointer_depth(declarator);
   const ASTNode *value_decl = declarator_value_declarator(declarator);

   if (ptr_depth <= 0 || !value_decl) {
      return NULL;
   }
   return clone_declarator_variant(value_decl, ptr_depth - 1, declarator_suffix_start_index(value_decl));
}

static bool find_aggregate_member_info(const ASTNode *type, const char *member, AggregateMemberInfo *out) {
   const ASTNode *agg;
   int bit_cursor = 0;
   bool is_union = false;

   if (!type || !type_name_from_node(type) || !member) {
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
      int bit_width;
      int byte_offset;
      int bit_offset;
      int storage_size;
      if (!field || field->count < 3) {
         continue;
      }
      ftype = field->children[1];
      fdecl = field->children[2];
      fname = declarator_name(fdecl);
      if (!fname) {
         continue;
      }
      fsize = declarator_storage_size(ftype, fdecl);
      bit_width = declarator_bitfield_width(fdecl);
      if (is_union) {
         byte_offset = 0;
         bit_offset = 0;
      }
      else if (bit_width > 0) {
         byte_offset = bit_cursor / 8;
         bit_offset = bit_cursor % 8;
      }
      else {
         if (bit_cursor % 8) {
            bit_cursor = ((bit_cursor + 7) / 8) * 8;
         }
         byte_offset = bit_cursor / 8;
         bit_offset = 0;
      }
      storage_size = bit_width > 0 ? ((bit_offset + bit_width + 7) / 8) : fsize;
      if (!strcmp(fname, member)) {
         if (out) {
            out->type = ftype;
            out->declarator = fdecl;
            out->byte_offset = byte_offset;
            out->bit_offset = bit_offset;
            out->bit_width = bit_width;
            out->storage_size = storage_size;
            out->is_bitfield = bit_width > 0;
         }
         return true;
      }
      if (!is_union) {
         if (bit_width > 0) {
            bit_cursor += bit_width;
         }
         else {
            bit_cursor += fsize * 8;
         }
      }
   }
   return false;
}

static bool find_aggregate_member(const ASTNode *type, const char *member, const ASTNode **member_type, const ASTNode **member_declarator, int *member_offset) {
   AggregateMemberInfo info = {0};
   if (!find_aggregate_member_info(type, member, &info)) {
      return false;
   }
   if (member_type) *member_type = info.type;
   if (member_declarator) *member_declarator = info.declarator;
   if (member_offset) *member_offset = info.byte_offset;
   return true;
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
         if (init_context_entry_from_global_decl(&gtmp, expr->strval, g)) {
            entry = &gtmp;
         }
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
      out->base_type = entry->type;
      out->base_declarator = entry->declarator;
      out->suffixes = NULL;
      out->is_static = entry->is_static;
      out->is_zeropage = entry->is_zeropage;
      out->is_global = entry->is_global;
      out->is_ref = entry->is_ref;
      out->is_absolute_ref = entry->is_absolute_ref;
      out->read_expr = entry->read_expr;
      out->write_expr = entry->write_expr;
      out->base_offset = entry->offset;
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
      error_user("[%s:%d.%d] ref argument must be an lvalue", expr->file, expr->line, expr->column);
   }
   if (!emit_prepare_lvalue_ptr(ctx, &lv, LVALUE_ACCESS_ADDRESS)) {
      return false;
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

static void emit_runtime_fixed_binary_fp_fp(const char *helper, int dst_offset, int lhs_offset, int rhs_offset) {
   emit_prepare_fp_ptr(0, lhs_offset);
   emit_prepare_fp_ptr(1, rhs_offset);
   emit_prepare_fp_ptr(2, dst_offset);
   remember_runtime_import(helper);
   emit(&es_code, "    jsr _%s\n", helper);
}

static const char *int_addsub_helper_name(const ASTNode *type, int size, bool subtract, bool *is_generic_out) {
   if (is_generic_out) {
      *is_generic_out = false;
   }
   if (size < 3 || !type || has_flag(type_name_from_node(type), "$endian:big")) {
      return NULL;
   }
   switch (size) {
      case 3: return subtract ? "sub24" : "add24";
      case 4: return subtract ? "sub32" : "add32";
      default:
         if (is_generic_out) {
            *is_generic_out = true;
         }
         return subtract ? "subN" : "addN";
   }
}

static void emit_runtime_float_binary_fp_fp(const char *helper, int dst_offset, int lhs_offset, int rhs_offset, int size, int expbits) {
   emit_prepare_fp_ptr(0, lhs_offset);
   emit_prepare_fp_ptr(1, rhs_offset);
   emit_prepare_fp_ptr(2, dst_offset);
   emit(&es_code, "    lda #$%02x\n", size & 0xff);
   emit(&es_code, "    sta arg0\n");
   emit(&es_code, "    lda #$%02x\n", expbits & 0xff);
   emit(&es_code, "    sta arg1\n");
   remember_runtime_import(helper);
   emit(&es_code, "    jsr _%s\n", helper);
}

static void emit_runtime_float_compare(int lhs_offset, int rhs_offset, int size, int expbits) {
   emit_prepare_fp_ptr(0, lhs_offset);
   emit_prepare_fp_ptr(1, rhs_offset);
   emit(&es_code, "    lda #$%02x\n", size & 0xff);
   emit(&es_code, "    sta arg0\n");
   emit(&es_code, "    lda #$%02x\n", expbits & 0xff);
   emit(&es_code, "    sta arg1\n");
   remember_runtime_import("fcmp");
   emit(&es_code, "    jsr _fcmp\n");
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

static bool emit_prepare_lvalue_ptr_suffixes(Context *ctx, const ASTNode *suffixes, const ASTNode **type_io, const ASTNode **decl_io) {
   if (!suffixes || is_empty(suffixes)) {
      return true;
   }
   if (suffixes->count > 0 && !emit_prepare_lvalue_ptr_suffixes(ctx, suffixes->children[0], type_io, decl_io)) {
      return false;
   }
   if (!strcmp(suffixes->name, "[")) {
      const ASTNode *idx = unwrap_expr_node(suffixes->children[1]);
      int elem_size = declarator_first_element_size(*type_io, *decl_io);
      const ASTNode *next_decl;

      if (!idx || elem_size <= 0) {
         return false;
      }
      if (declarator_pointer_depth(*decl_io) > 0) {
         emit_deref_ptr(0);
      }
      else if (declarator_array_count(*decl_io) <= 0) {
         return false;
      }

      if (idx->kind == AST_INTEGER) {
         emit_add_immediate_to_ptr(0, atoi(idx->strval) * elem_size);
      }
      else {
         const ASTNode *idx_type = expr_value_type((ASTNode *) idx, ctx);
         int ptr_size = get_size("*");
         int saved_locals = ctx ? ctx->locals : 0;
         int idx_offset = saved_locals;
         int factor_offset = idx_offset + ptr_size;
         int scaled_offset = factor_offset + ptr_size;
         int save_ptr0_offset = elem_size != 1 ? (scaled_offset + ptr_size) : (idx_offset + ptr_size);
         int total = (save_ptr0_offset - idx_offset) + ptr_size;
         ContextEntry idx_tmp = { .name = "$idx", .type = idx_type ? idx_type : required_typename_node("int"), .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = idx_offset, .size = ptr_size };

         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         emit_store_ptr_to_fp(save_ptr0_offset, 0, ptr_size);
         if (ctx) {
            ctx->locals = saved_locals + total;
         }
         if (!compile_expr_to_slot((ASTNode *) idx, ctx, &idx_tmp)) {
            if (ctx) {
               ctx->locals = saved_locals;
            }
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
         if (ctx) {
            ctx->locals = saved_locals;
         }
         emit_load_ptr_from_fpvar(0, save_ptr0_offset);
         if (elem_size != 1) {
            unsigned char *factor_bytes = (unsigned char *) calloc(ptr_size ? ptr_size : 1, sizeof(unsigned char));
            char factor_buf[64];
            if (!factor_bytes) {
               remember_runtime_import("popN");
               emit(&es_code, "    lda #$%02x\n", total & 0xff);
               emit(&es_code, "    sta arg0\n");
               emit(&es_code, "    jsr _popN\n");
               return false;
            }
            snprintf(factor_buf, sizeof(factor_buf), "%d", elem_size);
            if (idx_type && has_flag(type_name_from_node(idx_type), "$endian:big")) {
               make_be_int(factor_buf, factor_bytes, ptr_size);
            }
            else {
               make_le_int(factor_buf, factor_bytes, ptr_size);
            }
            emit_store_immediate_to_fp(factor_offset, factor_bytes, ptr_size);
            free(factor_bytes);
            emit_runtime_binary_fp_fp("mulN", scaled_offset, idx_offset, factor_offset, ptr_size);
            emit_add_fp_to_ptr(0, scaled_offset, ptr_size);
         }
         else {
            emit_add_fp_to_ptr(0, idx_offset, ptr_size);
         }
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
      }

      next_decl = declarator_after_subscript(*decl_io);
      *decl_io = next_decl;
      return true;
   }
   if (!strcmp(suffixes->name, ".") || !strcmp(suffixes->name, "->")) {
      AggregateMemberInfo info = {0};

      if (!strcmp(suffixes->name, "->")) {
         if (declarator_pointer_depth(*decl_io) <= 0) {
            return false;
         }
         emit_deref_ptr(0);
      }
      if (!find_aggregate_member_info(*type_io, suffixes->children[1]->strval, &info)) {
         return false;
      }
      emit_add_immediate_to_ptr(0, info.byte_offset);
      *type_io = info.type;
      *decl_io = info.declarator;
      return true;
   }
   return true;
}

static bool emit_prepare_lvalue_ptr(Context *ctx, const LValueRef *lv, LValueAccessMode mode) {
   ContextEntry base_entry;
   char sym[256];
   const ASTNode *type;
   const ASTNode *decl;
   const char *abs_expr = NULL;

   if (!lv) {
      return false;
   }
   if (mode == LVALUE_ACCESS_ADDRESS && lv->is_bitfield) {
      return false;
   }

   if (lv->is_absolute_ref) {
      switch (mode) {
         case LVALUE_ACCESS_READ:
            abs_expr = lv->read_expr;
            break;
         case LVALUE_ACCESS_WRITE:
            abs_expr = lv->write_expr;
            break;
         case LVALUE_ACCESS_ADDRESS:
            if (lv->read_expr && lv->write_expr) {
               if (strcmp(lv->read_expr, lv->write_expr)) {
                  return false;
               }
               abs_expr = lv->read_expr;
            }
            else {
               abs_expr = lv->read_expr ? lv->read_expr : lv->write_expr;
            }
            break;
      }
      if (!abs_expr || !*abs_expr) {
         return false;
      }
      emit_load_expr_address_to_ptr(0, abs_expr, lv->ptr_adjust);
      if (!lv->base_type) {
         return true;
      }
      type = lv->base_type;
      decl = lv->base_declarator;
      return emit_prepare_lvalue_ptr_suffixes(ctx, lv->suffixes, &type, &decl);
   }

   if (!lv->base_type) {
      if (lv->indirect) {
         if (lv->is_static || lv->is_zeropage || lv->is_global) {
            base_entry = (ContextEntry){ .name = lv->name, .type = lv->type, .declarator = lv->declarator, .is_static = lv->is_static, .is_zeropage = lv->is_zeropage, .is_global = lv->is_global, .is_ref = lv->is_ref, .is_absolute_ref = lv->is_absolute_ref, .read_expr = lv->read_expr, .write_expr = lv->write_expr, .offset = lv->offset, .size = lv->size };
            if (!entry_symbol_name(ctx, &base_entry, sym, sizeof(sym))) {
               return false;
            }
            emit_load_ptr_from_symbol(0, sym, 0);
         }
         else {
            emit_load_ptr_from_fpvar(0, lv->offset);
         }
         emit_add_immediate_to_ptr(0, lv->ptr_adjust);
         return true;
      }
      if (lv->is_static || lv->is_zeropage || lv->is_global) {
         base_entry = (ContextEntry){ .name = lv->name, .type = lv->type, .declarator = lv->declarator, .is_static = lv->is_static, .is_zeropage = lv->is_zeropage, .is_global = lv->is_global, .is_ref = lv->is_ref, .is_absolute_ref = lv->is_absolute_ref, .read_expr = lv->read_expr, .write_expr = lv->write_expr, .offset = lv->offset, .size = lv->size };
         if (!entry_symbol_name(ctx, &base_entry, sym, sizeof(sym))) {
            return false;
         }
         emit_load_address_to_ptr(0, sym, 0);
      }
      else {
         emit_prepare_fp_ptr(0, lv->offset);
      }
      emit_add_immediate_to_ptr(0, lv->ptr_adjust);
      return true;
   }

   base_entry = (ContextEntry){ .name = lv->name, .type = lv->base_type, .declarator = lv->base_declarator, .is_static = lv->is_static, .is_zeropage = lv->is_zeropage, .is_global = lv->is_global, .is_ref = lv->is_ref, .is_absolute_ref = lv->is_absolute_ref, .read_expr = lv->read_expr, .write_expr = lv->write_expr, .offset = lv->base_offset, .size = declarator_storage_size(lv->base_type, lv->base_declarator) };
   type = lv->base_type;
   decl = lv->base_declarator;

   if (lv->is_static || lv->is_zeropage || lv->is_global) {
      if (!entry_symbol_name(ctx, &base_entry, sym, sizeof(sym))) {
         return false;
      }
      if (lv->deref_depth > 0 || lv->is_ref) {
         int extra_derefs = lv->deref_depth;
         emit_load_ptr_from_symbol(0, sym, 0);
         if (!lv->is_ref && extra_derefs > 0) {
            extra_derefs--;
         }
         for (int i = 0; i < extra_derefs; i++) {
            emit_deref_ptr(0);
         }
      }
      else {
         emit_load_address_to_ptr(0, sym, 0);
      }
   }
   else {
      if (lv->deref_depth > 0 || lv->is_ref) {
         int extra_derefs = lv->deref_depth;
         emit_load_ptr_from_fpvar(0, lv->base_offset);
         if (!lv->is_ref && extra_derefs > 0) {
            extra_derefs--;
         }
         for (int i = 0; i < extra_derefs; i++) {
            emit_deref_ptr(0);
         }
      }
      else {
         emit_prepare_fp_ptr(0, lv->base_offset);
      }
   }

   return emit_prepare_lvalue_ptr_suffixes(ctx, lv->suffixes, &type, &decl);
}

static bool emit_copy_bitfield_lvalue_to_fp(Context *ctx, int dst_offset, const LValueRef *src, int size) {
   int copy_size = size < src->size ? size : src->size;
   bool dst_direct = dst_offset >= 0 && dst_offset + copy_size <= 256;
   int saved_locals = ctx ? ctx->locals : 0;
   int protected_locals = saved_locals;
   int ptr_save_offset;
   bool is_signed;
   int src_byte_offset;
   int shift_bits;
   int raw_copy_size;
   int field_last_byte;
   int field_rem;

   if (copy_size <= 0) {
      return true;
   }
   if (dst_offset + copy_size > protected_locals) {
      protected_locals = dst_offset + copy_size;
   }
   ptr_save_offset = protected_locals;
   if (ctx) {
      ctx->locals = protected_locals;
   }
   if (!emit_prepare_lvalue_ptr(ctx, src, LVALUE_ACCESS_READ)) {
      if (ctx) {
         ctx->locals = saved_locals;
      }
      return false;
   }
   if (!dst_direct) {
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$02\n");
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      emit_store_ptr_to_fp(ptr_save_offset, 0, get_size("*"));
      if (ctx) {
         ctx->locals = protected_locals + get_size("*");
      }
      emit_prepare_fp_ptr(1, dst_offset);
      emit_load_ptr_from_fpvar(0, ptr_save_offset);
      if (ctx) {
         ctx->locals = protected_locals;
      }
   }
   if (dst_direct) {
      emit_prepare_fp_ptr(1, dst_offset);
   }

   emit_runtime_fill_ptr1(copy_size, 0x00);

   src_byte_offset = src->bit_offset / 8;
   shift_bits = src->bit_offset % 8;
   raw_copy_size = src->size - src_byte_offset;
   if (raw_copy_size > copy_size) {
      raw_copy_size = copy_size;
   }
   if (raw_copy_size > 0) {
      if (src_byte_offset > 0) {
         emit_add_immediate_to_ptr(0, src_byte_offset);
      }
      emit_runtime_copy_ptr0_to_ptr1("cpyN", raw_copy_size, raw_copy_size);
   }

   if (shift_bits > 0) {
      const char *outer_label = next_label("bitfield_load_shift_outer");
      const char *inner_label = next_label("bitfield_load_shift_inner");
      const char *done_label = next_label("bitfield_load_shift_done");

      emit(&es_code, "    ldx #$%02x\n", shift_bits & 0xff);
      emit(&es_code, "%s:\n", outer_label);
      emit(&es_code, "    cpx #0\n");
      emit(&es_code, "    beq %s\n", done_label);
      emit(&es_code, "    clc\n");
      emit(&es_code, "    ldy #$%02x\n", (copy_size - 1) & 0xff);
      emit(&es_code, "%s:\n", inner_label);
      emit(&es_code, "    lda (ptr1),y\n");
      emit(&es_code, "    ror a\n");
      emit(&es_code, "    sta (ptr1),y\n");
      emit(&es_code, "    dey\n");
      emit(&es_code, "    bpl %s\n", inner_label);
      emit(&es_code, "    dex\n");
      emit(&es_code, "    bne %s\n", outer_label);
      emit(&es_code, "%s:\n", done_label);
   }

   field_last_byte = (src->bit_width - 1) / 8;
   field_rem = src->bit_width % 8;
   if (src->bit_width > 0 && src->bit_width < copy_size * 8) {
      if (field_rem != 0) {
         emit(&es_code, "    ldy #%d\n", field_last_byte);
         emit(&es_code, "    lda (ptr1),y\n");
         emit(&es_code, "    and #$%02x\n", ((1 << field_rem) - 1) & 0xff);
         emit(&es_code, "    sta (ptr1),y\n");
      }
      if (copy_size - (field_last_byte + 1) > 0) {
         emit_add_immediate_to_ptr(1, field_last_byte + 1);
         emit_runtime_fill_ptr1(copy_size - (field_last_byte + 1), 0x00);
         emit_prepare_fp_ptr(1, dst_offset);
      }
   }

   is_signed = src->type && has_flag(type_name_from_node(src->type), "$signed");
   if (is_signed && src->bit_width > 0 && src->bit_width < copy_size * 8) {
      int sign_byte = (src->bit_width - 1) / 8;
      int sign_mask = 1 << ((src->bit_width - 1) % 8);
      int rem = src->bit_width % 8;
      const char *skip_label = next_label("bitfield_signext_skip");
      emit(&es_code, "    ldy #%d\n", sign_byte);
      emit(&es_code, "    lda (ptr1),y\n");
      emit(&es_code, "    and #$%02x\n", sign_mask & 0xff);
      emit(&es_code, "    beq %s\n", skip_label);
      if (rem != 0) {
         emit(&es_code, "    ldy #%d\n", sign_byte);
         emit(&es_code, "    lda (ptr1),y\n");
         emit(&es_code, "    ora #$%02x\n", ((0xff << rem) & 0xff));
         emit(&es_code, "    sta (ptr1),y\n");
      }
      if (copy_size - (sign_byte + 1) > 0) {
         emit_add_immediate_to_ptr(1, sign_byte + 1);
         emit_runtime_fill_ptr1(copy_size - (sign_byte + 1), 0xff);
         emit_prepare_fp_ptr(1, dst_offset);
      }
      emit(&es_code, "%s:\n", skip_label);
   }
   if (!dst_direct) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$02\n");
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
   }
   if (ctx) {
      ctx->locals = saved_locals;
   }
   return true;
}

static bool emit_copy_fp_to_bitfield_lvalue(Context *ctx, const LValueRef *dst, int src_offset, int size) {
   int copy_size = size < dst->size ? size : dst->size;
   bool src_direct = src_offset >= 0 && src_offset + copy_size <= 256;
   int saved_locals = ctx ? ctx->locals : 0;
   int protected_locals = saved_locals;
   int ptr_save_offset;

   if (copy_size <= 0) {
      return true;
   }
   if (src_offset + copy_size > protected_locals) {
      protected_locals = src_offset + copy_size;
   }
   ptr_save_offset = protected_locals;
   if (ctx) {
      ctx->locals = protected_locals;
   }
   if (!emit_prepare_lvalue_ptr(ctx, dst, LVALUE_ACCESS_WRITE)) {
      if (ctx) {
         ctx->locals = saved_locals;
      }
      return false;
   }
   if (!src_direct) {
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$02\n");
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      emit_store_ptr_to_fp(ptr_save_offset, 0, get_size("*"));
      if (ctx) {
         ctx->locals = protected_locals + get_size("*");
      }
      emit_prepare_fp_ptr(1, src_offset);
      emit_load_ptr_from_fpvar(0, ptr_save_offset);
      if (ctx) {
         ctx->locals = protected_locals;
      }
   }
   for (int bit = 0; bit < dst->bit_width; bit++) {
      int dst_byte = (dst->bit_offset + bit) / 8;
      int dst_mask = 1 << ((dst->bit_offset + bit) % 8);
      int src_byte = bit / 8;
      int src_mask = 1 << (bit % 8);
      const char *clear_label = next_label("bitfield_store_clear");
      const char *done_label = next_label("bitfield_store_done");
      emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + src_byte) : src_byte);
      emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr1)");
      emit(&es_code, "    and #$%02x\n", src_mask & 0xff);
      emit(&es_code, "    beq %s\n", clear_label);
      emit(&es_code, "    ldy #%d\n", dst_byte);
      emit(&es_code, "    lda (ptr0),y\n");
      emit(&es_code, "    ora #$%02x\n", dst_mask & 0xff);
      emit(&es_code, "    sta (ptr0),y\n");
      emit(&es_code, "    jmp %s\n", done_label);
      emit(&es_code, "%s:\n", clear_label);
      emit(&es_code, "    ldy #%d\n", dst_byte);
      emit(&es_code, "    lda (ptr0),y\n");
      emit(&es_code, "    and #$%02x\n", (0xff ^ dst_mask) & 0xff);
      emit(&es_code, "    sta (ptr0),y\n");
      emit(&es_code, "%s:\n", done_label);
   }
   if (!src_direct) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$02\n");
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
   }
   if (ctx) {
      ctx->locals = saved_locals;
   }
   return true;
}

static bool emit_copy_lvalue_to_fp(Context *ctx, int dst_offset, const LValueRef *src, int size) {
   int copy_size = size < src->size ? size : src->size;
   bool dst_direct = dst_offset >= 0 && dst_offset + copy_size <= 256;
   int saved_locals = ctx ? ctx->locals : 0;
   int protected_locals = saved_locals;
   int ptr_save_offset;

   if (src && src->is_bitfield) {
      return emit_copy_bitfield_lvalue_to_fp(ctx, dst_offset, src, size);
   }
   if (absolute_ref_supports_direct_access(src)) {
      const char *read_expr = src->read_expr;

      if (!read_expr || !*read_expr) {
         return false;
      }
      if (!dst_direct) {
         emit_prepare_fp_ptr(1, dst_offset);
      }
      for (int i = 0; i < copy_size; i++) {
         emit_load_a_from_expr_address(read_expr, src->offset + i);
         emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + i) : i);
         emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
      }
      return true;
   }
   if (copy_size <= 0) {
      return true;
   }
   if (dst_offset + copy_size > protected_locals) {
      protected_locals = dst_offset + copy_size;
   }
   ptr_save_offset = protected_locals;
   if (ctx) {
      ctx->locals = protected_locals;
   }
   if (!emit_prepare_lvalue_ptr(ctx, src, LVALUE_ACCESS_READ)) {
      if (ctx) {
         ctx->locals = saved_locals;
      }
      return false;
   }
   if (!dst_direct) {
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$02\n");
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      emit_store_ptr_to_fp(ptr_save_offset, 0, get_size("*"));
      if (ctx) {
         ctx->locals = protected_locals + get_size("*");
      }
      emit_prepare_fp_ptr(1, dst_offset);
      emit_load_ptr_from_fpvar(0, ptr_save_offset);
      if (ctx) {
         ctx->locals = protected_locals;
      }
   }
   for (int i = 0; i < copy_size; i++) {
      emit(&es_code, "    ldy #%d\n", i);
      emit(&es_code, "    lda (ptr0),y\n");
      emit(&es_code, "    ldy #%d\n", dst_direct ? (dst_offset + i) : i);
      emit(&es_code, "    sta %s,y\n", dst_direct ? "(fp)" : "(ptr1)");
   }
   if (!dst_direct) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$02\n");
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
   }
   if (ctx) {
      ctx->locals = saved_locals;
   }
   return true;
}

static bool emit_copy_fp_to_lvalue(Context *ctx, const LValueRef *dst, int src_offset, int size) {
   int copy_size = size < dst->size ? size : dst->size;
   bool src_direct = src_offset >= 0 && src_offset + copy_size <= 256;
   int saved_locals = ctx ? ctx->locals : 0;
   int protected_locals = saved_locals;
   int ptr_save_offset;

   if (dst && dst->is_bitfield) {
      return emit_copy_fp_to_bitfield_lvalue(ctx, dst, src_offset, size);
   }
   if (absolute_ref_supports_direct_access(dst)) {
      const char *write_expr = dst->write_expr;

      if (!write_expr || !*write_expr) {
         return false;
      }
      if (!src_direct) {
         emit_prepare_fp_ptr(1, src_offset);
      }
      for (int i = 0; i < copy_size; i++) {
         emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + i) : i);
         emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr1)");
         emit_store_a_to_expr_address(write_expr, dst->offset + i);
      }
      return true;
   }
   if (copy_size <= 0) {
      return true;
   }
   if (src_offset + copy_size > protected_locals) {
      protected_locals = src_offset + copy_size;
   }
   ptr_save_offset = protected_locals;
   if (ctx) {
      ctx->locals = protected_locals;
   }
   if (!emit_prepare_lvalue_ptr(ctx, dst, LVALUE_ACCESS_WRITE)) {
      if (ctx) {
         ctx->locals = saved_locals;
      }
      return false;
   }
   if (!src_direct) {
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$02\n");
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      emit_store_ptr_to_fp(ptr_save_offset, 0, get_size("*"));
      if (ctx) {
         ctx->locals = protected_locals + get_size("*");
      }
      emit_prepare_fp_ptr(1, src_offset);
      emit_load_ptr_from_fpvar(0, ptr_save_offset);
      if (ctx) {
         ctx->locals = protected_locals;
      }
   }
   for (int i = 0; i < copy_size; i++) {
      emit(&es_code, "    ldy #%d\n", src_direct ? (src_offset + i) : i);
      emit(&es_code, "    lda %s,y\n", src_direct ? "(fp)" : "(ptr1)");
      emit(&es_code, "    ldy #%d\n", i);
      emit(&es_code, "    sta (ptr0),y\n");
   }
   if (!src_direct) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$02\n");
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
   }
   if (ctx) {
      ctx->locals = saved_locals;
   }
   return true;
}
static bool resolve_lvalue_suffixes(Context *ctx, const ASTNode *suffixes, LValueRef *out) {
   if (!suffixes || is_empty(suffixes)) {
      return true;
   }
   if (suffixes->count > 0 && !resolve_lvalue_suffixes(ctx, suffixes->children[0], out)) {
      return false;
   }
   if (!strcmp(suffixes->name, "[")) {
      const ASTNode *idx = unwrap_expr_node(suffixes->children[1]);
      int elem_size = declarator_first_element_size(out->type, out->declarator);
      const ASTNode *next_decl = declarator_after_subscript(out->declarator);

      if (!idx || elem_size <= 0) {
         return false;
      }
      if (declarator_pointer_depth(out->declarator) > 0) {
         out->indirect = true;
         if (idx->kind == AST_INTEGER && !out->needs_runtime_address) {
            out->ptr_adjust += atoi(idx->strval) * elem_size;
         }
         else if (ctx) {
            out->needs_runtime_address = true;
         }
         else {
            return false;
         }
      }
      else if (declarator_array_count(out->declarator) > 0) {
         if (idx->kind == AST_INTEGER && !out->needs_runtime_address) {
            if (out->indirect) {
               out->ptr_adjust += atoi(idx->strval) * elem_size;
            }
            else {
               out->offset += atoi(idx->strval) * elem_size;
            }
         }
         else if (ctx) {
            out->needs_runtime_address = true;
         }
         else {
            return false;
         }
      }
      else {
         error_user("[%s:%d.%d] cannot subscript non-pointer/non-array '%s'",
               suffixes->file, suffixes->line, suffixes->column,
               out->name ? out->name : "<unnamed>");
      }
      out->declarator = next_decl;
      out->size = out->declarator ? declarator_storage_size(out->type, out->declarator) : get_size(type_name_from_node(out->type));
      out->is_bitfield = false;
      out->bit_offset = 0;
      out->bit_width = 0;
      out->bit_storage_size = 0;
      return true;
   }
   if (!strcmp(suffixes->name, ".") || !strcmp(suffixes->name, "->")) {
      AggregateMemberInfo info = {0};
      if (!find_aggregate_member_info(out->type, suffixes->children[1]->strval, &info)) {
         return false;
      }
      if (!strcmp(suffixes->name, "->")) {
         if (declarator_pointer_depth(out->declarator) <= 0) {
            error_user("[%s:%d.%d] cannot use '->' on non-pointer '%s'",
                  suffixes->file, suffixes->line, suffixes->column,
                  out->name ? out->name : "<unnamed>");
         }
         out->indirect = true;
         if (!out->needs_runtime_address) {
            out->ptr_adjust += info.byte_offset;
         }
      }
      else if (out->indirect) {
         if (!out->needs_runtime_address) {
            out->ptr_adjust += info.byte_offset;
         }
      }
      else {
         out->offset += info.byte_offset;
      }
      out->type = info.type;
      out->declarator = info.declarator;
      out->size = declarator_storage_size(info.type, info.declarator);
      out->is_bitfield = info.is_bitfield;
      out->bit_offset = info.bit_offset;
      out->bit_width = info.bit_width;
      out->bit_storage_size = info.storage_size;
      return true;
   }
   return true;
}
static ContextEntry *lookup_lvalue_entry(Context *ctx, const char *name, ContextEntry *scratch) {
   ContextEntry *entry;
   const ASTNode *g;

   if (!name) {
      return NULL;
   }

   entry = ctx_lookup(ctx, name);
   if (entry) {
      return entry;
   }

   g = global_decl_lookup(name);
   if (g && g->count >= 3 && scratch && init_context_entry_from_global_decl(scratch, name, g)) {
      return scratch;
   }

   return NULL;
}

static void init_lvalue_from_entry(LValueRef *out, const ContextEntry *entry, const char *fallback_name) {
   out->name = entry->name ? entry->name : fallback_name;
   out->type = entry->type;
   out->declarator = entry->declarator;
   out->base_type = entry->type;
   out->base_declarator = entry->declarator;
   out->is_static = entry->is_static;
   out->is_zeropage = entry->is_zeropage;
   out->is_global = entry->is_global;
   out->is_ref = entry->is_ref;
   out->is_absolute_ref = entry->is_absolute_ref;
   out->read_expr = entry->read_expr;
   out->write_expr = entry->write_expr;
   out->base_offset = entry->offset;
   out->offset = entry->offset;
   out->size = entry->size;
   out->deref_depth = 0;
   out->indirect = entry->is_ref;
}

static bool resolve_lvalue_base(Context *ctx, ASTNode *base, LValueRef *out) {
   ContextEntry scratch;
   ContextEntry *entry;

   if (!base || !out) {
      return false;
   }

   if (!strcmp(base->name, "lvalue_base")) {
      if (base->count == 0 || base->children[0]->kind != AST_IDENTIFIER) {
         return false;
      }
      entry = lookup_lvalue_entry(ctx, base->children[0]->strval, &scratch);
      if (!entry) {
         return false;
      }
      init_lvalue_from_entry(out, entry, base->children[0]->strval);
      return true;
   }

   if (!strcmp(base->name, "*") && base->count > 0) {
      if (!resolve_lvalue_base(ctx, base->children[0], out)) {
         return false;
      }
      if (declarator_pointer_depth(out->declarator) <= 0) {
         error_user("[%s:%d.%d] cannot dereference non-pointer '%s'",
               base->file, base->line, base->column,
               out->name ? out->name : "<unnamed>");
      }
      out->declarator = declarator_after_deref(out->declarator);
      out->size = out->declarator ? declarator_storage_size(out->type, out->declarator) : get_size(type_name_from_node(out->type));
      out->indirect = true;
      out->deref_depth++;
      return true;
   }

   return false;
}

static bool resolve_lvalue(Context *ctx, ASTNode *node, LValueRef *out) {
   ASTNode *base;

   if (!node || strcmp(node->name, "lvalue") || node->count == 0 || !out) {
      return false;
   }

   memset(out, 0, sizeof(*out));
   out->suffixes = node->children[1];
   base = node->children[0];
   if (!base) {
      return false;
   }

   if (!resolve_lvalue_base(ctx, base, out)) {
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

static bool function_has_static_parameters(const ASTNode *fn) {
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

static int call_graph_node_index_for_function(const ASTNode *fn) {
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

static void emit_function_parameter_storage(const ASTNode *node, Context *ctx) {
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

static void emit_function_parameter_exports(const ASTNode *node) {
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

static void emit_variadic_hidden_local_setup(const ASTNode *node, Context *ctx) {
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
         fn = resolve_function_call_target(callee_name, args, ctx);
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

static const char *expr_bare_identifier_name(ASTNode *expr) {
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

static bool compile_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst) {
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

   if (!strcmp(expr->name, "cast")) {
      const ASTNode *target_type = cast_expr_target_type(expr);
      const ASTNode *target_decl = cast_expr_target_declarator(expr);
      int target_size = cast_expr_target_size(expr);
      ContextEntry tmp;
      if (!target_type || target_size <= 0 || expr->count < 2) {
         return false;
      }
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", target_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      tmp = (ContextEntry){ .name = "$cast", .type = target_type, .declarator = target_decl, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = target_size };
      if (!compile_expr_to_slot(expr->children[1], ctx, &tmp)) {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", target_size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         return false;
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
            error_user("[%s:%d.%d] overloaded %s has unknown return size", expr->file, expr->line, expr->column, inc ? "operator++" : "operator--");
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
         call = make_synthetic_call_expr(expr, declarator_name(function_declarator_node(ofn)), argv, 1);
         if (!call) {
            return false;
         }

         remember_runtime_import("pushN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _pushN\n");
         if (!emit_copy_lvalue_to_fp(ctx, ctx->locals, &lv, old_size)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            return false;
         }
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
         if (!emit_copy_fp_to_lvalue(ctx, &lv, store_offset, store_size)) {
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
         call = make_synthetic_call_expr(expr, declarator_name(function_declarator_node(ofn)), argv, expr->count);
         return call ? compile_call_expr_to_slot(call, ctx, dst) : false;
      }
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
         int saved_locals = ctx ? ctx->locals : 0;
         int lhs_offset = saved_locals;
         int rhs_offset = lhs_offset + work_size;
         int factor_offset = 0;
         int scaled_offset = 0;
         int value_offset = rhs_offset;
         int tmp_total = work_size * 2;
         const ASTNode *rhs_slot_type = scaled_pointer_arith ? rhs_type : work_type;
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
            emit_runtime_binary_fp_fp("mulN", scaled_offset, rhs_offset, factor_offset, work_size);
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

      helper = !strcmp(op, "<<") ? "lslN" : (op_type && has_flag(type_name_from_node(op_type), "$signed") ? "asrN" : "lsrN");
      emit_runtime_shift_fp(helper, lhs_offset, aux_offset, rhs_offset, lhs_size);

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
            emit_runtime_binary_fp_fp("mulN", aux_offset, lhs_offset, rhs_offset, op_size);
         }
         emit_copy_fp_to_fp(lhs_offset, aux_offset, op_size);
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
            remember_runtime_import("divN");
            emit(&es_code, "    jsr _divN\n");
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


static const ASTNode *expr_value_type(ASTNode *expr, Context *ctx) {
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
            fn = resolve_function_call_target(callee_name, args, ctx);
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
            fn = resolve_function_call_target(callee_name, args, ctx);
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

   if (!strcmp(expr->name, "sizeof")) {
      return get_size("int");
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
      bool is_signed;
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
         type = promoted_integer_type_for_binary(lhs_type, rhs_type, expr);
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
      is_signed = type_is_signed_integer(type);
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
      return compile_truthy_expr_branch_false(expr, ctx, type, NULL, size, false_label);
   }
}

static void compile_if_stmt(ASTNode *node, Context *ctx) {
   const char *false_label = next_label("if_false");
   const char *end_label = next_label("if_end");
   ASTNode *cond = node->children[0];
   ASTNode *then_block = node->children[1];
   ASTNode *else_block = (node->count > 2) ? node->children[2] : NULL;

   if (!compile_condition_branch_false(cond, ctx, false_label)) {
      error_unreachable("[%s:%d.%d] if condition not compiled yet", node->file, node->line, node->column);
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
      error_unreachable("[%s:%d.%d] while condition not compiled yet", node->file, node->line, node->column);
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
      if (!strcmp(init->name, "defdecl_stmt")) {
         ASTNode *list = init->children[0];
         for (int i = 0; i < list->count; i++) {
            compile_local_decl_item(list->children[i], ctx);
         }
      }
      else {
         compile_expr(init, ctx);
      }
   }

   emit(&es_code, "%s:\n", start_label);
   if (cond && !is_empty(cond)) {
      if (!compile_condition_branch_false(cond, ctx, end_label)) {
         error_unreachable("[%s:%d.%d] for condition not compiled yet", node->file, node->line, node->column);
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
      error_user("[%s:%d.%d] break used outside loop", node->file, node->line, node->column);
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
      error_user("[%s:%d.%d] continue used outside loop", node->file, node->line, node->column);
      return;
   }

   emit(&es_code, "    jmp %s\n", target);
}

static void predeclare_local_decl_item(ASTNode *node, Context *ctx) {
   ASTNode *modifiers  = node->children[0];
   ASTNode *type       = node->children[1];
   ASTNode *declarator = (ASTNode *) decl_node_declarator(node);
   const ASTNode *addrspec = decl_node_address_spec(node);
   const char *name    = declarator_name(declarator);
   int size            = declarator_storage_size(type, declarator);
   ContextEntry *entry = (ContextEntry *) set_get(ctx->vars, name);
   validate_nonreserved_variadic_name(name, node);

   if (entry != NULL) {
      return;
   }

   if (addrspec != NULL && !has_modifier(modifiers, "ref")) {
      warn_address_spec_without_ref(node, name);
   }

   if (has_modifier(modifiers, "ref") && addrspec != NULL) {
      if (!address_spec_has_read(addrspec) && !address_spec_has_write(addrspec)) {
         error_user("[%s:%d.%d] absolute ref '%s' cannot use none for both read and write address",
               node->file, node->line, node->column, name);
      }
      entry = (ContextEntry *) malloc(sizeof(ContextEntry));
      if (!entry) {
         error_unreachable("out of memory");
      }
      entry->name = strdup(name);
      entry->is_static = false;
      entry->is_zeropage = false;
      entry->is_global = false;
      entry->is_ref = false;
      entry->is_absolute_ref = true;
      entry->read_expr = address_spec_read_expr(addrspec);
      entry->write_expr = address_spec_write_expr(addrspec);
      entry->type = type;
      entry->declarator = declarator;
      entry->size = size;
      entry->offset = 0;
      set_add(ctx->vars, strdup(name), entry);
      return;
   }

   if (has_modifier(modifiers, "static") || modifiers_imply_named_nonzeropage(modifiers)) {
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
      if (!has_modifier(modifiers, "static") && !modifiers_imply_mem_storage(modifiers)) {
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

static const ASTNode *scalar_braced_initializer_value(const ASTNode *uinit, const ASTNode *type, const ASTNode *declarator) {
   const ASTNode *item = NULL;
   const ASTNode *items[1] = { NULL };
   int index = 0;

   if (!initializer_is_list(uinit)) {
      return NULL;
   }

   if ((declarator && declarator_array_count(declarator) > 0 && declarator_pointer_depth(declarator) == 0) || type_is_aggregate(type)) {
      return NULL;
   }

   initializer_collect_items(uinit, items, &index);
   if (initializer_item_count(uinit) != 1 || index != 1 || !items[0] || items[0]->count < 2) {
      error_user("[%s:%d.%d] too many initializers for scalar", uinit->file, uinit->line, uinit->column);
   }

   item = items[0];
   if (!is_empty(item->children[0])) {
      error_user("[%s:%d.%d] designated initializer not valid for scalar", item->file, item->line, item->column);
   }

   return item->children[1];
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

static int constant_shift_width_bits(ASTNode *expr) {
   int size = expr_value_size(expr, NULL);

   if (size <= 0) {
      size = (int) sizeof(long long);
   }
   if (size > (int) sizeof(long long)) {
      size = (int) sizeof(long long);
   }
   return size * 8;
}

static void diagnose_constant_shift_count(ASTNode *count_expr, int lhs_bits) {
   InitConstValue value = {0};

   if (!count_expr) {
      return;
   }
   if (lhs_bits <= 0) {
      lhs_bits = (int) (sizeof(long long) * 8);
   }
   if (!eval_constant_initializer_expr(count_expr, &value) || value.kind != INIT_CONST_INT) {
      return;
   }

   if (value.i < 0) {
      error_user("[%s:%d.%d] negative shift count %lld", count_expr->file, count_expr->line, count_expr->column, value.i);
   }
   if (value.i >= lhs_bits) {
      error_user("[%s:%d.%d] shift count %lld exceeds %d-bit left operand", count_expr->file, count_expr->line, count_expr->column, value.i, lhs_bits);
   }
}

static long long arithmetic_right_shift_ll(long long value, unsigned int count) {
   unsigned long long bits;

   if (count == 0) {
      return value;
   }
   if (count >= sizeof(long long) * 8U) {
      return value < 0 ? -1LL : 0LL;
   }
   if (value >= 0) {
      return (long long) (((unsigned long long) value) >> count);
   }

   bits = (unsigned long long) value;
   return (long long) (~((~bits) >> count));
}

static bool eval_constant_cast_expr(ASTNode *expr, InitConstValue *out) {
   InitConstValue inner = {0};
   const ASTNode *target_type = cast_expr_target_type(expr);
   const ASTNode *target_decl = cast_expr_target_declarator(expr);
   int target_size;
   bool target_is_pointer;
   bool target_is_float;
   bool target_is_signed;
   unsigned long long bits;
   unsigned long long mask;
   long long ival;

   if (!expr || !out || !target_type || expr->count < 2) {
      return false;
   }
   if (!eval_constant_initializer_expr(expr->children[1], &inner)) {
      return false;
   }

   target_size = declarator_storage_size(target_type, target_decl);
   if (target_size <= 0) {
      target_size = type_size_from_node(target_type);
   }
   if (target_size <= 0) {
      return false;
   }

   target_is_pointer = (target_decl && declarator_pointer_depth(target_decl) > 0) ||
      !strcmp(type_name_from_node(target_type), "*");
   target_is_float = type_is_float_like(target_type);
   target_is_signed = has_flag(type_name_from_node(target_type), "$signed");

   if (target_is_float) {
      if (inner.kind == INIT_CONST_FLOAT) {
         *out = inner;
         return true;
      }
      if (inner.kind == INIT_CONST_INT) {
         out->kind = INIT_CONST_FLOAT;
         out->f = (double) inner.i;
         return true;
      }
      return false;
   }

   if (inner.kind == INIT_CONST_ADDRESS) {
      if (target_is_pointer) {
         *out = inner;
         return true;
      }
      return false;
   }

   if (inner.kind == INIT_CONST_FLOAT) {
      ival = (long long) inner.f;
   }
   else if (inner.kind == INIT_CONST_INT) {
      ival = inner.i;
   }
   else {
      return false;
   }

   bits = (unsigned long long) ival;
   if (target_size < (int) sizeof(bits)) {
      mask = (1ULL << (target_size * 8)) - 1ULL;
      bits &= mask;
      if (target_is_signed && target_size > 0) {
         unsigned long long sign = 1ULL << (target_size * 8 - 1);
         if (bits & sign) {
            bits |= ~mask;
         }
      }
   }

   out->kind = INIT_CONST_INT;
   out->i = (long long) bits;
   return true;
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

   if (!strcmp(expr->name, "cast")) {
      return eval_constant_cast_expr(expr, out);
   }

   if (expr->kind == AST_INTEGER) {
      out->kind = INIT_CONST_INT;
      out->i = parse_int(expr->strval);
      out->int_text = expr->strval;
      return true;
   }

   if (expr->kind == AST_FLOAT) {
      out->kind = INIT_CONST_FLOAT;
      out->f = parse_float(expr->strval);
      return true;
   }

   if (expr->kind == AST_STRING) {
      long long ch_value = 0;

      if (decode_char_constant_value(expr->strval, &ch_value)) {
         out->kind = INIT_CONST_INT;
         out->i = ch_value;
         return true;
      }

      out->kind = INIT_CONST_ADDRESS;
      out->symbol = remember_string_literal(expr->strval);
      out->addend = 0;
      return true;
   }

   {
      const char *ident = expr_bare_identifier_name(expr);
      if (ident) {
         const ASTNode *fn = resolve_function_designator_target(ident, NULL, NULL);
         char sym[512];
         if (fn) {
            if (function_has_static_parameters(fn)) {
               error_user("[%s:%d.%d] cannot create a pointer to function '%s' because it has symbol-backed parameters", expr->file, expr->line, expr->column, ident);
            }
            if (function_symbol_name(fn, ident, sym, sizeof(sym))) {
               char label[sizeof(sym) + 2];
               snprintf(label, sizeof(label), "%s", sym);
               out->kind = INIT_CONST_ADDRESS;
               out->symbol = strdup(label);
               out->addend = 0;
               return true;
            }
         }
      }
   }

   if ((!strcmp(expr->name, "conditional_expr") && expr->count == 4 && expr->children[0] &&
        expr->children[0]->kind == AST_IDENTIFIER && !strcmp(expr->children[0]->strval, "?:")) ||
       (!strcmp(expr->name, "?:") && expr->count >= 3)) {
      InitConstValue cond = {0};
      ASTNode *test = !strcmp(expr->name, "conditional_expr") ? expr->children[1] : expr->children[0];
      ASTNode *iftrue = !strcmp(expr->name, "conditional_expr") ? expr->children[2] : expr->children[1];
      ASTNode *iffalse = !strcmp(expr->name, "conditional_expr") ? expr->children[3] : expr->children[2];

      if (!eval_constant_initializer_expr(test, &cond)) {
         return false;
      }
      if (init_const_truthy(&cond)) {
         return eval_constant_initializer_expr(iftrue, out);
      }
      return eval_constant_initializer_expr(iffalse, out);
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
         {
            const char *ident = expr_bare_identifier_name(inner);
            char sym[512];
            const ASTNode *fn = ident ? resolve_function_designator_target(ident, NULL, NULL) : NULL;
            if (fn) {
               if (function_has_static_parameters(fn)) {
                  error_user("[%s:%d.%d] cannot create a pointer to function '%s' because it has symbol-backed parameters", inner->file, inner->line, inner->column, ident);
               }
               if (function_symbol_name(fn, ident, sym, sizeof(sym))) {
                  char label[sizeof(sym) + 2];
                  snprintf(label, sizeof(label), "%s", sym);
                  out->kind = INIT_CONST_ADDRESS;
                  out->symbol = strdup(label);
                  out->addend = 0;
                  return true;
               }
            }
         }
         if (eval_constant_initializer_expr(inner, &lhs) && lhs.kind == INIT_CONST_INT) {
            out->kind = INIT_CONST_INT;
            out->i = lhs.i;
            return true;
         }
         return false;
      }
   }

   if (expr->count == 2) {
      if (!strcmp(expr->name, "&&") || !strcmp(expr->name, "||")) {
         bool lhs_truthy;

         if (!eval_constant_initializer_expr(expr->children[0], &lhs)) {
            return false;
         }
         lhs_truthy = init_const_truthy(&lhs);
         out->kind = INIT_CONST_INT;
         if (!strcmp(expr->name, "&&") && !lhs_truthy) {
            out->i = 0;
            return true;
         }
         if (!strcmp(expr->name, "||") && lhs_truthy) {
            out->i = 1;
            return true;
         }
         if (!eval_constant_initializer_expr(expr->children[1], &rhs)) {
            return false;
         }
         out->i = init_const_truthy(&rhs) ? 1 : 0;
         return true;
      }

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
         if (!strcmp(expr->name, "<<") || !strcmp(expr->name, ">>")) {
            int lhs_bits = constant_shift_width_bits(expr->children[0]);
            unsigned int shift_count;

            if (rhs.i < 0) {
               error_user("[%s:%d.%d] negative shift count %lld", expr->children[1]->file, expr->children[1]->line, expr->children[1]->column, rhs.i);
            }
            if (rhs.i >= lhs_bits) {
               error_user("[%s:%d.%d] shift count %lld exceeds %d-bit left operand", expr->children[1]->file, expr->children[1]->line, expr->children[1]->column, rhs.i, lhs_bits);
            }
            shift_count = (unsigned int) rhs.i;
            if (!strcmp(expr->name, "<<")) {
               out->i = (long long) (((unsigned long long) lhs.i) << shift_count);
            }
            else {
               out->i = arithmetic_right_shift_ll(lhs.i, shift_count);
            }
         }
         else if (!strcmp(expr->name, "&")) out->i = lhs.i & rhs.i;
         else if (!strcmp(expr->name, "|")) out->i = lhs.i | rhs.i;
         else out->i = lhs.i ^ rhs.i;
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

static bool encode_init_const_int_value(const InitConstValue *value, unsigned char *buf, int size, const ASTNode *type) {
   if (!value) {
      return false;
   }

   if (value->int_text && value->i >= 0) {
      if (has_flag(type_name_from_node(type), "$endian:big")) {
         make_be_int(value->int_text, buf, size);
      }
      else {
         make_le_int(value->int_text, buf, size);
      }
      return true;
   }

   return encode_integer_initializer_value(value->i, buf, size, type);
}

static bool encode_float_initializer_value(double value, unsigned char *buf, int size, const ASTNode *type) {
   char tmp[256];
   const char *style;
   if (!buf || size < 0 || !type) {
      return false;
   }
   style = type_float_style(type);
   if (!style) {
      return false;
   }
   snprintf(tmp, sizeof(tmp), "%la", value);
   if (has_flag(type_name_from_node(type), "$endian:big")) {
      make_be_float_style(tmp, buf, size, style);
   }
   else {
      make_le_float_style(tmp, buf, size, style);
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

   if (uexpr) {
      const char *label = emit_pointer_initializer_backing_object(type, declarator, uexpr);
      if (label) {
         return emit_symbol_address_initializer(es, size, type, label, 0);
      }
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

static void emit_sink_append(EmitSink *dst, const EmitSink *src) {
   if (!dst || !src) {
      return;
   }
   for (EmitPiece *piece = src->head; piece; piece = piece->next) {
      emit(dst, "%s", piece->txt);
   }
}

static void remember_pending_global_init(const char *name, const char *symbol, const ASTNode *type, const ASTNode *declarator, ASTNode *expression, int size, bool is_zeropage, bool is_absolute_ref, const char *read_expr, const char *write_expr) {
   PendingGlobalInit *items;
   PendingGlobalInit *entry;

   items = (PendingGlobalInit *) realloc(pending_global_inits,
                                         sizeof(*pending_global_inits) * (pending_global_init_count + 1));
   if (!items) {
      error_unreachable("out of memory");
   }
   pending_global_inits = items;

   entry = &pending_global_inits[pending_global_init_count++];
   entry->name = strdup(name);
   entry->symbol = strdup(symbol ? symbol : name);
   entry->type = type;
   entry->declarator = declarator;
   entry->expression = expression;
   entry->size = size;
   entry->is_zeropage = is_zeropage;
   entry->is_absolute_ref = is_absolute_ref;
   entry->read_expr = read_expr ? strdup(read_expr) : NULL;
   entry->write_expr = write_expr ? strdup(write_expr) : NULL;

   if (size > pending_global_init_max_size) {
      pending_global_init_max_size = size;
   }
}

static unsigned long hash_runtime_init_name(const char *text) {
   unsigned long hash = 2166136261u;

   if (!text) {
      return 0u;
   }

   for (const unsigned char *p = (const unsigned char *) text; *p; ++p) {
      hash ^= (unsigned long) *p;
      hash *= 16777619u;
   }

   return hash;
}

static const char *runtime_global_init_symbol(void) {
   if (!runtime_global_init_symbol_ready) {
      unsigned long hash = hash_runtime_init_name(root_filename ? root_filename : "<stdin>");
      snprintf(runtime_global_init_symbol_buf, sizeof(runtime_global_init_symbol_buf), "__init_%08lx", hash & 0xfffffffful);
      runtime_global_init_symbol_ready = true;
   }
   return runtime_global_init_symbol_buf;
}

static void emit_runtime_global_init_function(void) {
   Context ctx;
   const char *sym;

   if (pending_global_init_count <= 0) {
      return;
   }

   sym = runtime_global_init_symbol();
   emit(&es_export, ".export %s\n", sym);

   ctx.name = sym;
   ctx.locals = pending_global_init_max_size;
   ctx.params = 0;
   ctx.vars = new_set();
   ctx.break_label = NULL;
   ctx.continue_label = NULL;

   emit(&es_code, ".proc %s\n", sym);
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

   for (int i = 0; i < pending_global_init_count; i++) {
      PendingGlobalInit *entry = &pending_global_inits[i];

      if (entry->size > 0) {
         emit_fill_fp_bytes(0, 0, entry->size, 0x00);
      }
      if (!compile_initializer_to_fp(entry->expression, &ctx, entry->type, entry->declarator, 0, entry->size)) {
         error_unreachable("[%s:%d.%d] could not compile runtime global initializer for '%s'",
               entry->expression->file, entry->expression->line, entry->expression->column, entry->name);
      }
      if (entry->is_absolute_ref) {
         LValueRef lv = { .name = entry->name, .type = entry->type, .declarator = entry->declarator, .base_type = entry->type, .base_declarator = entry->declarator, .is_static = false, .is_zeropage = false, .is_global = true, .is_ref = true, .is_absolute_ref = true, .read_expr = entry->read_expr, .write_expr = entry->write_expr, .offset = 0, .size = entry->size };
         if (!emit_copy_fp_to_lvalue(&ctx, &lv, 0, entry->size)) {
            error_unreachable("[%s:%d.%d] could not store runtime initializer for absolute ref '%s'",
                  entry->expression->file, entry->expression->line, entry->expression->column, entry->name);
         }
      }
      else {
         if (!entry->symbol) {
            error_unreachable("[%s:%d.%d] missing runtime initializer symbol for '%s'",
                  entry->expression->file, entry->expression->line, entry->expression->column, entry->name);
         }

         emit_copy_fp_to_symbol(entry->symbol, 0, entry->size);
      }
   }

   if (ctx.locals > 0) {
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", ctx.locals & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
   }
   emit(&es_code, "    rts\n");
   emit(&es_code, ".endproc\n");
}

static const char *aggregate_initializer_target_name(const ASTNode *type) {
   const char *name = type ? type_name_from_node(type) : NULL;
   return name ? name : "aggregate";
}

static bool compile_initializer_to_fp(const ASTNode *init, Context *ctx, const ASTNode *type, const ASTNode *declarator, int base_offset, int total_size) {
   const ASTNode *uinit = unwrap_expr_node((ASTNode *) init);
   int size = scalar_storage_size(type, declarator, total_size);

   if (!uinit || is_empty(uinit)) {
      return true;
   }

   if (uinit->kind == AST_STRING && !string_literal_is_char_constant(uinit->strval)) {
      return emit_string_initializer_to_fp(type, declarator, base_offset, size, uinit->strval);
   }

   if (!initializer_is_list(uinit)) {
      ContextEntry dst = { .name = "$init", .type = type, .declarator = declarator, .is_static = false, .is_zeropage = false, .is_global = false, .offset = base_offset, .size = size };
      return compile_expr_to_slot((ASTNode *) uinit, ctx, &dst);
   }

   {
      const ASTNode *scalar_init = scalar_braced_initializer_value(uinit, type, declarator);
      if (scalar_init) {
         return compile_initializer_to_fp(scalar_init, ctx, type, declarator, base_offset, total_size);
      }
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
      if (is_union && index > 1) {
         free(items);
         error_user("[%s:%d.%d] too many initializers for '%s'", uinit->file, uinit->line, uinit->column,
               aggregate_initializer_target_name(type));
      }
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
               find_aggregate_member(type, declarator_name(fdecl), NULL, NULL, &offset);
               field_pos++;
               break;
            }
            if (!ftype || !fdecl) {
               free(items);
               error_user("[%s:%d.%d] too many initializers for '%s'", item->file, item->line, item->column,
                     aggregate_initializer_target_name(type));
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

   if (uinit->kind == AST_STRING && !string_literal_is_char_constant(uinit->strval)) {
      return emit_string_initializer_bytes(buf, buf_size, base_offset, type, declarator, size, uinit->strval);
   }

   if (pointer_initializer_uses_backing_object(type, declarator, uinit)) {
      return false;
   }

   if (!initializer_is_list(uinit)) {
      InitConstValue value = {0};
      if (!eval_constant_initializer_expr((ASTNode *) uinit, &value)) {
         return false;
      }
      if (value.kind == INIT_CONST_FLOAT || type_is_float_like(type)) {
         if (value.kind != INIT_CONST_FLOAT && value.kind != INIT_CONST_INT) {
            return false;
         }
         return encode_float_initializer_value(value.kind == INIT_CONST_FLOAT ? value.f : (double) value.i,
               buf + base_offset, size, type);
      }
      if (value.kind != INIT_CONST_INT) {
         return false;
      }
      return encode_init_const_int_value(&value, buf + base_offset, size, type);
   }

   {
      const ASTNode *scalar_init = scalar_braced_initializer_value(uinit, type, declarator);
      if (scalar_init) {
         return build_initializer_bytes(buf, buf_size, base_offset, scalar_init, type, declarator, total_size);
      }
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
      if (is_union && index > 1) {
         free(items);
         error_user("[%s:%d.%d] too many initializers for '%s'", uinit->file, uinit->line, uinit->column,
               aggregate_initializer_target_name(type));
      }
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
               find_aggregate_member(type, declarator_name(fdecl), NULL, NULL, &offset);
               field_pos++;
               break;
            }
            if (!ftype || !fdecl) {
               free(items);
               error_user("[%s:%d.%d] too many initializers for '%s'", item->file, item->line, item->column,
                     aggregate_initializer_target_name(type));
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
         if (stmt->count > 0 && stmt->children[0] && !is_empty(stmt->children[0]) && !strcmp(stmt->children[0]->name, "defdecl_stmt")) {
            ASTNode *list = stmt->children[0]->children[0];
            for (int j = 0; j < list->count; j++) {
               predeclare_local_decl_item(list->children[j], ctx);
            }
         }
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
   ASTNode *declarator = (ASTNode *) decl_node_declarator(node);
   const char *name    = declarator_name(declarator);
   ASTNode *expression = node->children[node->count - 1];
   validate_nonreserved_variadic_name(name, node);
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
      error_unreachable("[%s:%d.%d] local declaration for '%s' not compiled yet", node->file, node->line, node->column, name);
      return;
   }

   if (entry->is_absolute_ref) {
      if (is_empty(expression)) {
         return;
      }
      remember_runtime_import("pushN");
      emit(&es_code, "    lda #$%02x\n", size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _pushN\n");
      if (initializer_is_list(unwrap_expr_node(expression)) || declarator_array_count(declarator) > 0 || type_is_aggregate(type)) {
         emit_fill_fp_bytes(ctx->locals, 0, size, 0x00);
         if (!compile_initializer_to_fp(expression, ctx, type, declarator, ctx->locals, size)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            error_unreachable("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
            return;
         }
      }
      else if (!compile_expr_to_slot(expression, ctx, &(ContextEntry){ .name = "$tmp", .type = type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = ctx->locals, .size = size })) {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", size & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         error_unreachable("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
         return;
      }
      {
         LValueRef lv = { .name = entry->name, .type = entry->type, .declarator = entry->declarator, .base_type = entry->type, .base_declarator = entry->declarator, .is_static = entry->is_static, .is_zeropage = entry->is_zeropage, .is_global = entry->is_global, .is_ref = entry->is_ref, .is_absolute_ref = entry->is_absolute_ref, .read_expr = entry->read_expr, .write_expr = entry->write_expr, .offset = entry->offset, .size = entry->size };
         if (!emit_copy_fp_to_lvalue(ctx, &lv, ctx->locals, size)) {
            remember_runtime_import("popN");
            emit(&es_code, "    lda #$%02x\n", size & 0xff);
            emit(&es_code, "    sta arg0\n");
            emit(&es_code, "    jsr _popN\n");
            error_unreachable("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
            return;
         }
      }
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      return;
   }

   if (is_empty(expression) && has_modifier(modifiers, "const")) {
      error_user("[%s:%d.%d] 'const' missing initializer", node->file, node->line, node->column);
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
               error_unreachable("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
            }
         }
         else if (!compile_expr_to_slot(expression, ctx, entry)) {
            error_unreachable("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
         }
      }
      return;
   }

   {
      char sym[256];
      EmitSink *sink;
      if (!entry_symbol_name(ctx, entry, sym, sizeof(sym))) {
         error_unreachable("[%s:%d.%d] local initializer for '%s' not compiled yet", node->file, node->line, node->column, name);
         return;
      }
      if (is_empty(expression)) {
         if (entry->is_zeropage) {
            sink = &es_zp;
         }
         else {
            char segbuf[256];
            build_named_storage_segment(segbuf, sizeof(segbuf), modifiers, "BSS");
            sink = &es_bss;
            emit(sink, ".segment \"%s\"\n", segbuf);
         }
         emit(sink, "%s:\n", sym);
         emit(sink, "\t.res %d\n", size);
         return;
      }

      {
         EmitSink init_es = EMIT_INIT;

         if (emit_global_initializer(&init_es, type, declarator, expression, size)) {
            if (modifiers_imply_named_nonzeropage(modifiers)) {
               char segbuf[256];
               sink = &es_data;
               build_named_storage_segment(segbuf, sizeof(segbuf), modifiers, "DATA");
               emit(sink, ".segment \"%s\"\n", segbuf);
            }
            else {
               sink = has_modifier(modifiers, "const") ? &es_rodata : (entry->is_zeropage ? &es_zpdata : &es_data);
            }
            emit(sink, "%s:\n", sym);
            emit_sink_append(sink, &init_es);
         }
         else {
            if (entry->is_zeropage) {
               sink = &es_zp;
            }
            else {
               char segbuf[256];
               build_named_storage_segment(segbuf, sizeof(segbuf), modifiers, "BSS");
               sink = &es_bss;
               emit(sink, ".segment \"%s\"\n", segbuf);
            }
            emit(sink, "%s:\n", sym);
            emit(sink, "\t.res %d\n", size);
            remember_pending_global_init(name, sym, type, declarator, expression, size, entry->is_zeropage, false, NULL, NULL);
         }
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
      error_unreachable("[%s:%d.%d] do/while condition not compiled yet", node->file, node->line, node->column);
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
      else if (!strcmp(stmt->name, "asm_stmt")) {
         compile_asm_stmt(stmt, ctx);
      }
      else if (!strcmp(stmt->name, "statement_list")) {
         compile_statement_list(stmt, ctx);
      }
      else if (is_empty(stmt) || !strcmp(stmt->name, "empty")) {
         /* labeled empty statement: no-op */
      }
      else {
         error_unreachable("[%s:%d.%d] labeled statement '%s' not compiled yet", stmt->file, stmt->line, stmt->column, stmt->name);
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
   int saved_locals;
   ContextEntry lhs;
   ContextEntry rhs;
   const char *cleanup_label;
   const char *default_label = NULL;
   const char *end_label = NULL;
   const char **case_labels = NULL;
   int section_count;
   bool is_signed;

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
   saved_locals = ctx ? ctx->locals : 0;
   lhs = (ContextEntry){ .name = "$lhs", .type = type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = saved_locals, .size = size };
   rhs = (ContextEntry){ .name = "$rhs", .type = type, .declarator = NULL, .is_static = false, .is_zeropage = false, .is_global = false, .offset = saved_locals + size, .size = size };
   is_signed = type_is_signed_integer(type);
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
      error_unreachable("out of memory");
   }

   remember_runtime_import("pushN");
   emit(&es_code, "    lda #$%02x\n", compare_size & 0xff);
   emit(&es_code, "    sta arg0\n");
   emit(&es_code, "    jsr _pushN\n");
   if (ctx) {
      ctx->locals = saved_locals + compare_size;
   }

   if (!compile_expr_to_slot(expr, ctx, &lhs)) {
      if (ctx) {
         ctx->locals = saved_locals;
      }
      error_unreachable("[%s:%d.%d] switch expression not compiled yet", node->file, node->line, node->column);
      remember_runtime_import("popN");
      emit(&es_code, "    lda #$%02x\n", compare_size & 0xff);
      emit(&es_code, "    sta arg0\n");
      emit(&es_code, "    jsr _popN\n");
      free(case_labels);
      free((void *) cleanup_label);
      free((void *) end_label);
      return;
   }
   if (ctx) {
      ctx->locals = saved_locals;
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

      if (!strcmp(case_expr->name, "case_choice")) {
         ASTNode *low = case_expr->count > 0 ? case_expr->children[0] : NULL;
         ASTNode *high = case_expr->count > 1 ? case_expr->children[1] : NULL;

         if (!low) {
            error_unreachable("[%s:%d.%d] malformed case label", section->file, section->line, section->column);
            continue;
         }

         if (!high) {
            if (ctx) {
               ctx->locals = saved_locals + compare_size;
            }
            if (!compile_constant_expr_to_slot(low, ctx, &rhs) &&
                !compile_expr_to_slot(low, ctx, &rhs)) {
               if (ctx) {
                  ctx->locals = saved_locals;
               }
               error_unreachable("[%s:%d.%d] case expression not compiled yet", section->file, section->line, section->column);
               continue;
            }
            if (ctx) {
               ctx->locals = saved_locals;
            }
            emit_prepare_fp_ptr(0, lhs.offset);
            emit_prepare_fp_ptr(1, rhs.offset);
            emit(&es_code, "    lda #$%02x\n", size & 0xff);
            emit(&es_code, "    sta arg0\n");
            remember_runtime_import("eqN");
            emit(&es_code, "    jsr _eqN\n");
            emit(&es_code, "    lda arg1\n");
            emit(&es_code, "    bne %s\n", case_labels[i]);
            continue;
         }

         {
            InitConstValue low_value = {0};
            InitConstValue high_value = {0};
            bool swapped = false;
            ASTNode *ordered_low = low;
            ASTNode *ordered_high = high;
            const char *skip_label = next_label("case_skip");
            const char *le_helper = is_signed ? "leNs" : "leNu";

            if (!skip_label) {
               warning("[%s:%d.%d] switch case label generation failed", section->file, section->line, section->column);
               continue;
            }

            if (eval_constant_initializer_expr(low, &low_value) &&
                eval_constant_initializer_expr(high, &high_value) &&
                low_value.kind == high_value.kind) {
               if ((low_value.kind == INIT_CONST_INT && low_value.i > high_value.i) ||
                   (low_value.kind == INIT_CONST_FLOAT && low_value.f > high_value.f)) {
                  swapped = true;
                  ordered_low = high;
                  ordered_high = low;
               }
            }

            if (swapped) {
               warning("[%s:%d.%d] case range bounds were reversed; compiling as the inclusive range in ascending order",
                       section->file, section->line, section->column);
            }

            if (ctx) {
               ctx->locals = saved_locals + compare_size;
            }
            if (!compile_constant_expr_to_slot(ordered_low, ctx, &rhs) &&
                !compile_expr_to_slot(ordered_low, ctx, &rhs)) {
               if (ctx) {
                  ctx->locals = saved_locals;
               }
               free((void *) skip_label);
               error_unreachable("[%s:%d.%d] case range start not compiled yet", section->file, section->line, section->column);
               continue;
            }
            if (ctx) {
               ctx->locals = saved_locals;
            }
            emit_prepare_fp_ptr(0, rhs.offset);
            emit_prepare_fp_ptr(1, lhs.offset);
            emit(&es_code, "    lda #$%02x\n", size & 0xff);
            emit(&es_code, "    sta arg0\n");
            remember_runtime_import(le_helper);
            emit(&es_code, "    jsr _%s\n", le_helper);
            emit(&es_code, "    lda arg1\n");
            emit(&es_code, "    beq %s\n", skip_label);

            if (ctx) {
               ctx->locals = saved_locals + compare_size;
            }
            if (!compile_constant_expr_to_slot(ordered_high, ctx, &rhs) &&
                !compile_expr_to_slot(ordered_high, ctx, &rhs)) {
               if (ctx) {
                  ctx->locals = saved_locals;
               }
               free((void *) skip_label);
               error_unreachable("[%s:%d.%d] case range end not compiled yet", section->file, section->line, section->column);
               continue;
            }
            if (ctx) {
               ctx->locals = saved_locals;
            }
            emit_prepare_fp_ptr(0, lhs.offset);
            emit_prepare_fp_ptr(1, rhs.offset);
            emit(&es_code, "    lda #$%02x\n", size & 0xff);
            emit(&es_code, "    sta arg0\n");
            remember_runtime_import(le_helper);
            emit(&es_code, "    jsr _%s\n", le_helper);
            emit(&es_code, "    lda arg1\n");
            emit(&es_code, "    bne %s\n", case_labels[i]);
            emit(&es_code, "%s:\n", skip_label);
            free((void *) skip_label);
            continue;
         }
      }

      if (ctx) {
         ctx->locals = saved_locals + compare_size;
      }
      if (!compile_expr_to_slot(case_expr, ctx, &rhs)) {
         if (ctx) {
            ctx->locals = saved_locals;
         }
         error_unreachable("[%s:%d.%d] case expression not compiled yet", section->file, section->line, section->column);
         continue;
      }
      if (ctx) {
         ctx->locals = saved_locals;
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
         if (ctx) {
            ctx->locals = saved_locals + compare_size;
         }
         compile_statement_list(body, ctx);
         if (ctx) {
            ctx->locals = saved_locals;
         }
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
      error_unreachable("[%s:%d.%d] internal missing return slot", node->file, node->line, node->column);
   }

   if (!expr || is_empty(expr)) {
      emit(&es_code, "    jmp @fini\n");
      return;
   }

   if (!compile_expr_to_return_slot(expr, ctx, ret)) {
      error_unreachable("[%s:%d.%d] return expression not compiled yet", node->file, node->line, node->column);
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
         if (!entry_symbol_name(ctx, dst, sym, sizeof(sym))) {
            error_unreachable("[%s:%d.%d] assignment target not compiled yet", node->file, node->line, node->column);
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
            ofn = lookup_operator_overload(opname, 2, arg_types, arg_decls, arg_lvalues);
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

      if (!compile_expr_to_slot(rhs, ctx, &rhs_tmp)) {
         remember_runtime_import("popN");
         emit(&es_code, "    lda #$%02x\n", tmp_total & 0xff);
         emit(&es_code, "    sta arg0\n");
         emit(&es_code, "    jsr _popN\n");
         error_unreachable("[%s:%d.%d] assignment value not compiled yet", node->file, node->line, node->column);
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
            emit_runtime_binary_fp_fp("mulN", aux_offset, lhs_tmp_offset, rhs_tmp_offset, work_size);
         }
         emit_copy_fp_to_fp(lhs_tmp_offset, aux_offset, work_size);
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
            remember_runtime_import("divN");
            emit(&es_code, "    jsr _divN\n");
            emit_copy_fp_to_fp(lhs_tmp_offset, !strcmp(op, "/=") ? quo_offset : rem_offset, work_size);
         }
      }
      else if (!strcmp(op, "<<=") || !strcmp(op, ">>=")) {
         helper = !strcmp(op, "<<=") ? "lslN" : (work_type && has_flag(type_name_from_node(work_type), "$signed") ? "asrN" : "lsrN");
         emit_runtime_shift_fp(helper, lhs_tmp_offset, aux_offset, rhs_tmp_offset, work_size);
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


static void compile_asm_stmt(ASTNode *node, Context *ctx) {
   (void) ctx;

   if (!node || is_empty(node) || node->count < 1 || !node->children[0]) {
      return;
   }

   const ASTNode *leaf = node->children[0];
   if (leaf->kind != AST_ASM || !leaf->strval) {
      warning("[%s:%d.%d] inline asm statement malformed", node->file, node->line, node->column);
      return;
   }

   emit(&es_code, "%s\n", leaf->strval);
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
      else if (!strcmp(stmt->name, "asm_stmt")) {
         compile_asm_stmt(stmt, ctx);
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
   const char *name    = declarator_name(declarator);
   const ASTNode *saved_call_graph_function = current_call_graph_function;
   int saved_call_graph_node = current_call_graph_node;
   char sym[256];

   remember_function(node, name);
   if (!function_symbol_name(node, name, sym, sizeof(sym))) {
      error_unreachable("[%s:%d.%d] could not mangle function '%s'", node->file, node->line, node->column, name);
   }

   if (!has_modifier(modifiers, "static")) {
      emit(&es_export, ".export %s\n", sym);
      emit_function_parameter_exports(node);
   }

   Context ctx;
   ctx.name = strdup(sym);
   ctx.locals = 0;
   ctx.params = 0;
   ctx.vars = new_set();
   ctx.break_label = NULL;
   ctx.continue_label = NULL;
   build_function_context(node, &ctx);
   current_call_graph_function = node;
   current_call_graph_node = call_graph_node_index_for_function(node);

   if (!is_empty(body) && !strcmp(body->name, "statement_list")) {
      predeclare_statement_list(body, &ctx);
   }

   emit_function_parameter_storage(node, &ctx);
   emit(&es_code, ".proc %s\n", sym);
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

   emit_variadic_hidden_local_setup(node, &ctx);

   if (!is_empty(body)) {
      if (!strcmp(body->name, "statement_list")) {
         compile_statement_list(body, &ctx);
      }
      else {
         error_unreachable("[%s:%d.%d] function body node '%s' not compiled yet", body->file, body->line, body->column, body->name);
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
   current_call_graph_function = saved_call_graph_function;
   current_call_graph_node = saved_call_graph_node;
}

static void compile_mem_decl_stmt(ASTNode *node) {
   attach_memname(node->children[0]->strval, node);
}

static void compile_type_decl_stmt(ASTNode *node) {
   const char *key = node->children[0]->strval;
   attach_typename(key, node);

   //debug("%s:%s", __func__, node->children[0]->strval);
   bool haveSize = false;
   int size = -1;
   bool haveEndian = false;
   const char *endian = NULL;
   bool haveFloat = false;
   const char *float_style = NULL;

   // we need to guarantee a "size" and "endian"
   if (strcmp(node->children[1]->name, "empty")) {
      for (int i = 0; i < node->children[1]->count; i++) {
         ASTNode *item = node->children[1]->children[i];

         // check for $size, must be nonnegative
         if (!strncmp(item->strval, "$size:", 6)) {
            if (haveSize) {
               error_user("[%s:%d.%d] type_decl_stmt '%s' has multiple '$size:' flags",
                     node->file, node->line, node->column,
                     node->children[0]->strval);
            }
            char *p = strchr(item->strval, ':');
            p++;
            size = atoi(p);
            if (size < 0 || (size == 0 && strcmp(p, "0"))) {
               error_user("[%s:%d.%d] type_decl_stmt '%s' unrecognized '$size:%s' flag",
                     node->file, node->line, node->column,
                     node->children[0]->strval, p);
            }
            haveSize = true;
            pair_insert(typesizes, key, (void *)(intptr_t) size);
         }

         // check for $endian, must be "big" or "little"
         if (!strncmp(item->strval, "$endian:", 8)) {
            if (haveEndian) {
               error_user("[%s:%d.%d] type_decl_stmt '%s' has multiple '$endian:' flags",
                     node->file, node->line, node->column,
                     node->children[0]->strval);
            }
            endian = strchr(item->strval, ':');
            endian++;
            if (strcmp(endian, "big") && strcmp(endian, "little")) {
               error_user("[%s:%d.%d] type_decl_stmt '%s' unrecognized '$endian:%s' flag",
                     node->file, node->line, node->column,
                     node->children[0]->strval, endian);
            }

            haveEndian = true;
         }

         if (!strcmp(item->strval, "$float")) {
            error_user("[%s:%d.%d] type_decl_stmt '%s' must use '$float:ieee754' or '$float:simple'",
                  node->file, node->line, node->column,
                  node->children[0]->strval);
         }
         else if (!strncmp(item->strval, "$float:", 7)) {
            const char *style = parse_float_style_flag_text(item->strval);
            if (haveFloat) {
               error_user("[%s:%d.%d] type_decl_stmt '%s' has multiple '$float' flags",
                     node->file, node->line, node->column,
                     node->children[0]->strval);
            }
            if (!style || !float_style_is_known(style)) {
               error_user("[%s:%d.%d] type_decl_stmt '%s' unrecognized '%s' flag",
                     node->file, node->line, node->column,
                     node->children[0]->strval, item->strval);
            }
            haveFloat = true;
            float_style = style;
         }
      }
   }

   if (!haveSize) {
      error_user("[%s:%d.%d] type_decl_stmt '%s' missing '$size:' flag",
            node->file, node->line, node->column, node->children[0]->strval);
   }

   if (!haveEndian && size > 1) {
      error_user("[%s:%d.%d] type_decl_stmt '%s' missing '$endian:' flag",
            node->file, node->line, node->column, node->children[0]->strval);
   }

   if (haveFloat) {
      int expbits = float_style_expbits_for_size(float_style, size);
      if (expbits < 0) {
         error_user("[%s:%d.%d] type_decl_stmt '%s' float style '%s' does not support $size:%d",
               node->file, node->line, node->column,
               node->children[0]->strval,
               float_style ? float_style : "(null)",
               size);
      }
   }

   if (get_xray(XRAY_TYPEINFO)) {
      message("TYPEINFO: %s %d %s", key, haveSize ? size : -1, haveEndian ? endian : "unspec");
   }
}

static bool enum_candidate_is_integer_type(const ASTNode *node) {
   const char *name;

   if (!node || strcmp(node->name, "type_decl_stmt")) {
      return false;
   }

   name = node->children[0]->strval;
   if (!name || !strcmp(name, "void") || !strcmp(name, "*") || !strcmp(name, "bool")) {
      return false;
   }

   if (has_flag_prefix(name, "$float:")) {
      return false;
   }

   return get_size(name) > 0;
}

static bool enum_candidate_can_hold_range(const ASTNode *node, long long min_value, unsigned long long max_value, bool have_negative) {
   int size;
   int bits;
   bool is_unsigned;
   bool is_signed;
   unsigned long long signed_max;
   long long signed_min;
   unsigned long long unsigned_max;

   if (!enum_candidate_is_integer_type(node)) {
      return false;
   }

   size = type_size_from_node(node);
   if (size <= 0 || size > 8) {
      return false;
   }

   bits = size * 8;
   is_unsigned = has_flag(type_name_from_node(node), "$unsigned");
   is_signed = has_flag(type_name_from_node(node), "$signed");

   if (bits >= 64) {
      signed_max = LLONG_MAX;
      signed_min = LLONG_MIN;
      unsigned_max = ULLONG_MAX;
   }
   else {
      signed_max = (1ULL << (bits - 1)) - 1ULL;
      signed_min = -(long long) (1ULL << (bits - 1));
      unsigned_max = (1ULL << bits) - 1ULL;
   }

   if (is_unsigned) {
      return !have_negative && max_value <= unsigned_max;
   }

   if (have_negative) {
      return min_value >= signed_min && max_value <= signed_max;
   }

   if (is_signed) {
      return max_value <= signed_max;
   }

   return max_value <= unsigned_max;
}

static const ASTNode *find_best_enum_backing_type(ASTNode *node) {
   long long min_value = 0;
   unsigned long long max_value = 0;
   bool have_range = false;
   bool have_negative = false;
   const ASTNode *best = NULL;
   int best_size = INT_MAX;

   if (!node || node->count < 2 || !node->children[1]) {
      error_unreachable("[%s:%d.%d] invalid enum declaration", node ? node->file : __FILE__, node ? node->line : __LINE__, node ? node->column : 0);
   }

   for (int i = 0; i < node->children[1]->count; i++) {
      ASTNode *entry = node->children[1]->children[i];
      long long value;
      unsigned long long uvalue;
      if (!entry || entry->count < 2 || !entry->children[1] || entry->children[1]->kind != AST_INTEGER) {
         error_user("[%s:%d.%d] enum value '%s' is not an integer constant", entry ? entry->file : node->file, entry ? entry->line : node->line, entry ? entry->column : node->column, (entry && entry->count > 0 && entry->children[0]) ? entry->children[0]->strval : "?");
      }
      value = parse_int(entry->children[1]->strval);
      uvalue = value < 0 ? 0ULL : (unsigned long long) value;
      if (!have_range) {
         min_value = value;
         max_value = uvalue;
         have_range = true;
      }
      else {
         if (value < min_value) {
            min_value = value;
         }
         if (uvalue > max_value) {
            max_value = uvalue;
         }
      }
      if (value < 0) {
         have_negative = true;
      }
   }

   if (!have_range) {
      error_user("[%s:%d.%d] enum '%s' is empty", node->file, node->line, node->column, node->children[0]->strval);
   }

   for (int i = 0; root && i < root->count; i++) {
      ASTNode *cand = root->children[i];
      int cand_size;
      if (!enum_candidate_can_hold_range(cand, min_value, max_value, have_negative)) {
         continue;
      }
      cand_size = type_size_from_node(cand);
      if (!best || cand_size < best_size) {
         best = cand;
         best_size = cand_size;
      }
   }

   if (!best) {
      error_user("[%s:%d.%d] enum '%s' has no declared integer type that can represent values %lld..%llu",
            node->file, node->line, node->column,
            node->children[0]->strval,
            min_value, max_value);
   }

   return best;
}

static void compile_enum_decl_stmt(ASTNode *node) {
   const char *key = node->children[0]->strval;
   const ASTNode *backing = find_best_enum_backing_type(node);
   const char *backing_name = type_name_from_node(backing);
   int size = type_size_from_node(backing);

   attach_typename(key, node);
   pair_insert(typesizes, key, (void *)(intptr_t) size);
   pair_insert(enumbackings, key, (void *) backing_name);

   if (get_xray(XRAY_TYPEINFO)) {
      message("TYPEINFO: enum %s %d %s", key, size, backing_name ? backing_name : "?");
   }
}

static void compile_struct_decl_stmt(ASTNode *node) {
   const char *key = node->children[0]->strval;
   attach_typename(key, node);

   debug("%s:%d %s >>", __FILE__, __LINE__,  __func__);
   debug("========================================\n");
   parse_dump_node(node);
   debug("========================================\n");
}

static void compile_union_decl_stmt(ASTNode *node) {
   const char *key = node->children[0]->strval;
   attach_typename(key, node);

   debug("%s:%d %s >>", __FILE__, __LINE__,  __func__);
   debug("========================================\n");
   parse_dump_node(node);
   debug("========================================\n");
}

static bool declarator_is_function(const ASTNode *declarator) {
   return declarator && declarator_has_parameter_list(declarator) && !declarator_nested(declarator);
}

static int declarator_array_multiplier(const ASTNode *declarator) {
   if (!declarator || declarator_is_function(declarator)) {
      return 1;
   }

   return declarator_array_multiplier_from(declarator, declarator_suffix_start_index(declarator));
}

static int declarator_storage_size(const ASTNode *type, const ASTNode *declarator) {
   int size;

   if (declarator_pointer_depth(declarator) > 0 || declarator_function_pointer_depth(declarator) > 0) {
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
   ASTNode *declarator = (ASTNode *) decl_node_declarator(node);
   const ASTNode *addrspec = decl_node_address_spec(node);
   const char *name    = declarator_name(declarator);
   ASTNode *expression = node->children[node->count - 1];
   validate_nonreserved_variadic_name(name, node);
   ASTNode *uexpr;
   EmitSink init_es = EMIT_INIT;

   if (!globals) {
      globals = new_set();
   }

   const ASTNode *value = set_get(globals, name);
   if (value != NULL) {
      error_user("[%s:%d.%d] duplicate symbol '%s' first defined at [%s:%d.%d]",
            node->file, node->line, node->column,
            name,
            value->file, value->line, value->column);
   }
   set_add(globals, strdup(name), node);

   bool is_extern = has_modifier(modifiers, "extern");
   bool is_const = has_modifier(modifiers, "const");
   bool is_static = has_modifier(modifiers, "static");
   bool is_zeropage = modifiers_imply_zeropage(modifiers);
   bool is_ref = has_modifier(modifiers, "ref");
   bool is_absolute_ref = is_ref && addrspec != NULL;
   int size = declarator_storage_size(type, declarator);
   char symname[256];
   format_user_asm_symbol(name, symname, sizeof(symname));

   if (addrspec != NULL && !is_ref) {
      warn_address_spec_without_ref(node, name);
   }

   if (is_ref && !is_absolute_ref) {
      error_user("[%s:%d.%d] 'ref' not allowed in global declaration without an absolute address binding",
            node->file, node->line, node->column);
   }

   if (is_absolute_ref) {
      if (!address_spec_has_read(addrspec) && !address_spec_has_write(addrspec)) {
         error_user("[%s:%d.%d] absolute ref '%s' cannot use none for both read and write address",
               node->file, node->line, node->column, name);
      }
      if (is_extern) {
         error_user("[%s:%d.%d] 'extern' not allowed on absolute ref '%s'",
               node->file, node->line, node->column, name);
      }
      if (!is_empty(expression)) {
         ASTNode *runtime_expr = (ASTNode *) unwrap_expr_node(expression);
         if (!address_spec_has_write(addrspec)) {
            error_user("[%s:%d.%d] global absolute ref '%s' with initializer must be writable",
                  node->file, node->line, node->column, name);
         }
         remember_pending_global_init(name,
                                      NULL,
                                      type,
                                      declarator,
                                      runtime_expr ? runtime_expr : expression,
                                      size,
                                      false,
                                      true,
                                      address_spec_read_expr(addrspec),
                                      address_spec_write_expr(addrspec));
      }
      return;
   }

   if (is_extern) {
      if (is_static) {
         error_user("[%s:%d.%d] 'extern' and 'static' don't mix",
               node->file, node->line, node->column);
      }

      if (is_zeropage) {
         emit(&es_import, ".zpimport %s\n", symname);
      }
      else {
         emit(&es_import, ".import %s\n", symname);
      }
      return;
   }

   if (!is_static) {
      if (is_zeropage) {
         emit(&es_export, ".zpexport %s\n", symname);
      }
      else {
         emit(&es_export, ".export %s\n", symname);
      }
   }

   if (is_empty(expression)) {
      if (is_const) {
         error_user("[%s:%d.%d] 'const' missing initializer",
               node->file, node->line, node->column);
      }
      if (is_zeropage) {
         emit(&es_zp, "%s:\n", symname);
         emit(&es_zp, "\t.res %d\n", size);
      }
      else {
         char segbuf[256];
         build_named_storage_segment(segbuf, sizeof(segbuf), modifiers, "BSS");
         emit(&es_bss, ".segment \"%s\"\n", segbuf);
         emit(&es_bss, "%s:\n", symname);
         emit(&es_bss, "\t.res %d\n", size);
      }
      return;
   }

   uexpr = (ASTNode *) unwrap_expr_node(expression);

   {
      char symbuf[256];
      snprintf(symbuf, sizeof(symbuf), "%s", symname);

      if (emit_global_initializer(&init_es, type, declarator, uexpr ? uexpr : expression, size)) {
         if (modifiers_imply_named_nonzeropage(modifiers)) {
            char segbuf[256];
            build_named_storage_segment(segbuf, sizeof(segbuf), modifiers, "DATA");
            emit(&es_data, ".segment \"%s\"\n", segbuf);
            emit(&es_data, "%s:\n", symname);
            emit_sink_append(&es_data, &init_es);
         }
         else {
            EmitSink *es = is_const ? &es_rodata : (is_zeropage ? &es_zpdata : &es_data);
            emit(es, "%s:\n", symname);
            emit_sink_append(es, &init_es);
         }
         return;
      }

      if (is_zeropage) {
         emit(&es_zp, "%s:\n", symname);
         emit(&es_zp, "\t.res %d\n", size);
      }
      else {
         char segbuf[256];
         build_named_storage_segment(segbuf, sizeof(segbuf), modifiers, "BSS");
         emit(&es_bss, ".segment \"%s\"\n", segbuf);
         emit(&es_bss, "%s:\n", symname);
         emit(&es_bss, "\t.res %d\n", size);
      }
      remember_pending_global_init(name, symbuf, type, declarator, uexpr ? uexpr : expression, size, is_zeropage, false, NULL, NULL);
   }
}


static void remember_function(const ASTNode *node, const char *name) {
   bool name_present = false;

   validate_function_nonreserved_variadic_names(node);

   if (!name) {
      error_user("[%s:%d.%d] unnamed function declaration is not supported here", node->file, node->line, node->column);
   }

   if (is_operator_function_name(name)) {
      remember_operator_overload(node, name);
      return;
   }

   if (!functions) {
      functions = new_set();
   }

   for (int i = 0; i < ordinary_function_count; i++) {
      const ASTNode *value;

      if (strcmp(ordinary_functions[i].name, name)) {
         continue;
      }
      name_present = true;
      value = ordinary_functions[i].node;
      if (value == node) {
         return;
      }
      if (function_same_signature(value, node)) {
         if (!function_same_declaration(value, node)) {
            error_user("[%s:%d.%d] vs [%s:%d.%d] conflicting declarations for overloaded '%s'",
                  node->file, node->line, node->column,
                  value->file, value->line, value->column,
                  name);
         }
         if (function_has_body(value) && function_has_body(node)) {
            error_user("[%s:%d.%d] vs [%s:%d.%d] multiple definitions for '%s'",
                  node->file, node->line, node->column,
                  value->file, value->line, value->column,
                  name);
         }
         if (!function_has_body(value) && function_has_body(node)) {
            ordinary_functions[i].node = node;
            if (set_get(functions, name) == value) {
               set_rm(functions, name);
               set_add(functions, strdup(name), (void *) node);
            }
         }
         return;
      }
   }

   ordinary_functions = (OrdinaryFunction *) realloc(ordinary_functions,
         sizeof(*ordinary_functions) * (ordinary_function_count + 1));
   if (!ordinary_functions) {
      error_unreachable("out of memory");
   }
   ordinary_functions[ordinary_function_count].name = strdup(name);
   ordinary_functions[ordinary_function_count].node = node;
   ordinary_function_count++;

   if (!name_present && !set_get(functions, name)) {
      set_add(functions, strdup(name), (void *) node);
   }
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
               remember_function(item, declarator_name(declarator));
            }
         }
      }
      else if (node->count == 3) {
         ASTNode *declarator = node->children[1];
         remember_function(node, declarator_name(declarator));
      }
   }
}

static void compile_function_signature(ASTNode *node) {
   ASTNode *modifiers  = node->children[0];
   ASTNode *declarator = node->children[2];
   const char *name    = declarator_name(declarator);
   char sym[256];

   remember_function(node, name);

   if (has_modifier(modifiers, "extern") && !has_modifier(modifiers, "static")) {
      if (!function_symbol_name(node, name, sym, sizeof(sym))) {
         error_unreachable("[%s:%d.%d] could not mangle function '%s'", node->file, node->line, node->column, name);
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

   error_unreachable("[%s:%d.%d] unsupported defdecl_stmt shape", node->file, node->line, node->column);
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
         error_user("undefined struct/union '%s' [%s:%d.%d]",
               undefined, node->file, node->line, node->column);
      }
      else {
         error_unreachable("undefined struct/union '%s'", undefined); // this is probably unreachable
      }
      // error_user() calls exit()
   }
}

static bool crosscheck_helper(Pair *markers, const char *name) {
   const char *childname;
   ASTNode *child;
   pair_insert(markers, name, (void *)1);
   ASTNode *node = get_typename_node(name);
   if (node && (!strcmp(node->name, "struct_decl_stmt") || !strcmp(node->name, "union_decl_stmt"))) {
      for (int i = 1; i < node->count; i++) {
         child = node->children[i];
         {
            const ASTNode *child_decl = child->children[2];
            if (declarator_pointer_depth(child_decl) <= 0 && declarator_function_pointer_depth(child_decl) <= 0) {
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
               error_user("cyclic struct/union detected");
               // error_user() calls exit()
            }
         }
      }
   }

   pair_destroy(markers);
}

static void calculate_struct_union_sizes(ASTNode *program) {
   // everybody uses pointers, let's just do that now...

   if (!typename_exists("*")) {
      error_unreachable("type * is not defined, pointer size is unknown");
      // error_user() calls exit()
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
            int bit_cursor = 0;

            if (!pair_exists(typesizes, name)) {
               for (int j = 1; j < node->count; j++) {
                  ASTNode *item = node->children[j];
                  const ASTNode *type = item->children[1];
                  const char *tname = type->strval;
                  const ASTNode *decl = item->children[2];
                  int mult = declarator_array_multiplier(decl);
                  bool isptr = declarator_pointer_depth(decl) > 0 || declarator_function_pointer_depth(decl) > 0;
                  int bit_width = declarator_bitfield_width(decl);
                  int othersize;

                  if (isptr) {
                     othersize = sizeof_ptr;
                  }
                  else if (pair_exists(typesizes, tname)) {
                     othersize = (intptr_t) pair_get(typesizes, tname);
                  }
                  else {
                     othersize = -1;
                  }

                  if (othersize == -1) {
                     size = -1;
                     break;
                  }

                  if (bit_width > 0) {
                     if (declarator_pointer_depth(decl) > 0 || declarator_function_pointer_depth(decl) > 0 || declarator_array_count(decl) > 0) {
                        error_user("[%s:%d.%d] bitfield '%s' must be a plain scalar field",
                              decl->file, decl->line, decl->column,
                              declarator_name(decl) ? declarator_name(decl) : "<unnamed>");
                     }
                     if (has_flag_prefix(tname, "$float:")) {
                        error_user("[%s:%d.%d] bitfield '%s' cannot use floating type '%s'",
                              decl->file, decl->line, decl->column,
                              declarator_name(decl) ? declarator_name(decl) : "<unnamed>",
                              tname);
                     }
                     if (has_flag(tname, "$endian:big")) {
                        error_user("[%s:%d.%d] bitfield '%s' does not support big-endian type '%s'",
                              decl->file, decl->line, decl->column,
                              declarator_name(decl) ? declarator_name(decl) : "<unnamed>",
                              tname);
                     }
                     if (bit_width <= 0 || bit_width > othersize * 8) {
                        error_user("[%s:%d.%d] bitfield '%s' width %d exceeds storage of '%s' (%d bits)",
                              decl->file, decl->line, decl->column,
                              declarator_name(decl) ? declarator_name(decl) : "<unnamed>",
                              bit_width, tname, othersize * 8);
                     }
                     if (mult != 1) {
                        error_user("[%s:%d.%d] bitfield '%s' cannot be an array",
                              decl->file, decl->line, decl->column,
                              declarator_name(decl) ? declarator_name(decl) : "<unnamed>");
                     }
                     if (is_struct) {
                        bit_cursor += bit_width;
                        size = (bit_cursor + 7) / 8;
                     }
                     else {
                        int field_size = (bit_width + 7) / 8;
                        if (field_size > size) {
                           size = field_size;
                        }
                     }
                  }
                  else if (is_struct) {
                     if (bit_cursor % 8) {
                        bit_cursor = ((bit_cursor + 7) / 8) * 8;
                     }
                     bit_cursor += othersize * mult * 8;
                     size = bit_cursor / 8;
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
                  debug("sizeof(%s) == %d", name, size);
               }
            }
         }
      }
   }
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
