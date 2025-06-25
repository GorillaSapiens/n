#ifndef _INCLUDE_EMIT_H_
#define _INCLUDE_EMIT_H_

struct EmitPiece;
typedef struct EmitPiece {
   const char *txt;
   struct EmitPiece *next;
} EmitPiece;

typedef struct EmitSink {
   EmitPiece *head;
   EmitPiece *tail;
} EmitSink;

#define EMIT_INIT { NULL, NULL }

void emit(EmitSink *es, const char *fmt, ...);
void emit_print(EmitSink *es);

#endif
