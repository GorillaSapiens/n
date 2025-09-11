#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

// returns copy of string without underscores, caller must free()
char *strip_underscores(const char *p) {
   if (!p) {
      return NULL;
   }

   char *ret = (char *) malloc (strlen(p) + 1);
   char *q = ret;
   
   while (*p) {
      if (*p != '_') {
         *q++ = *p++;
      }
      else {
         p++;
      }
   }

   return ret;
}
