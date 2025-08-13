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
      ASTNode *previous = get_xform_node(name);
      yyerror ("typename at %s:%d.%d cannot be the same as existing xform at %s:%d.%d",
         current_filename, yylineno, yycolumn,
         previous->file, previous->line, previous->column);
      return -1;
   }

   if (memname_exists(name)) {
      ASTNode *previous = get_memname_node(name);
      yyerror ("typename at %s:%d.%d cannot be the same as existing memname at %s:%d.%d",
         current_filename, yylineno, yycolumn,
         previous->file, previous->line, previous->column);
      return -1;
   }

   if (typename_exists(name)) {
      ASTNode *previous = get_typename_node(name);
      yyerror ("typename at %s:%d.%d already exists at %s:%d.%d",
         current_filename, yylineno, yycolumn,
         previous->file, previous->line, previous->column);
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
