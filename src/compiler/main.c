#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "ast.h"
#include "compile.h"
#include "coverage.h"
#include "expropt.h"
#include "lextern.h"
#include "md5seen.h"
#include "messages.h"
#include "xray.h"

#include "parser.tab.h"

static char *arg0;

static void opt_help(char *);

static void opt_xray(char *n) {
   int i = lookup_xray(n);
   set_xray(i);
}

static char **inclist = NULL;
static int inclist_cnt = 0;
static void opt_include(char *n) {
   inclist = (char **) realloc(inclist, sizeof(char *) * (inclist_cnt + 1));
   inclist[inclist_cnt++] = strdup(n);
}

const char *search_includes(const char *filename) {
   static char *ret = NULL;

   if (access(filename, F_OK) == 0) {
      return filename;
   }

   // hunt through list
   for (int i = 0; i < inclist_cnt; i++) {
      int l = strlen(filename) + 1 + strlen(inclist[i]) + 1;
      ret = (char *) realloc(ret, sizeof(char) * l);
      sprintf(ret, "%s/%s", inclist[i], filename);

      if (access(ret, F_OK) == 0) {
         return ret;
      }
   }

   // an error...
   return filename;
}

struct {
   char  short_char;
   char *long_name;
   char *arg_name;
   char *help;
   void (*func)(char *);
} options[] = {
   { 'X', "XRAY", "name",  "enable named XRAY option for compiler debugging ('list' will list them)", opt_xray },
   { 'i', "include", "path",  "add path to include search list", opt_include },
   { '?', "help", NULL, "print usage information", opt_help }
};

#define NOPTS (sizeof(options) / sizeof(options[0]))

static void opt_help(char *) {
   printf("Usage: %s <flags> <filename>\n", arg0);
   printf("   flags:\n");
   for (int i = 0; i < NOPTS; i++) {
      if (options[i].arg_name) {
         printf("   -%c/--%s\t<%s>\t%s\n",
            options[i].short_char,
            options[i].long_name,
            options[i].arg_name,
            options[i].help);
      }
      else {
         printf("   -%c/--%s\t\t%s\n",
            options[i].short_char,
            options[i].long_name,
            options[i].help);
      }
   }
   exit(0);
}

#define return "DON'T USE return, MUST USE exit !!!" // please don't break xray !!!
int main(int argc, char** argv) {
   int ret;

   arg0 = argv[0];

   argc--;
   argv++;

   while (argc) {
      if (argv[0][0] == '-') {
         bool matched = false;

         for (int i = 0; i < NOPTS; i++) {
            if ((argv[0][1] == '-' && !strcmp(options[i].long_name, argv[0] + 2)) ||
                (argv[0][1] == options[i].short_char)) {
               char *arg = NULL;
               matched = true;
               argc--;
               argv++;

               if (options[i].arg_name) {
                  if (argc == 0) {
                     opt_help(NULL);
                  }
                  else {
                     arg = argv[0];
                     argc--;
                     argv++;
                  }
               }

               options[i].func(arg);
               break;
            }
         }

         if (!matched) {
            opt_help(NULL);
         }
      }
      else {
         break;
      }
   }

   if (argc != 1) {
      opt_help(NULL);
   }

   yyin = fopen(argv[0], "r");
   if (!yyin) {
      perror(argv[0]);
      exit(-1);
   }
   md5seen(argv[0], yyin);

   root_filename = current_filename = argv[0];

#if YYDEBUG
   yydebug = 1;
#endif

   // TODO register built in type names

   debug(";Parsing...\n");

   ret = yyparse();

   if (get_xray(XRAY_DUMPAST)) {
      parse_dump_node(root);
   }

   if (ret == 0) {
      debug(";Parse successful.\n");
      if (get_xray(XRAY_PARSEONLY)) {
         exit(0);
      }
   } else {
      error(";Parse failed.\n");
      exit(-1);
   }

   if (get_xray(XRAY_COVERAGE)) {
      coverage_report();
      exit(0);
   }

   do_expropt();
   if (get_xray(XRAY_EXPROPTONLY)) {
      exit(0);
   }

   do_compile();

   exit(0);
}
