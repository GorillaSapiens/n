#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asm_pass.h"
#include "opcode.h"

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

static int spec_to_emit_mode(mode_spec_t spec, emit_mode_t *out_mode)
{
   switch (spec) {
      case MODE_SPEC_Z:
         *out_mode = EM_ZP;
         return 1;

      case MODE_SPEC_ZX:
         *out_mode = EM_ZPX;
         return 1;

      case MODE_SPEC_ZY:
         *out_mode = EM_ZPY;
         return 1;

      case MODE_SPEC_A:
         *out_mode = EM_ABS;
         return 1;

      case MODE_SPEC_AX:
         *out_mode = EM_ABSX;
         return 1;

      case MODE_SPEC_AY:
         *out_mode = EM_ABSY;
         return 1;

      case MODE_SPEC_I:
         *out_mode = EM_IND;
         return 1;

      case MODE_SPEC_IX:
         *out_mode = EM_INDX;
         return 1;

      case MODE_SPEC_IY:
         *out_mode = EM_INDY;
         return 1;

      default:
         break;
   }

   return 0;
}

/*
   Addressing-mode specifiers are allowed to override some surface syntax.

   Examples:
      LDA.ix $44    == LDA ($44,X)
      LDA.iy $44    == LDA ($44),Y
      JMP.i  $1234  == JMP ($1234)

   Why:
   - The suffix is a hard encoding selector, not a suggestion.
   - Requiring both the suffix and the traditional punctuation is redundant.
   - We still reject combinations that don't match the underlying operand
     family at all, like LDA.ix foo,Y or LDA.ax (foo).

   This lets the programmer force a specific encoding without turning the
   assembler into a "maybe I will shrink it later" guessing machine.
*/
static int parsed_mode_accepts_spec(addr_mode_t parsed_mode, mode_spec_t spec)
{
   parsed_mode = normalize_mode("", parsed_mode);

   switch (spec) {
      case MODE_SPEC_Z:
      case MODE_SPEC_A:
         return parsed_mode == AM_ZP_OR_ABS;

      case MODE_SPEC_ZX:
      case MODE_SPEC_AX:
         return parsed_mode == AM_ZPX_OR_ABSX;

      case MODE_SPEC_ZY:
      case MODE_SPEC_AY:
         return parsed_mode == AM_ZPY_OR_ABSY;

      case MODE_SPEC_I:
         return parsed_mode == AM_ZP_OR_ABS ||
                parsed_mode == AM_INDIRECT;

      case MODE_SPEC_IX:
         return parsed_mode == AM_ZP_OR_ABS ||
                parsed_mode == AM_INDEXED_INDIRECT;

      case MODE_SPEC_IY:
         return parsed_mode == AM_ZP_OR_ABS ||
                parsed_mode == AM_INDIRECT_INDEXED;

      case MODE_SPEC_NONE:
         return 1;
   }

   return 0;
}

static int expr_is_u8_value(long value)
{
   return value >= 0 && value <= 0xFF;
}

static int expr_is_s8_or_u8_value(long value)
{
   return value >= -128 && value <= 0xFF;
}

