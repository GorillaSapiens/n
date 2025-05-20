#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mprintf.h"

// returns malloc'd pointer that must be free'd
char *mprintf(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   int size = 1 + vsnprintf(NULL, 0, fmt, ap);
   va_end(ap);

   char *ret = (char *) malloc(sizeof(char) * size);

   va_start(ap, fmt);
   vsnprintf(ret, size, fmt, ap);
   va_end(ap);

   return ret;
}

