#ifndef _INCLUDE_COMPILE_DECL_H_
#define _INCLUDE_COMPILE_DECL_H_

#include "ast.h"

void compile_mem_decl_stmt(ASTNode *node);
void compile_type_decl_stmt(ASTNode *node);
void compile_enum_decl_stmt(ASTNode *node);
void compile_struct_decl_stmt(ASTNode *node);
void compile_union_decl_stmt(ASTNode *node);
void compile_global_decl_item(ASTNode *node);
void predeclare_top_level_functions(ASTNode *program);
void compile_defdecl_stmt(ASTNode *node);
void check_struct_union_undefined(ASTNode *program);
void crosscheck_struct_union_nesting(ASTNode *program);
void calculate_struct_union_sizes(ASTNode *program);

#endif
