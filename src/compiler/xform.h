#ifndef _INCLUDE_XFORM_H_
#define _INCLUDE_XFORM_H_

#include <stdbool.h>

#include "ast.h"

int register_xform(const char *name, ASTNode *node);

bool xform_exists(const char *name);

const char *do_xform(const char *s, const char *name);

#endif
