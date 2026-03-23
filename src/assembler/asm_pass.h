#ifndef ASM_PASS_H
#define ASM_PASS_H

#include "ir.h"
#include "symtab.h"

typedef struct asm_context {
   program_ir_t *prog;
   symtab_t symbols;
   long origin;
} asm_context_t;

void asm_context_init(asm_context_t *ctx, program_ir_t *prog);
void asm_context_free(asm_context_t *ctx);

int asm_pass1(asm_context_t *ctx);
int asm_pass2(asm_context_t *ctx);

#endif
