#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "lextern.h"
#include "messages.h"
#include "pair.h"
#include "xform.h"

static Pair *xforms;

int register_xform(const char *name, ASTNode *node) {
   if (pair_exists(xforms, name)) {
      yyerror ("duplicate xform '%s' %s:%d.%d",
         name, current_filename, yylineno, yycolumn);
      return -1;
   }

   pair_insert(xforms, name, node);
   return 0;
}

bool xform_exists(const char *name) {
   return pair_exists(xforms, name);
}

const char *do_xform(const char *s, const char *name) {
   return s;
}

