#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"

char *xstrdup(const char *s)
{
   size_t n;
   char *p;

   if (!s)
      return NULL;

   n = strlen(s) + 1;
   p = (char *)malloc(n);
   if (!p) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   memcpy(p, s, n);
   return p;
}
