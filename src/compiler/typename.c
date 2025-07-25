#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lextern.h"
#include "messages.h"
#include "xray.h"

#define MAX_TYPES 1024
static char *type_names[MAX_TYPES];
static int type_count = 0;

int find_typename(const char* name) {
   for (int i = 0; i < type_count; i++) {
      if (strcmp(type_names[i], name) == 0) return i;
   }
   return -1;
}

int register_typename(const char* name) {
   // allows duplicates, errors come on the second pass
   if (find_typename(name) == -1) {
      if (type_count < MAX_TYPES) {
         type_names[type_count++] = strdup(name);
      }
      else {
         yyerror("type table full %s:%d.%d",
            current_filename, yylineno, yycolumn);
         return -1;
      }
   }
   return 0;
}

