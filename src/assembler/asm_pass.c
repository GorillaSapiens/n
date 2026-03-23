#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm_pass.h"

static int is_branch_opcode(const char *opcode)
{
   return !strcmp(opcode, "BCC") ||
          !strcmp(opcode, "BCS") ||
          !strcmp(opcode, "BEQ") ||
          !strcmp(opcode, "BMI") ||
          !strcmp(opcode, "BNE") ||
          !strcmp(opcode, "BPL") ||
          !strcmp(opcode, "BVC") ||
          !strcmp(opcode, "BVS");
}

static addr_mode_t normalize_mode(const char *opcode, addr_mode_t mode)
{
   if (is_branch_opcode(opcode) && mode == AM_ZP_OR_ABS)
      return AM_RELATIVE;

   return mode;
}

static int opcode_accepts_mode(const char *opcode, addr_mode_t mode)
{
   /* keep this minimal for now; parser already did some checking */
   (void)opcode;
   (void)mode;
   return 1;
}

static int directive_size_pass1(const directive_info_t *dir, long *new_origin)
{
   const expr_list_node_t *node;
   long value;
   expr_eval_status_t rc;
   int count;

   if (!strcmp(dir->name, ".org")) {
      if (!dir->exprs || dir->exprs->next) {
         fprintf(stderr, ".org expects exactly one expression\n");
         return -1;
      }

      rc = expr_eval(dir->exprs->expr, NULL, 0, &value);
      if (rc != EXPR_EVAL_OK) {
         fprintf(stderr, ".org must be absolute in pass 1\n");
         return -1;
      }

      *new_origin = value;
      return 0;
   }

   if (!strcmp(dir->name, ".byte")) {
      count = 0;
      for (node = dir->exprs; node; node = node->next)
         count++;
      return count;
   }

   if (!strcmp(dir->name, ".word")) {
      count = 0;
      for (node = dir->exprs; node; node = node->next)
         count++;
      return count * 2;
   }

   if (!strcmp(dir->name, ".text") || !strcmp(dir->name, ".ascii")) {
      if (!dir->string)
         return 0;
      return (int)(strlen(dir->string) - 2);
   }

   return 0;
}

static int insn_size_pass1(const insn_info_t *insn)
{
   addr_mode_t mode;

   mode = normalize_mode(insn->opcode, insn->mode);

   switch (mode) {
      case AM_IMPLIED:
      case AM_ACCUMULATOR:
         return 1;

      case AM_IMMEDIATE:
      case AM_INDEXED_INDIRECT:
      case AM_INDIRECT_INDEXED:
      case AM_RELATIVE:
         return 2;

      case AM_INDIRECT:
         return 3;

      case AM_ZP_OR_ABS:
      case AM_ZPX_OR_ABSX:
      case AM_ZPY_OR_ABSY:
         /*
            Conservative in pass 1: assume worst case.
            Pass 2 can shrink zp later if you want, but that implies
            either relaxation or a policy choice. For now, keep it stable.
         */
         return 3;

      default:
         return 0;
   }
}

void asm_context_init(asm_context_t *ctx, program_ir_t *prog)
{
   ctx->prog = prog;
   ctx->origin = 0;
   symtab_init(&ctx->symbols);
}

void asm_context_free(asm_context_t *ctx)
{
   symtab_free(&ctx->symbols);
}

int asm_pass1(asm_context_t *ctx)
{
   stmt_t *stmt;
   long pc;
   long new_origin;
   int sz;

   pc = ctx->origin;

   for (stmt = ctx->prog->head; stmt; stmt = stmt->next) {
      if (stmt->label)
         symtab_define(&ctx->symbols, stmt->label, pc);

      switch (stmt->kind) {
         case STMT_LABEL:
            break;

         case STMT_INSN:
            sz = insn_size_pass1(&stmt->u.insn);
            if (sz < 0)
               return 1;
            pc += sz;
            break;

         case STMT_DIR:
            new_origin = pc;
            sz = directive_size_pass1(stmt->u.dir, &new_origin);
            if (sz < 0)
               return 1;

            if (!strcmp(stmt->u.dir->name, ".org"))
               pc = new_origin;
            else
               pc += sz;
            break;
      }
   }

   return 0;
}

static void emit_byte(unsigned char b)
{
   printf("%02X ", b);
}

static void emit_word(unsigned short w)
{
   emit_byte((unsigned char)(w & 0xFF));
   emit_byte((unsigned char)((w >> 8) & 0xFF));
}

