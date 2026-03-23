#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "expr.h"
#include "symtab.h"
#include "util.h"

static expr_t *expr_alloc(expr_kind_t kind)
{
   expr_t *e;

   e = (expr_t *)calloc(1, sizeof(*e));
   if (!e) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   e->kind = kind;
   return e;
}

expr_t *expr_make_number(long value)
{
   expr_t *e;

   e = expr_alloc(EXPR_NUMBER);
   e->u.number = value;
   return e;
}

expr_t *expr_make_ident(const char *name)
{
   expr_t *e;

   e = expr_alloc(EXPR_IDENT);
   e->u.ident = xstrdup(name);
   return e;
}

expr_t *expr_make_char(int value)
{
   expr_t *e;

   e = expr_alloc(EXPR_CHARCONST);
   e->u.char_value = value;
   return e;
}

expr_t *expr_make_pc(void)
{
   return expr_alloc(EXPR_PC);
}

expr_t *expr_make_unary(expr_unary_op_t op, expr_t *child)
{
   expr_t *e;

   e = expr_alloc(EXPR_UNARY);
   e->u.unary.op = op;
   e->u.unary.child = child;
   return e;
}

expr_t *expr_make_binary(expr_binary_op_t op, expr_t *left, expr_t *right)
{
   expr_t *e;

   e = expr_alloc(EXPR_BINARY);
   e->u.binary.op = op;
   e->u.binary.left = left;
   e->u.binary.right = right;
   return e;
}

void expr_free(expr_t *expr)
{
   if (!expr)
      return;

   switch (expr->kind) {
      case EXPR_IDENT:
         free(expr->u.ident);
         break;

      case EXPR_UNARY:
         expr_free(expr->u.unary.child);
         break;

      case EXPR_BINARY:
         expr_free(expr->u.binary.left);
         expr_free(expr->u.binary.right);
         break;

      default:
         break;
   }

   free(expr);
}

static void expr_fprint_inner(FILE *fp, const expr_t *expr)
{
   if (!expr) {
      fprintf(fp, "<null>");
      return;
   }

   switch (expr->kind) {
      case EXPR_NUMBER:
         fprintf(fp, "%ld", expr->u.number);
         break;

      case EXPR_IDENT:
         fprintf(fp, "%s", expr->u.ident);
         break;

      case EXPR_CHARCONST:
         fprintf(fp, "'%d", expr->u.char_value);
         break;

      case EXPR_PC:
         fprintf(fp, "*");
         break;

      case EXPR_UNARY:
         fprintf(fp, "(");
         switch (expr->u.unary.op) {
            case EXPR_UOP_NEG:
               fprintf(fp, "-");
               break;

            case EXPR_UOP_LO:
               fprintf(fp, "<");
               break;

            case EXPR_UOP_HI:
               fprintf(fp, ">");
               break;
         }
         expr_fprint_inner(fp, expr->u.unary.child);
         fprintf(fp, ")");
         break;

      case EXPR_BINARY:
         fprintf(fp, "(");
         expr_fprint_inner(fp, expr->u.binary.left);
         switch (expr->u.binary.op) {
            case EXPR_BOP_ADD:
               fprintf(fp, " + ");
               break;

            case EXPR_BOP_SUB:
               fprintf(fp, " - ");
               break;

            case EXPR_BOP_MUL:
               fprintf(fp, " * ");
               break;

            case EXPR_BOP_DIV:
               fprintf(fp, " / ");
               break;
         }
         expr_fprint_inner(fp, expr->u.binary.right);
         fprintf(fp, ")");
         break;
   }
}

void expr_print(const expr_t *expr)
{
   expr_fprint_inner(stdout, expr);
}

void expr_fprint(FILE *fp, const expr_t *expr)
{
   expr_fprint_inner(fp, expr);
}

long parse_number_token(const char *text)
{
   char *end;
   int base;

   if (!text || !*text)
      return 0;

   if (text[0] == '$') {
      base = 16;
      text++;
   } else if (text[0] == '%') {
      base = 2;
      text++;
   } else {
      base = 10;
   }

   return strtol(text, &end, base);
}

int parse_charconst_token(const char *text)
{
   if (!text || text[0] != '\'')
      return 0;

   text++;

   if (*text == '\\') {
      text++;
      switch (*text) {
         case 'n':
            return '\n';

         case 'r':
            return '\r';

         case 't':
            return '\t';

         case '0':
            return '\0';

         case '\'':
            return '\'';

         case '"':
            return '"';

         case '\\':
            return '\\';

         default:
            return (unsigned char)*text;
      }
   }

   return (unsigned char)*text;
}

expr_eval_status_t expr_eval(const expr_t *expr,
                             const symtab_t *symtab,
                             long pc,
                             long *value)
{
   long left;
   long right;
   expr_eval_status_t rc_left;
   expr_eval_status_t rc_right;
   const symbol_t *sym;

   if (!expr || !value)
      return EXPR_EVAL_UNRESOLVED;

   switch (expr->kind) {
      case EXPR_NUMBER:
         *value = expr->u.number;
         return EXPR_EVAL_OK;

      case EXPR_IDENT:
         if (!symtab)
            return EXPR_EVAL_UNRESOLVED;

         sym = symtab_find_const(symtab, expr->u.ident);
         if (!sym || !sym->defined)
            return EXPR_EVAL_UNRESOLVED;

         *value = sym->value;
         return EXPR_EVAL_OK;

      case EXPR_CHARCONST:
         *value = expr->u.char_value;
         return EXPR_EVAL_OK;

      case EXPR_PC:
         *value = pc;
         return EXPR_EVAL_OK;

      case EXPR_UNARY:
         rc_left = expr_eval(expr->u.unary.child, symtab, pc, &left);
         if (rc_left != EXPR_EVAL_OK)
            return rc_left;

         switch (expr->u.unary.op) {
            case EXPR_UOP_NEG:
               *value = -left;
               return EXPR_EVAL_OK;

            case EXPR_UOP_LO:
               *value = left & 0xFF;
               return EXPR_EVAL_OK;

            case EXPR_UOP_HI:
               *value = (left >> 8) & 0xFF;
               return EXPR_EVAL_OK;
         }
         return EXPR_EVAL_UNRESOLVED;

      case EXPR_BINARY:
         rc_left = expr_eval(expr->u.binary.left, symtab, pc, &left);
         if (rc_left != EXPR_EVAL_OK)
            return rc_left;

         rc_right = expr_eval(expr->u.binary.right, symtab, pc, &right);
         if (rc_right != EXPR_EVAL_OK)
            return rc_right;

         switch (expr->u.binary.op) {
            case EXPR_BOP_ADD:
               *value = left + right;
               return EXPR_EVAL_OK;

            case EXPR_BOP_SUB:
               *value = left - right;
               return EXPR_EVAL_OK;

            case EXPR_BOP_MUL:
               *value = left * right;
               return EXPR_EVAL_OK;

            case EXPR_BOP_DIV:
               if (right == 0)
                  return EXPR_EVAL_DIVZERO;
               *value = left / right;
               return EXPR_EVAL_OK;
         }
         return EXPR_EVAL_UNRESOLVED;
   }

   return EXPR_EVAL_UNRESOLVED;
}

int expr_is_byte_value(long value)
{
   return value >= 0 && value <= 0xFF;
}
