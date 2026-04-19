#ifndef _INCLUDE_ABI_META_H_
#define _INCLUDE_ABI_META_H_

#include <stdbool.h>
#include "ast.h"

#define ABI_META_PREFIX "__abimeta$V1$"

void emit_function_abi_metadata(const ASTNode *fn, const char *sym, bool is_definition);
void emit_global_abi_metadata(const ASTNode *node, const char *symname, bool is_definition, bool is_zeropage);

#endif
