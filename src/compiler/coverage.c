#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static unsigned char coverage_map[] = {
#include "coverage_map.h"
};

static unsigned char visited[sizeof(coverage_map)] = { 0 };

static bool init = false;

void cover_exit(void) {
   int missing = 0;
   bool begin = false;
   for (int i = 0; i < sizeof(coverage_map); i++) {
      if (visited[i] != coverage_map[i]) {
         for (int j = 0; j < 8; j++) {
            unsigned char k = 1 << j;
            if ((coverage_map[i] & k) != (visited[i] & k)) {
               int line = i * 8 + j;
               if (!begin) {
                  printf("COVERAGE MISSING");
                  begin = true;
               }
               printf(" %d", line);
               missing++;
            }
         }
      }
   }
   if (missing) {
      printf("\nCOVERAGE MISSING COUNT %d\n", missing);
   }
}

void cover(int n) {
   if (!init) {
      atexit(cover_exit);
      init = true;
   }
   visited[n / 8] |= 1 << (n % 8);
}
