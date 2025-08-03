#ifndef _INCLUDE_XFORM_H_
#define _INCLUDE_XFORM_H_

#include "ast.h"

int register_xform(ASTNode *xform);

const char *do_xform(const char *s, const char *xform);

#endif
