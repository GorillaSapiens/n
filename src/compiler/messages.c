#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>

#include "messages.h"

void debug(const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   fprintf(stderr, "debug: ");
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
   va_end(args);
}

void error(const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   fprintf(stderr, "error: ");
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
   va_end(args);
   exit(-1);
}

void warning(const char *fmt, ...) {
   va_list args;
   va_start(args, fmt);
   fprintf(stderr, "warning: ");
   vfprintf(stderr, fmt, args);
   fprintf(stderr, "\n");
   va_end(args);
   exit(-1);
}
