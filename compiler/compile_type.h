#ifndef _INCLUDE_COMPILE_TYPE_H_
#define _INCLUDE_COMPILE_TYPE_H_

#include <stdbool.h>
#include <stddef.h>
#include "ast.h"
#include "compile_internal.h"

const ASTNode *type_name_node(const ASTNode *type);
const char *type_name_from_node(const ASTNode *type);
const ASTNode *required_typename_node(const char *name);
const ASTNode *bool_type_node(void);
bool type_is_bool(const ASTNode *type);
const char *parse_integer_style_flag_text(const char *text);
bool type_has_integer_style(const ASTNode *type, const char *style);
bool type_is_signed_integer(const ASTNode *type);
bool type_is_unsigned_integer(const ASTNode *type);
bool type_is_promotable_integer(const ASTNode *type);
bool type_has_exactops(const ASTNode *type);
bool same_named_value_type(const ASTNode *lhs_type, const ASTNode *lhs_decl,
                           const ASTNode *rhs_type, const ASTNode *rhs_decl);
const ASTNode *expr_same_type_exactops_type(ASTNode *expr, Context *ctx);
bool mixed_exactops_value_types(const ASTNode *lhs_type, const ASTNode *lhs_decl,
                                const ASTNode *rhs_type, const ASTNode *rhs_decl,
                                const ASTNode **exact_type_out, const ASTNode **other_type_out);
bool expr_mixed_exactops_type(ASTNode *expr, Context *ctx,
                              const ASTNode **exact_type_out,
                              const ASTNode **other_type_out);
