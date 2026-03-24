#ifndef ASM_PASS_H
#define ASM_PASS_H

#include <stdio.h>
#include "ir.h"
#include "symtab.h"
#include "ihex.h"
#include "listing.h"

typedef struct import_name {
   char *name;
   const char *file;
   int line;
   struct import_name *next;
} import_name_t;

typedef struct asm_segment {
   char *name;
   long base;
   long size;
   long pc;
   long used_size;
   int defined;
   int overflow_warned;
   struct asm_segment *next;
} asm_segment_t;

typedef struct asm_context {
   program_ir_t *prog;
   symtab_t symbols;
   long origin;
   ihex_image_t image;
   listing_writer_t *listing;
   int error_count;
   int object_mode_o65;
   import_name_t *imports;
   asm_segment_t *segments;
} asm_context_t;

void asm_context_init(asm_context_t *ctx, program_ir_t *prog, listing_writer_t *listing, int object_mode_o65);
void asm_context_free(asm_context_t *ctx);

int asm_relax(asm_context_t *ctx);
int asm_pass1(asm_context_t *ctx, int pass_index);
int asm_pass2(asm_context_t *ctx);

int asm_write_map_file(FILE *fp, const asm_context_t *ctx);

#endif