/*
   No suffix:
      Use stable-width defaults for ambiguous families when both concrete
      encodings exist for this opcode.

      But if the opcode only supports one concrete encoding in that family,
      choose the only legal one. Example:
         STX $99,Y
      parses as the ambiguous Y-index family, but STX only has zp,Y on
      6502, not abs,Y, so the assembler must choose ZPY here or it will
      reject valid source for the wrong reason.

   With suffix:
      The suffix is a hard requirement. Encode exactly that mode or error.
*/
static int choose_emit_mode(const insn_info_t *insn, emit_mode_t *out_mode, const char **why)
{
   addr_mode_t mode;
   unsigned char dummy;

   mode = normalize_mode(insn->opcode, insn->mode);

   if (insn->spec != MODE_SPEC_NONE) {
      if (!parsed_mode_accepts_spec(mode, insn->spec)) {
         if (why)
            *why = "specifier is incompatible with the operand shape";
         return 0;
      }

      if (!spec_to_emit_mode(insn->spec, out_mode)) {
         if (why)
            *why = "unknown addressing-mode specifier";
         return 0;
      }

      return 1;
   }

   switch (mode) {
      case AM_IMPLIED:
         *out_mode = EM_IMPLIED;
         return 1;

      case AM_ACCUMULATOR:
         *out_mode = EM_ACCUMULATOR;
         return 1;

      case AM_IMMEDIATE:
         *out_mode = EM_IMMEDIATE;
         return 1;

      case AM_INDEXED_INDIRECT:
         *out_mode = EM_INDX;
         return 1;

      case AM_INDIRECT_INDEXED:
         *out_mode = EM_INDY;
         return 1;

      case AM_INDIRECT:
         *out_mode = EM_IND;
         return 1;

      case AM_RELATIVE:
         *out_mode = EM_REL;
         return 1;

      case AM_ZP_OR_ABS:
         if (opcode_lookup(insn->opcode, EM_ABS, &dummy)) {
            *out_mode = EM_ABS;
            return 1;
         }
         if (opcode_lookup(insn->opcode, EM_ZP, &dummy)) {
            *out_mode = EM_ZP;
            return 1;
         }
         break;

      case AM_ZPX_OR_ABSX:
         if (opcode_lookup(insn->opcode, EM_ABSX, &dummy)) {
            *out_mode = EM_ABSX;
            return 1;
         }
         if (opcode_lookup(insn->opcode, EM_ZPX, &dummy)) {
            *out_mode = EM_ZPX;
            return 1;
         }
         break;

      case AM_ZPY_OR_ABSY:
         if (opcode_lookup(insn->opcode, EM_ABSY, &dummy)) {
            *out_mode = EM_ABSY;
            return 1;
         }
         if (opcode_lookup(insn->opcode, EM_ZPY, &dummy)) {
            *out_mode = EM_ZPY;
            return 1;
         }
         break;

      default:
         break;
   }

   if (why)
      *why = "unsupported addressing mode";

   return 0;
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

static int insn_size_pass1(const insn_info_t *insn, int line)
{
   emit_mode_t emode;
   const char *why;

   if (!choose_emit_mode(insn, &emode, &why)) {
      fprintf(stderr,
              "line %d: %s%s ... %s\n",
              line, insn->opcode, mode_spec_suffix(insn->spec), why);
      return -1;
   }

   return emit_mode_size(emode);
}

void asm_context_init(asm_context_t *ctx, program_ir_t *prog)
{
   ctx->prog = prog;
   ctx->origin = 0;
   symtab_init(&ctx->symbols);
   ihex_image_init(&ctx->image);
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
            sz = insn_size_pass1(&stmt->u.insn, stmt->line);
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

static int emit_byte(asm_context_t *ctx, long addr, unsigned char b, int line)
{
   if (!ihex_write_byte(&ctx->image, addr, b)) {
      fprintf(stderr, "line %d: output address out of range: $%lX\n", line, addr);
      return 0;
   }

   return 1;
}

static int emit_word(asm_context_t *ctx, long addr, unsigned short w, int line)
{
   if (!ihex_write_word(&ctx->image, addr, w)) {
      fprintf(stderr, "line %d: output address out of range: $%lX\n", line, addr);
      return 0;
   }

   return 1;
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

static int directive_emit_pass2(asm_context_t *ctx,
                                const directive_info_t *dir,
                                const symtab_t *symbols,
                                long *pc,
                                int line)
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
         if (!emit_byte(ctx, *pc, (unsigned char)(value & 0xFF), line))
            return 1;
         (*pc)++;
      }
      return 0;
   }

   if (!strcmp(dir->name, ".word")) {
      for (node = dir->exprs; node; node = node->next) {
         if (eval_or_report(node->expr, symbols, *pc, &value, line))
            return 1;
         if (!emit_word(ctx, *pc, (unsigned short)(value & 0xFFFF), line))
            return 1;
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
            if (!emit_byte(ctx, *pc, (unsigned char)s[i], line))
               return 1;
            (*pc)++;
         }
      }
      return 0;
   }

   fprintf(stderr, "line %d: unhandled directive %s\n", line, dir->name);
   return 1;
}

