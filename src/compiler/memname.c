#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typename.h"
#include "lextern.h"
#include "memname.h"
#include "messages.h"
#include "xray.h"

#define MAX_MEMS 32
static char *mem_names[MAX_MEMS];
static int mem_count = 0;

int find_memname(const char* name) {
   for (int i = 0; i < mem_count; i++) {
      if (strcmp(mem_names[i], name) == 0) return i;
   }
   return -1;
}

int register_memname(const char* name) {
   if (find_typename(name) != -1) {
      yyerror ("memname cannot be the same as existing typename %s:%d.%d",
         current_filename, yylineno, yycolumn);
      return -1;
   }

   if (find_memname(name) != -1) {
      yyerror ("memname already exists %s:%d.%d",
         current_filename, yylineno, yycolumn);
      return -1;
   }

   if (mem_count < MAX_MEMS) {
      mem_names[mem_count++] = strdup(name);
      return 0;
   }
   else {
      yyerror("mem table full %s:%d.%d",
         current_filename, yylineno, yycolumn);
      return -1;
   }
}
