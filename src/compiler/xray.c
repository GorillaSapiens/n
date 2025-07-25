#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "messages.h"
#define NO_XRAY_OVERRIDE_EXIT
#include "xray.h"

#define MAX_XRAY 512 // suitably large

static unsigned int xrays[MAX_XRAY / (8 * sizeof(unsigned int))] = { 0 };

static struct {
   int number;         // MUST BE IN RANGE [0..MAX_XRAY]
   const char *name;
   const char *description;
} name2number[] = {
   { XRAY_INVERT,    "invert",    "invert success/failure exit value" },
   { XRAY_COVERAGE,  "coverage",  "yacc/bison rule coverage testing" },
   { XRAY_PARSEONLY, "parseonly", "exit after parsing" },
   { XRAY_DEBUG,     "debug",     "print debug() messages" },
};

void xray_exit(int n, const char *file, int line) {
   if (get_xray(0)) {
      n = ~n;
      debug("xray:inverting exit value at %s:%d", file, line);
   }
   exit(n);
}

int lookup_xray(const char *name) {
   if (!strcmp(name, "list")) {
      // special code to list defined xrays
      printf("%19s   %s\n", "name", "description");
      for (int i = 0; i < sizeof(name2number) / sizeof(name2number[0]); i++) {
         printf("(%3d)%14s   %s\n",
            name2number[i].number,
            name2number[i].name,
            name2number[i].description);
      }
      xray_exit(0, __FILE__, __LINE__);
   }

   for (int i = 0; i < sizeof(name2number) / sizeof(name2number[0]); i++) {
      if (!strcmp(name2number[i].name, name)) {
         return name2number[i].number;
      }
   }
   return -1;
}

void set_xray(int n) {
   if (n < 0 || n >= MAX_XRAY) {
      error("xray %d out of bounds", n);
   }
   xrays[n / (8 * sizeof(unsigned int))] |= 1 << (n % (8 * sizeof(unsigned int)));
}

int get_xray(int n) {
   if (n < 0 || n >= MAX_XRAY) {
      error("xray %d out of bounds", n);
   }
   if (xrays[n / (8 * sizeof(unsigned int))] & (1 << (n % (8 * sizeof(unsigned int))))) {
      return 1;
   }
   return 0;
}
