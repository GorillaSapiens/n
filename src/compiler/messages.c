#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "lextern.h"
#include "messages.h"
#include "xray.h"

void message(const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
   va_end(args);
}

void debug(const char *fmt, ...) {
   if (get_xray(XRAY_DEBUG)) {
      va_list args;
      va_start(args, fmt);
      fprintf(stderr, "Debug: ");
      vfprintf(stderr, fmt, args);
      fprintf(stderr, "\n");
      va_end(args);
   }
}

void error(const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   fprintf(stderr, "Error: ");
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
   va_end(args);
   exit(-1);
}

void warning(const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   fprintf(stderr, "Warning: ");
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
   va_end(args);
}

static bool isident(const char *p) {
   return (*p >= 'A' && *p <= 'Z') ||
          (*p >= 'a' && *p <= 'z') ||
          (*p >= '0' && *p <= '9') ||
          (*p == '_');
}

static void replace_in_place(char *s, const char *l, const char *r) {
    size_t len_l = strlen(l);
    size_t len_r = strlen(r);
    char *pos;

    while ((pos = strstr(s, l)) != NULL && (pos == s || !isident(pos-1)) && !isident(pos + len_l)) {
        size_t tail_len = strlen(pos + len_l);
        if (len_r > len_l) {
            // Shift right to make room
            memmove(pos + len_r, pos + len_l, tail_len + 1); // +1 to copy null terminator
        } else if (len_r < len_l) {
            // Shift left to close the gap
            memmove(pos + len_r, pos + len_l, tail_len + 1);
        }
        memcpy(pos, r, len_r);
        s = pos + len_r; // continue after the replacement
    }
}

static const char *replacements[] = {
#include "delexer.h"
};

static void do_replacements(char *s) {
   int i;
   for (i = 0; i < sizeof(replacements) / sizeof(replacements[0]); i += 2) {
      replace_in_place(s, replacements[i], replacements[i+1]);
   }
}

static void yymessage(const char *preamble, const char *fmt, va_list args) {
   va_list args_copy;
   va_copy(args_copy, args);

   int size = 1 + vsnprintf(NULL, 0, fmt, args);
   size *= 2; // allow space for expansion
   char *str = (char *) malloc(sizeof(char) * size);
   vsnprintf(str, size, fmt, args_copy);

   do_replacements(str);

   fprintf(stderr, "%s at %s:%d.%d (near '%s')\n",
      preamble, current_filename, yylineno, yycolumn, yytext);
   fprintf(stderr, "   %s\n", str);
   free(str);
}

void yyerror(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   yymessage("Error", fmt, ap);
   va_end(ap);
   exit(-1);
}

void yywarn(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   yymessage("Warning", fmt, ap);
   va_end(ap);
}
