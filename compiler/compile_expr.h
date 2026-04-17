#ifndef _INCLUDE_COMPILE_EXPR_H_
#define _INCLUDE_COMPILE_EXPR_H_

#include "ast.h"
#include "compile_internal.h"

bool compile_constant_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst);
bool compile_expr_to_slot(ASTNode *expr, Context *ctx, ContextEntry *dst);
bool compile_condition_branch_false(ASTNode *expr, Context *ctx, const char *false_label);
void compile_expr(ASTNode *node, Context *ctx);

#endif
