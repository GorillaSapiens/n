#ifndef _INCLUDE_COMPILE_INTERNAL_H_
#define _INCLUDE_COMPILE_INTERNAL_H_

#include <stdbool.h>
#include "ast.h"
#include "pair.h"

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

typedef struct Context Context;
typedef struct LValueRef LValueRef;

extern Pair *typesizes;
extern Pair *enumbackings;

const ASTNode *expr_value_type(ASTNode *expr, Context *ctx);
bool eval_constant_initializer_expr(ASTNode *expr, InitConstValue *out);
const ASTNode *expr_value_declarator(ASTNode *expr, Context *ctx);
bool string_literal_is_char_constant(const char *text);
bool resolve_ref_argument_lvalue(Context *ctx, ASTNode *expr, LValueRef *out);
bool classify_incdec_lvalue_expr(ASTNode *expr, bool *inc, bool *pre);
const char *expr_bare_identifier_name(ASTNode *expr);
void validate_nonreserved_variadic_name(const char *name, const ASTNode *node);
void validate_function_nonreserved_variadic_names(const ASTNode *fn);
void calculate_struct_union_sizes(ASTNode *program);

#endif
