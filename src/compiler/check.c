#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "check.h"
#include "messages.h"

void check_type_decl(ASTNode *tree) {

   if (!tree) {
      return;
   }

   if (!strcmp(tree->name, "type_decl")) {
      debug("%s:%s", __FUNCTION__, tree->children[0]->strval);
      bool haveSize = false;
      // we need to guarantee a "size"
      if (strcmp(tree->children[1]->name, "empty")) {
         for (ASTNode *list = tree->children[1];
              list != NULL;
              list = list->children[1]) {
            debug("%s:\t%s", __FUNCTION__, list->children[0]->strval);
            if (!strncmp(list->children[0]->strval, "$size:", 6)) {
               if (haveSize) {
                  error("[%s:%d.%d] type_decl '%s' has multiple '$size:' flags",
                     tree->file, tree->line, tree->column,
                     tree->children[0]->strval);
               }
               haveSize = true;
            }
         }
      }
      if (!haveSize) {
         error("[%s:%d.%d] type_decl '%s' missing '$size:' flag",
            tree->file, tree->line, tree->column, tree->children[0]->strval);
      }
   }

   for (int i = 0; i < tree->count; i++) {
      check_type_decl(tree->children[i]);
   }
}
