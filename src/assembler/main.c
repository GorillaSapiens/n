#include <stdio.h>
#include <stdlib.h>
#include "ir.h"
#include "asm_pass.h"
#include "ihex.h"
#include "listing.h"

int yyparse(void);
extern FILE *yyin;
extern program_ir_t g_program;

static void usage(const char *argv0)
{
   fprintf(stderr, "usage: %s input.s output.hex output.lst\n", argv0);
}

int main(int argc, char **argv)
{
   asm_context_t ctx;
   listing_writer_t lst;
   FILE *hexfp;
   int rc;

   if (argc != 4) {
      usage(argv[0]);
      return 1;
   }

   yyin = fopen(argv[1], "r");
   if (!yyin) {
      perror(argv[1]);
      return 1;
   }

   hexfp = fopen(argv[2], "w");
   if (!hexfp) {
      perror(argv[2]);
      fclose(yyin);
      return 1;
   }

   if (!listing_open(&lst, argv[3])) {
      perror(argv[3]);
      fclose(hexfp);
      fclose(yyin);
      return 1;
   }

   program_ir_init(&g_program);

   rc = yyparse();
   fclose(yyin);

   if (rc != 0) {
      listing_close(&lst);
      fclose(hexfp);
      program_ir_free(&g_program);
      return rc;
   }

   asm_context_init(&ctx, &g_program, &lst);

   if (asm_pass1(&ctx) != 0) {
      asm_context_free(&ctx);
      listing_close(&lst);
      fclose(hexfp);
      program_ir_free(&g_program);
      return 1;
   }

   if (asm_pass2(&ctx) != 0) {
      asm_context_free(&ctx);
      listing_close(&lst);
      fclose(hexfp);
      program_ir_free(&g_program);
      return 1;
   }

   if (!ihex_dump(hexfp, &ctx.image)) {
      asm_context_free(&ctx);
      listing_close(&lst);
      fclose(hexfp);
      program_ir_free(&g_program);
      return 1;
   }

   asm_context_free(&ctx);
   listing_close(&lst);
   fclose(hexfp);
   program_ir_free(&g_program);
   return 0;
}
