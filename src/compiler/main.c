#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "compile.h"
#include "lextern.h"
#include "md5seen.h"
#include "messages.h"

#include "parser.tab.h"

void usage(const char *exename) {
   fprintf(stderr, "usage: %s <filename>\n", exename);
   exit(0);
}

int main(int argc, char** argv) {
   int ret;

   // Optional: read from file
   if (argc > 1) {
      yyin = fopen(argv[1], "r");
      if (!yyin) {
         perror(argv[1]);
         return 1;
      }
      md5seen(argv[1], yyin);
   }
   else {
      usage(argv[0]);
   }

   current_filename = argv[1];

#if YYDEBUG
   yydebug = 1;
#endif

   // TODO register built in type names

   printf("Parsing...\n");
   ret = yyparse();
   if (ret == 0) {
      parse_dump();
      printf("Parse successful.\n");
   } else {
      printf("Parse failed.\n");
      return -1;
   }

   do_compile();

   return 0;
}
