#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "lextern.h"
#include "messages.h"
#include "pair.h"
#include "xform.h"

static Pair *xforms = NULL;

int register_xform(const char *name, ASTNode *node) {
   if (!xforms) {
      xforms = pair_create();
   }

   if (pair_exists(xforms, name)) {
      error ("duplicate xform '%s' %s:%d.%d",
         name, node->file, node->line, node->column);
      return -1;
   }

   pair_insert(xforms, name, node);
   return 0;
}

bool xform_exists(const char *name) {
   if (!xforms) {
      xforms = pair_create();
   }

   return pair_exists(xforms, name);
}

const char *do_xform(const char *s, const char *name) {
   if (!xforms) {
      xforms = pair_create();
   }

   if (!pair_exists(xforms, name)) {
      yyerror ("duplicate xform '%s' %s:%d.%d",
         name, current_filename, yylineno, yycolumn);
      return NULL;
   }

   return s;
}

