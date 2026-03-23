#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"

static char *xstrdup(const char *s)
{
   size_t n;
   char *p;

   if (!s)
      return NULL;

   n = strlen(s) + 1;
   p = (char *)malloc(n);
   if (!p) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   memcpy(p, s, n);
   return p;
}

void program_ir_init(program_ir_t *prog)
{
   prog->head = NULL;
   prog->tail = NULL;
}

void program_ir_append(program_ir_t *prog, stmt_t *stmt)
{
   stmt->next = NULL;

   if (!prog->head) {
      prog->head = stmt;
      prog->tail = stmt;
   } else {
      prog->tail->next = stmt;
      prog->tail = stmt;
   }
}

static void stmt_free(stmt_t *stmt)
{
   if (!stmt)
      return;

   free(stmt->label);

   switch (stmt->kind) {
      case STMT_LABEL:
         break;

      case STMT_INSN:
         free(stmt->u.insn.opcode);
         expr_free(stmt->u.insn.expr);
         break;

      case STMT_DIR:
         directive_free(stmt->u.dir);
         break;
   }

   free(stmt);
}

void program_ir_free(program_ir_t *prog)
{
   stmt_t *stmt;
   stmt_t *next;

   stmt = prog->head;
   while (stmt) {
      next = stmt->next;
      stmt_free(stmt);
      stmt = next;
   }

   prog->head = NULL;
   prog->tail = NULL;
}

stmt_t *stmt_make_label(int line, char *label)
{
   stmt_t *stmt;

   stmt = (stmt_t *)calloc(1, sizeof(*stmt));
   if (!stmt) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   stmt->kind = STMT_LABEL;
   stmt->line = line;
   stmt->label = xstrdup(label);
   return stmt;
}

stmt_t *stmt_make_insn(int line, char *label, char *opcode, addr_mode_t mode, expr_t *expr, int has_operand)
{
   stmt_t *stmt;

   stmt = (stmt_t *)calloc(1, sizeof(*stmt));
   if (!stmt) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   stmt->kind = STMT_INSN;
   stmt->line = line;
   stmt->label = xstrdup(label);
   stmt->u.insn.opcode = xstrdup(opcode);
   stmt->u.insn.mode = mode;
   stmt->u.insn.expr = expr;
   stmt->u.insn.has_operand = has_operand;
   return stmt;
}

stmt_t *stmt_make_dir(int line, char *label, directive_info_t *dir)
{
   stmt_t *stmt;

   stmt = (stmt_t *)calloc(1, sizeof(*stmt));
   if (!stmt) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   stmt->kind = STMT_DIR;
   stmt->line = line;
   stmt->label = xstrdup(label);
   stmt->u.dir = dir;
   return stmt;
}

static const char *addr_mode_name(addr_mode_t mode)
{
   switch (mode) {
      case AM_NONE: return "none";
      case AM_IMPLIED: return "implied";
      case AM_ACCUMULATOR: return "accumulator";
      case AM_IMMEDIATE: return "immediate";
      case AM_ZP_OR_ABS: return "zp/abs";
      case AM_ZPX_OR_ABSX: return "zp,x/abs,x";
      case AM_ZPY_OR_ABSY: return "zp,y/abs,y";
      case AM_INDIRECT: return "indirect";
      case AM_INDEXED_INDIRECT: return "(zp,x)";
      case AM_INDIRECT_INDEXED: return "(zp),y";
      case AM_RELATIVE: return "relative";
   }

   return "unknown";
}

void stmt_print(const stmt_t *stmt)
{
   if (!stmt)
      return;

   printf("line %d: ", stmt->line);

   if (stmt->label)
      printf("label=%s ", stmt->label);

   switch (stmt->kind) {
      case STMT_LABEL:
         printf("label-only");
         break;

      case STMT_INSN:
         printf("insn %s %s", stmt->u.insn.opcode, addr_mode_name(stmt->u.insn.mode));
         if (stmt->u.insn.expr) {
            printf(" expr=");
            expr_print(stmt->u.insn.expr);
         }
         break;

      case STMT_DIR:
         directive_print(stmt->u.dir);
         return;
   }

   printf("\n");
}

void program_ir_print(const program_ir_t *prog)
{
   const stmt_t *stmt;

   for (stmt = prog->head; stmt; stmt = stmt->next)
      stmt_print(stmt);
}
