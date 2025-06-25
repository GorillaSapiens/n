#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "emit.h"

void emit(EmitSink *es, const char *fmt, ...) {
   int len;
   va_list args;

   va_start(args, fmt);
   len = vsnprintf(NULL, 0, fmt, args);
   va_end(args);

   EmitPiece *piece = (EmitPiece *) malloc (sizeof(EmitPiece));
   piece->txt = (char *) malloc(len + 1);

   va_start(args, fmt);
   vsprintf((char *) piece->txt, fmt, args);
   va_end(args);

   piece->next = NULL;
   if (es->head == NULL) {
      es->head = es->tail = piece;
   }
   else {
      es->tail->next = piece;
      es->tail = piece;
   }
}

void emit_print(EmitSink *es) {
   for (EmitPiece *ep = es->head; ep; ep = ep->next) {
      printf("%s", ep->txt);
   }
}
