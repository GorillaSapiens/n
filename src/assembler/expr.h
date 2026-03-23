#ifndef EXPR_H
#define EXPR_H

#include <stdio.h>

typedef enum expr_kind {
   EXPR_NUMBER = 0,
   EXPR_IDENT,
   EXPR_CHARCONST,
   EXPR_PC,
   EXPR_UNARY,
   EXPR_BINARY
} expr_kind_t;

typedef enum expr_unary_op {
   EXPR_UOP_NEG = 0,
   EXPR_UOP_LO,
   EXPR_UOP_HI
} expr_unary_op_t;

typedef enum expr_binary_op {
   EXPR_BOP_ADD = 0,
   EXPR_BOP_SUB,
   EXPR_BOP_MUL,
   EXPR_BOP_DIV
} expr_binary_op_t;

typedef enum expr_eval_status {
   EXPR_EVAL_OK = 0,
   EXPR_EVAL_UNRESOLVED,
   EXPR_EVAL_DIVZERO
} expr_eval_status_t;

struct symtab;
typedef struct symtab symtab_t;

typedef struct expr expr_t;

struct expr {
   expr_kind_t kind;

   union {
      long number;
      char *ident;
      int char_value;

      struct {
         expr_unary_op_t op;
         expr_t *child;
      } unary;

      struct {
         expr_binary_op_t op;
         expr_t *left;
         expr_t *right;
      } binary;
   } u;
};

expr_t *expr_make_number(long value);
expr_t *expr_make_ident(const char *name);
expr_t *expr_make_char(int value);
expr_t *expr_make_pc(void);
expr_t *expr_make_unary(expr_unary_op_t op, expr_t *child);
expr_t *expr_make_binary(expr_binary_op_t op, expr_t *left, expr_t *right);

void expr_free(expr_t *expr);
void expr_print(const expr_t *expr);
void expr_fprint(FILE *fp, const expr_t *expr);

long parse_number_token(const char *text);
int parse_charconst_token(const char *text);

expr_eval_status_t expr_eval(const expr_t *expr,
                             const symtab_t *symtab,
                             const char *scope,
                             long pc,
                             long *value);

int expr_is_byte_value(long value);

#endif