void require_no_mixed_exactops_operator_expr(ASTNode *expr, Context *ctx);
void require_exactops_operator_expr(ASTNode *expr, Context *ctx);
void require_exactops_truthiness_expr(ASTNode *expr, Context *ctx);
const char *type_endian_name(const ASTNode *type);
bool type_is_big_endian(const ASTNode *type);
int endian_mem_index_for_significance(int size, bool big_endian, int significance_index);
const ASTNode *promoted_integer_type_for_binary(const ASTNode *lhs_type, const ASTNode *rhs_type, ASTNode *origin);
bool expr_is_literal_node(const ASTNode *expr);
bool ordinary_integer_endian_conflict(const ASTNode *lhs_type, const ASTNode *rhs_type);
const ASTNode *binary_integer_work_type(ASTNode *lhs_expr, ASTNode *rhs_expr, Context *ctx, ASTNode *origin);
const ASTNode *compound_integer_work_type(const ASTNode *lhs_type, const ASTNode *lhs_decl, ASTNode *rhs_expr, Context *ctx, ASTNode *origin);
void require_no_mixed_signed_integer_binary_expr(ASTNode *expr, Context *ctx);
void require_no_mixed_endian_integer_binary_expr(ASTNode *expr, Context *ctx);
void require_no_mixed_endian_pointer_index_expr(ASTNode *origin, ASTNode *idx_expr, Context *ctx, const char *op);
const ASTNode *select_endian_variant_type(const ASTNode *src_type, const char *target_endian);
const ASTNode *flag_cast_target_type(ASTNode *expr, Context *ctx);
const ASTNode *flag_cast_target_declarator(ASTNode *expr, Context *ctx);
int flag_cast_target_size(ASTNode *expr, Context *ctx);
const ASTNode *literal_annotation_type(const ASTNode *expr);
const char *find_mem_modifier_name(const ASTNode *modifiers);
const ASTNode *find_mem_modifier_node(const ASTNode *modifiers);
bool mem_decl_is_zeropage(const ASTNode *mem_decl);
bool modifiers_imply_zeropage(const ASTNode *modifiers);
bool modifiers_imply_mem_storage(const ASTNode *modifiers);
bool modifiers_imply_named_nonzeropage(const ASTNode *modifiers);
void build_named_storage_segment(char *buf, size_t bufsize, const ASTNode *modifiers, const char *base_segment);
int integer_literal_min_size(const ASTNode *expr);
bool has_flag(const char *type, const char *flag);
bool has_flag_prefix(const char *type, const char *prefix);
const char *enum_backing_type_name(const char *type);
const char *parse_float_style_flag_text(const char *text);
bool type_is_float_like(const ASTNode *type);
const char *type_float_style(const ASTNode *type);
int type_float_expbits(const ASTNode *type);
bool has_modifier(ASTNode *node, const char *modifier);
bool declaration_const_applies_to_object(const ASTNode *modifiers, const ASTNode *declarator);
const char *missing_argname(int i);
ASTNode *make_named_pointer_declarator(const char *name);
const ASTNode *parameter_decl_specifiers(const ASTNode *parameter);
const ASTNode *parameter_type(const ASTNode *parameter);
const ASTNode *parameter_declarator(const ASTNode *parameter);
bool parameter_is_ref(const ASTNode *parameter);
bool parameter_has_symbol_storage(const ASTNode *parameter);
int parameter_storage_size(const ASTNode *parameter);
const char *parameter_name(const ASTNode *parameter, int i);
bool parameter_is_void(const ASTNode *parameter);
const ASTNode *unwrap_expr_node(const ASTNode *expr);
int get_size(const char *type);
const ASTNode *declarator_value_declarator(const ASTNode *declarator);
const ASTNode *declarator_name_node(const ASTNode *declarator);
const char *declarator_name(const ASTNode *declarator);
const ASTNode *declarator_bitfield_node(const ASTNode *declarator);
int declarator_bitfield_width(const ASTNode *declarator);
int declarator_suffix_start_index(const ASTNode *declarator);
const ASTNode *declarator_parameter_list(const ASTNode *declarator);
bool declarator_has_parameter_list(const ASTNode *declarator);
int declarator_pointer_depth(const ASTNode *declarator);
int declarator_pointer_node_count(const ASTNode *declarator);
int declarator_function_pointer_depth(const ASTNode *declarator);
int declarator_array_multiplier_from(const ASTNode *declarator, int start_child);
int declarator_array_count(const ASTNode *declarator);
int declarator_first_element_size(const ASTNode *type, const ASTNode *declarator);
const ASTNode *clone_declarator_variant(const ASTNode *declarator, int new_ptr_depth, int first_array_child);
const ASTNode *call_adjusted_parameter_declarator(const ASTNode *declarator, bool is_ref);
void expr_match_signature(ASTNode *expr, Context *ctx, const ASTNode **type_out, const ASTNode **decl_out);
const ASTNode *function_pointer_declarator_from_callable(const ASTNode *declarator);
const ASTNode *function_return_declarator_from_callable(const ASTNode *declarator);
const ASTNode *declarator_after_subscript(const ASTNode *declarator);
const ASTNode *declarator_after_deref(const ASTNode *declarator);
const ASTNode *function_return_type(const ASTNode *fn);
const ASTNode *function_declarator_node(const ASTNode *fn);
int type_size_from_node(const ASTNode *type);
int declarator_value_size(const ASTNode *type, const ASTNode *declarator);
int expr_value_size(ASTNode *expr, Context *ctx);
bool expr_is_integer_constant_expr(const ASTNode *expr, long long *value_out);
bool expr_is_untyped_integer_literal(const ASTNode *expr);
bool integer_literal_is_zero_expr(const ASTNode *expr);
bool integer_literal_fits_plain_integer_type(const ASTNode *expr, const ASTNode *formal_type, const ASTNode *formal_decl);
bool declarator_is_plain_value(const ASTNode *declarator);
bool declarator_array_signature_matches_from(const ASTNode *actual, const ASTNode *formal, int start_child);
bool declarator_signature_matches(const ASTNode *actual, const ASTNode *formal);
bool declarator_is_function(const ASTNode *declarator);
int declarator_array_multiplier(const ASTNode *declarator);
int declarator_storage_size(const ASTNode *type, const ASTNode *declarator);

#endif
