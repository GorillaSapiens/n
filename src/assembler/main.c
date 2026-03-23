#include <stdio.h>
#include "ir.h"
#include "asm_pass.h"

int yyparse(void);

extern program_ir_t g_program;

int main(void)
{
   asm_context_t ctx;
   int rc;

   program_ir_init(&g_program);

   rc = yyparse();
   if (rc != 0) {
      program_ir_free(&g_program);
      return rc;
   }

   asm_context_init(&ctx, &g_program);

   if (asm_pass1(&ctx) != 0) {
      asm_context_free(&ctx);
      program_ir_free(&g_program);
      return 1;
   }

   if (asm_pass2(&ctx) != 0) {
      asm_context_free(&ctx);
      program_ir_free(&g_program);
      return 1;
   }

   asm_context_free(&ctx);
   program_ir_free(&g_program);
   return 0;
}
