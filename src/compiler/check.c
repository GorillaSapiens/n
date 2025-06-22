#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "ast.h"
#include "check.h"
#include "messages.h"

// check type_decl for existence of $size and $endian
static void check_type_decl(ASTNode *tree) {

   if (!tree) {
      return;
   }

   if (!strcmp(tree->name, "type_decl")) {
      debug("%s:%s", __FUNCTION__, tree->children[0]->strval);
      bool haveSize = false;
      int size = -1;
      bool haveEndian = false;
      // we need to guarantee a "size" and "endian"
      if (strcmp(tree->children[1]->name, "empty")) {
         for (ASTNode *list = tree->children[1];
              list != NULL;
              list = list->children[1]) {
            debug("%s:\t%s", __FUNCTION__, list->children[0]->strval);

            // check for $size, must be nonnegative
            if (!strncmp(list->children[0]->strval, "$size:", 6)) {
               if (haveSize) {
                  error("[%s:%d.%d] type_decl '%s' has multiple '$size:' flags",
                     tree->file, tree->line, tree->column,
                     tree->children[0]->strval);
               }
               char *p = strchr(list->children[0]->strval, ':');
               p++;
               size = atoi(p);
               if (size < 0 || (size == 0 && strcmp(p, "0"))) {
                  error("[%s:%d.%d] type_decl '%s' unrecognized '$size:%s' flag",
                     tree->file, tree->line, tree->column,
                     tree->children[0]->strval, p);
               }
               haveSize = true;
            }

            // check for $endian, must be "big" or "little"
            if (!strncmp(list->children[0]->strval, "$endian:", 8)) {
               if (haveEndian) {
                  error("[%s:%d.%d] type_decl '%s' has multiple '$endian:' flags",
                     tree->file, tree->line, tree->column,
                     tree->children[0]->strval);
               }
               char *p = strchr(list->children[0]->strval, ':');
               p++;
               if (strcmp(p, "big") && strcmp(p, "little")) {
                  error("[%s:%d.%d] type_decl '%s' unrecognized '$endian:%s' flag",
                     tree->file, tree->line, tree->column,
                     tree->children[0]->strval, p);
               }

               haveEndian = true;
            }

         }
      }
      if (!haveSize) {
         error("[%s:%d.%d] type_decl '%s' missing '$size:' flag",
            tree->file, tree->line, tree->column, tree->children[0]->strval);
      }
      if (!haveEndian && size != 0) {
         error("[%s:%d.%d] type_decl '%s' missing '$endian:' flag",
            tree->file, tree->line, tree->column, tree->children[0]->strval);
      }
   }

   for (int i = 0; i < tree->count; i++) {
      check_type_decl(tree->children[i]);
   }
}

void do_checks(void) {
   check_type_decl(root);
}
