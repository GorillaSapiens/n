#ifndef _INCLUDE_EMIT_H_
#define _INCLUDE_EMIT_H_

#include <stdio.h>

// piece created by calling emit()
struct EmitPiece;
typedef struct EmitPiece {
   const char *txt;
   struct EmitPiece *next;
} EmitPiece;

// linked list of pieces
typedef struct EmitSink {
   EmitPiece *head;
   EmitPiece *tail;
} EmitSink;

// static initializer
#define EMIT_INIT { NULL, NULL }

// add text to an EmitSink
void emit(EmitSink *es, const char *fmt, ...);

// run peephole optimization over compiler-emitted assembly in an EmitSink
void emit_peephole_optimize(EmitSink *es);

// print the text stored in an EmitSink
void emit_print(EmitSink *es, FILE *out);

#endif