static int insn_emit_pass2(asm_context_t *ctx,
                           const insn_info_t *insn,
                           const symtab_t *symbols,
                           long *pc,
                           int line)
{
   long value;
   emit_mode_t emode;
   unsigned char opcode;
   const char *why;

   if (!choose_emit_mode(insn, &emode, &why)) {
      fprintf(stderr,
              "line %d: %s%s ... %s\n",
              line, insn->opcode, mode_spec_suffix(insn->spec), why);
      return 1;
   }

#if 0
fprintf(stderr, "DEBUG: opcode=%s spec=%s emode=%d\n",
        insn->opcode, mode_spec_suffix(insn->spec), (int)emode);
#endif

   if (!opcode_lookup(insn->opcode, emode, &opcode)) {
      fprintf(stderr,
              "line %d: illegal addressing mode for %s%s\n",
              line, insn->opcode, mode_spec_suffix(insn->spec));
      return 1;
   }

   if (!emit_byte(ctx, *pc, opcode, line))
      return 1;
   (*pc)++;

   switch (emode) {
      case EM_IMPLIED:
      case EM_ACCUMULATOR:
         return 0;

      default:
         break;
   }

   if (!insn->has_operand || !insn->expr) {
      fprintf(stderr, "line %d: internal error: missing operand expression\n", line);
      return 1;
   }

   if (eval_or_report(insn->expr, symbols, *pc, &value, line))
      return 1;

   switch (emode) {
      case EM_IMMEDIATE:
         if (!expr_is_s8_or_u8_value(value)) {
            fprintf(stderr,
                    "line %d: %s%s immediate operand out of range: %ld\n",
                    line, insn->opcode, mode_spec_suffix(insn->spec), value);
            return 1;
         }
         break;

      case EM_ZP:
      case EM_ZPX:
      case EM_ZPY:
      case EM_INDX:
      case EM_INDY:
         if (!expr_is_u8_value(value)) {
            fprintf(stderr,
                    "line %d: %s%s requires a zero-page operand, got $%lX\n",
                    line, insn->opcode, mode_spec_suffix(insn->spec), value & 0xFFFF);
            return 1;
         }
         break;

      default:
         break;
   }

   switch (emode) {
      case EM_IMMEDIATE:
      case EM_ZP:
      case EM_ZPX:
      case EM_ZPY:
      case EM_INDX:
      case EM_INDY:
         if (!emit_byte(ctx, *pc, (unsigned char)(value & 0xFF), line))
            return 1;
         (*pc)++;
         return 0;

      case EM_REL: {
         long disp;

         disp = value - (*pc + 1);
         if (disp < -128 || disp > 127) {
            fprintf(stderr, "line %d: branch out of range\n", line);
            return 1;
         }

         if (!emit_byte(ctx, *pc, (unsigned char)(disp & 0xFF), line))
            return 1;
         (*pc)++;
         return 0;
      }

      case EM_ABS:
      case EM_ABSX:
      case EM_ABSY:
      case EM_IND:
         if (!emit_word(ctx, *pc, (unsigned short)(value & 0xFFFF), line))
            return 1;
         (*pc) += 2;
         return 0;

      default:
         fprintf(stderr, "line %d: internal emitter error\n", line);
         return 1;
   }
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
            if (directive_emit_pass2(ctx, stmt->u.dir, &ctx->symbols, &pc, stmt->line))
               return 1;
            break;

         case STMT_INSN:
            if (insn_emit_pass2(ctx, &stmt->u.insn, &ctx->symbols, &pc, stmt->line))
               return 1;
            break;
      }
   }

   return 0;
}
