#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "enumname.h"
#include "lextern.h"
#include "memname.h"
#include "messages.h"
#include "pair.h"
#include "typename.h"
#include "xform.h"
#include "xray.h"

static Pair *mems = NULL;

bool memname_exists(const char* name) {
   if (!mems) {
      mems = pair_create();
   }

   return pair_exists(mems, name);
}

int register_memname(const char* name) {
   if (!mems) {
      mems = pair_create();
   }

   if (xform_exists(name)) {
      ASTNode *previous = get_xform_node(name);
      yyerror ("memname at %s:%d.%d cannot be the same as existing xform %s:%d.%d",
         current_filename, yylineno, yycolumn,
         previous->file, previous->line, previous->column);
      return -1;
   }

   if (typename_exists(name)) {
      ASTNode *previous = get_typename_node(name);
      yyerror ("memname at %s:%d.%d cannot be the same as existing typename %s:%d.%d",
         current_filename, yylineno, yycolumn,
         previous->file, previous->line, previous->column);
      return -1;
   }

   if (enumname_exists(name)) {
      ASTNode *previous = get_enumname_node(name);
      yyerror ("memname at %s:%d.%d cannot be the same as existing enum name %s:%d.%d",
         current_filename, yylineno, yycolumn,
         previous->file, previous->line, previous->column);
      return -1;
   }

   if (memname_exists(name)) {
      ASTNode *previous = get_memname_node(name);
      yyerror ("memname at %s:%d.%d already exists at %s:%d.%d",
         current_filename, yylineno, yycolumn,
         previous->file, previous->line, previous->column);
      return -1;
   }

   pair_insert(mems, name, NULL);
   return 0;
}

void attach_memname(const char *name, ASTNode *node) {
   if (!mems) {
      mems = pair_create();
   }

   pair_insert(mems, name, node);
}

ASTNode *get_memname_node(const char *name) {
   if (!mems) {
      mems = pair_create();
   }

   return pair_get(mems, name);
}
