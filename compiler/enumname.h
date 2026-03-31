#ifndef _INCLUDE_ENUMNAME_H_
#define _INCLUDE_ENUMNAME_H_

#include <stdbool.h>

#include "ast.h"

// determine if enumname exists
bool enumname_exists(const char* name);

// register a new enumname
int register_enumnames(ASTNode *node);

// attach an ASTNode to a registered enumname
void attach_enumname(const char *name, ASTNode *node);

// get the node attached to a enumname
ASTNode *get_enumname_node(const char *name);

// check for any null valued typenames
const char *enumname_find_null(void);

#endif
