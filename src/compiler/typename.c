#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "lextern.h"
#include "memname.h"
#include "messages.h"
#include "pair.h"
#include "typename.h"
#include "xform.h"
#include "xray.h"

static Pair *types = NULL;

bool typename_exists(const char* name) {
   if (!types) {
      types = pair_create();
   }

   return pair_exists(types, name);
}

int register_typename(const char* name) {
   if (!types) {
      types = pair_create();
   }

   if (xform_exists(name)) {
      yyerror ("typename cannot be the same as existing xform %s:%d.%d",
         current_filename, yylineno, yycolumn);
      return -1;
   }

   if (memname_exists(name)) {
      yyerror ("typename cannot be the same as existing memname %s:%d.%d",
         current_filename, yylineno, yycolumn);
      return -1;
   }

   if (typename_exists(name)) {
      yyerror ("typename already exists %s:%d.%d",
         current_filename, yylineno, yycolumn);
      return -1;
   }

   pair_insert(types, name, NULL);
   return 0;
}

void attach_typename(const char *name, ASTNode *node) {
   if (!types) {
      types = pair_create();
   }

   pair_insert(types, name, node);
}

ASTNode *get_typename_node(const char *name) {
   if (!types) {
      types = pair_create();
   }

   return pair_get(types, name);
}
