#ifndef _INCLUDE_COMPILE_INTERNAL_H_
#define _INCLUDE_COMPILE_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include "ast.h"
#include "pair.h"
#include "set.h"

#include "emit.h"

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

typedef struct Context {
   const char *name;
   int locals;
   int params;
   Set *vars;
   const char *break_label;
   const char *continue_label;
} Context;

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

extern EmitSink es_code;
extern EmitSink es_export;
extern Pair *typesizes;
extern Pair *enumbackings;

const ASTNode *cast_expr_target_type(const ASTNode *expr);
const ASTNode *cast_expr_target_declarator(const ASTNode *expr);
bool decode_char_constant_value(const char *text, long long *value_out);
const char *remember_string_literal(const char *text);
bool pointer_initializer_uses_backing_object(const ASTNode *type, const ASTNode *declarator, const ASTNode *expr);
const char *emit_pointer_initializer_backing_object(const ASTNode *type, const ASTNode *declarator, const ASTNode *expr);
bool function_has_static_parameters(const ASTNode *fn);
bool entry_symbol_name(Context *ctx, const ContextEntry *entry, char *buf, size_t bufsize);
bool resolve_lvalue(Context *ctx, ASTNode *node, LValueRef *out);
void emit_fill_fp_bytes(int dst_offset, int start, int count, unsigned char value);
bool emit_copy_fp_to_lvalue(Context *ctx, const LValueRef *dst, int src_offset, int size);
void emit_copy_fp_to_symbol(const char *symbol, int src_offset, int size);
bool emit_string_initializer_to_fp(const ASTNode *type, const ASTNode *declarator, int base_offset, int total_size, const char *text);
bool emit_string_initializer_bytes(unsigned char *buf, int buf_size, int base_offset, const ASTNode *type, const ASTNode *declarator, int total_size, const char *text);
const ASTNode *expr_value_type(ASTNode *expr, Context *ctx);
bool eval_constant_initializer_expr(ASTNode *expr, InitConstValue *out);
const ASTNode *expr_value_declarator(ASTNode *expr, Context *ctx);
bool string_literal_is_char_constant(const char *text);
bool resolve_ref_argument_lvalue(Context *ctx, ASTNode *expr, LValueRef *out);
bool classify_incdec_lvalue_expr(ASTNode *expr, bool *inc, bool *pre);
const char *expr_bare_identifier_name(ASTNode *expr);
void validate_nonreserved_variadic_name(const char *name, const ASTNode *node);
void validate_function_nonreserved_variadic_names(const ASTNode *fn);
void remember_runtime_import(const char *name);
void emit_store_immediate_to_fp(int dst_offset, const unsigned char *bytes, int size);
bool compile_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst);
bool find_aggregate_member(const ASTNode *type, const char *member, const ASTNode **member_type, const ASTNode **member_declarator, int *member_offset);
void calculate_struct_union_sizes(ASTNode *program);

#endif
