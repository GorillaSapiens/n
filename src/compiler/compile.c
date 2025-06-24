#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "ast.h"
#include "compile.h"
#include "messages.h"
#include "set.h"

Set *types = NULL;

// check type_decl for existence of $size and $endian
static void compile_type_decl(ASTNode *node) {

   if (!types) {
      types = new_set();
   }

//   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
//   parse_dump_node(node);

   const char *key = node->children[0]->strval;
   set_add(types, key, node);

   //debug("%s:%s", __FUNCTION__, node->children[0]->strval);
   bool haveSize = false;
   int size = -1;
   bool haveEndian = false;
   // we need to guarantee a "size" and "endian"
   if (strcmp(node->children[1]->name, "empty")) {
      for (ASTNode *list = node->children[1];
            list != NULL;
            list = list->children[1]) {
         //debug("%s:\t%s", __FUNCTION__, list->children[0]->strval);

         // check for $size, must be nonnegative
         if (!strncmp(list->children[0]->strval, "$size:", 6)) {
            if (haveSize) {
               error("[%s:%d.%d] type_decl '%s' has multiple '$size:' flags",
                     node->file, node->line, node->column,
                     node->children[0]->strval);
            }
            char *p = strchr(list->children[0]->strval, ':');
            p++;
            size = atoi(p);
            if (size < 0 || (size == 0 && strcmp(p, "0"))) {
               error("[%s:%d.%d] type_decl '%s' unrecognized '$size:%s' flag",
                     node->file, node->line, node->column,
                     node->children[0]->strval, p);
            }
            haveSize = true;
         }

         // check for $endian, must be "big" or "little"
         if (!strncmp(list->children[0]->strval, "$endian:", 8)) {
            if (haveEndian) {
               error("[%s:%d.%d] type_decl '%s' has multiple '$endian:' flags",
                     node->file, node->line, node->column,
                     node->children[0]->strval);
            }
            char *p = strchr(list->children[0]->strval, ':');
            p++;
            if (strcmp(p, "big") && strcmp(p, "little")) {
               error("[%s:%d.%d] type_decl '%s' unrecognized '$endian:%s' flag",
                     node->file, node->line, node->column,
                     node->children[0]->strval, p);
            }

            haveEndian = true;
         }
      }
   }
   if (!haveSize) {
      error("[%s:%d.%d] type_decl '%s' missing '$size:' flag",
            node->file, node->line, node->column, node->children[0]->strval);
   }
   if (!haveEndian && size > 1) {
      error("[%s:%d.%d] type_decl '%s' missing '$endian:' flag",
            node->file, node->line, node->column, node->children[0]->strval);
   }
}

void compile_decl_stmt(ASTNode *node) {
   debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   parse_dump_node(node);

   const char *type      = node->children[0]->strval;
   const char *name      = node->children[1]->strval;
   const char *dimension = node->children[2]->strval;
   const char *location  = node->children[3]->strval;
   ASTNode *expression   = node->children[4];

   printf("=%s %s %s %s %p\n", type, name, dimension, location, expression);

   return;
}

void compile_function_decl(ASTNode *node) {
   //debug("%s:%d %s >>", __FILE__, __LINE__,  __FUNCTION__);
   //parse_dump_node(node);
   return;
}

void compile(ASTNode *node) {
   if (!node) {
      return;
   }

   if (!strcmp(node->name, "program")) {
      compile(node->children[0]);
      compile(node->children[1]);
   }
   else if (!strcmp(node->name, "type_decl")) {
      compile_type_decl(node);
   }
   else if (!strcmp(node->name, "decl_stmt")) {
      compile_decl_stmt(node);
   }
   else if (!strcmp(node->name, "static_decl_stmt")) {
      compile_decl_stmt(node);
   }
   else if (!strcmp(node->name, "const_decl_stmt")) {
      compile_decl_stmt(node);
   }
   else if (!strcmp(node->name, "function_decl")) {
      compile_function_decl(node);
   }
   else {
      error("[%s:%d.%d] unrecognized AST node '%s'",
         node->file, node->line, node->column,
         node->name);
   }
}

void do_compile(void) {
   compile(root);
}
