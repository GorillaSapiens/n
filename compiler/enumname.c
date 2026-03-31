#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "lextern.h"
#include "memname.h"
#include "messages.h"
#include "pair.h"
#include "enumname.h"
#include "xform.h"
#include "xray.h"

static Pair *enums = NULL;

bool enumname_exists(const char* name) {
   if (!enums) {
      enums = pair_create();
   }

   return pair_exists(enums, name);
}

int register_enumnames(ASTNode *ast) {
   if (!enums) {
      enums = pair_create();
   }

   // register all the enum names for this enum type.
   for (int i = 0; ast->children[1]->children[i]; i++) {
      const char *name = ast->children[1]->children[i]->children[0]->strval;

      if (xform_exists(name)) {
         ASTNode *previous = get_xform_node(name);
         yyerror ("enumname at %s:%d.%d cannot be the same as existing xform at %s:%d.%d",
               current_filename, yylineno, yycolumn,
               previous->file, previous->line, previous->column);
         return -1;
      }

      if (memname_exists(name)) {
         ASTNode *previous = get_memname_node(name);
         yyerror ("enumname at %s:%d.%d cannot be the same as existing memname at %s:%d.%d",
               current_filename, yylineno, yycolumn,
               previous->file, previous->line, previous->column);
         return -1;
      }

      if (enumname_exists(name)) {
         ASTNode *previous = get_enumname_node(name);
         yyerror ("enumname at %s:%d.%d already exists at %s:%d.%d",
               current_filename, yylineno, yycolumn,
               previous->file, previous->line, previous->column);
         return -1;
      }

      pair_insert(enums, name, ast->children[1]->children[i]);
   }

   return 0;
}

ASTNode *get_enumname_node(const char *name) {
   if (!enums) {
      enums = pair_create();
   }

   return pair_get(enums, name);
}

const char *enumname_find_null(void) {
   if (!enums) {
      enums = pair_create();
   }

   return pair_null_value(enums);
}
