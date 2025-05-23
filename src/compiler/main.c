#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Declare from parser.y
extern int yyparse(void);
extern void parse_dump(void);

extern char *current_filename;

// Optional: reference to input file
extern FILE* yyin;
extern int yydebug;

int main(int argc, char** argv) {
   int ret;

   // Optional: read from file
   if (argc > 1) {
      yyin = fopen(argv[1], "r");
      if (!yyin) {
         perror(argv[1]);
         return 1;
      }
   }
   current_filename = argv[1];

#ifdef YYDEBUG
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
   }

   return 0;
}