static int eval_or_report(const expr_t *expr, const symtab_t *symbols, long pc, long *value, int line)
{
   expr_eval_status_t rc;

   rc = expr_eval(expr, symbols, pc, value);
   if (rc == EXPR_EVAL_OK)
      return 0;

   fprintf(stderr, "line %d: ", line);
   expr_print(expr);
   fprintf(stderr, " -> ");

   if (rc == EXPR_EVAL_DIVZERO)
      fprintf(stderr, "divide by zero\n");
   else
      fprintf(stderr, "unresolved expression\n");

   return 1;
}

static int directive_emit_pass2(const directive_info_t *dir, const symtab_t *symbols, long *pc, int line)
{
   const expr_list_node_t *node;
   long value;

   if (!strcmp(dir->name, ".org")) {
      if (!dir->exprs || dir->exprs->next) {
         fprintf(stderr, "line %d: .org expects exactly one expression\n", line);
         return 1;
      }

      if (eval_or_report(dir->exprs->expr, symbols, *pc, &value, line))
         return 1;

      *pc = value;
      return 0;
   }

   if (!strcmp(dir->name, ".byte")) {
      for (node = dir->exprs; node; node = node->next) {
         if (eval_or_report(node->expr, symbols, *pc, &value, line))
            return 1;
         emit_byte((unsigned char)(value & 0xFF));
         (*pc)++;
      }
      return 0;
   }

   if (!strcmp(dir->name, ".word")) {
      for (node = dir->exprs; node; node = node->next) {
         if (eval_or_report(node->expr, symbols, *pc, &value, line))
            return 1;
         emit_word((unsigned short)(value & 0xFFFF));
         (*pc) += 2;
      }
      return 0;
   }

   if (!strcmp(dir->name, ".text") || !strcmp(dir->name, ".ascii")) {
      const char *s;
      size_t i, n;

      if (!dir->string)
         return 0;

      s = dir->string;
      n = strlen(s);
      if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
         for (i = 1; i + 1 < n; i++) {
            emit_byte((unsigned char)s[i]);
            (*pc)++;
         }
      }
      return 0;
   }

   printf("; line %d: unhandled directive %s\n", line, dir->name);
   return 0;
}

static int insn_emit_pass2(const insn_info_t *insn, const symtab_t *symbols, long *pc, int line)
{
   long value;
   addr_mode_t mode;

   mode = normalize_mode(insn->opcode, insn->mode);

   if (!opcode_accepts_mode(insn->opcode, mode)) {
      fprintf(stderr, "line %d: illegal addressing mode\n", line);
      return 1;
   }

   emit_byte(0xEA);
   (*pc)++;

   switch (mode) {
      case AM_IMPLIED:
      case AM_ACCUMULATOR:
         return 0;

      default:
         break;
   }

   if (!insn->has_operand) {
      fprintf(stderr, "line %d: internal error: missing operand\n", line);
      return 1;
   }

   if (eval_or_report(insn->expr, symbols, *pc, &value, line))
      return 1;

   switch (mode) {
      case AM_IMMEDIATE:
      case AM_INDEXED_INDIRECT:
      case AM_INDIRECT_INDEXED:
      case AM_RELATIVE:
         if (mode == AM_RELATIVE) {
            long disp;
            disp = value - (*pc + 1);
            if (disp < -128 || disp > 127) {
               fprintf(stderr, "line %d: branch out of range\n", line);
               return 1;
            }
            emit_byte((unsigned char)(disp & 0xFF));
         } else {
            emit_byte((unsigned char)(value & 0xFF));
         }
         (*pc)++;
         break;

      case AM_ZP_OR_ABS:
      case AM_ZPX_OR_ABSX:
      case AM_ZPY_OR_ABSY:
      case AM_INDIRECT:
         emit_word((unsigned short)(value & 0xFFFF));
         (*pc) += 2;
         break;

      default:
         fprintf(stderr, "line %d: unhandled mode in emitter\n", line);
         return 1;
   }

   return 0;
}

int asm_pass2(asm_context_t *ctx)
{
   stmt_t *stmt;
   long pc;

   pc = ctx->origin;

   for (stmt = ctx->prog->head; stmt; stmt = stmt->next) {
      switch (stmt->kind) {
         case STMT_LABEL:
            break;

         case STMT_DIR:
            if (directive_emit_pass2(stmt->u.dir, &ctx->symbols, &pc, stmt->line))
               return 1;
            break;

         case STMT_INSN:
            if (insn_emit_pass2(&stmt->u.insn, &ctx->symbols, &pc, stmt->line))
               return 1;
            break;
      }
   }

   printf("\n");
   return 0;
}
