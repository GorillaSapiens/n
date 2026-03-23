#ifndef IR_H
#define IR_H

#include "addr_mode.h"
#include "expr.h"
#include "directive.h"

typedef enum stmt_kind {
   STMT_INSN = 0,
   STMT_DIR,
   STMT_LABEL
} stmt_kind_t;

typedef struct insn_info {
   char *opcode;
   addr_mode_t mode;
   expr_t *expr;
   int has_operand;
} insn_info_t;

typedef struct stmt stmt_t;

struct stmt {
   stmt_kind_t kind;
   int line;
   char *label;
   union {
      insn_info_t insn;
      directive_info_t *dir;
   } u;
   stmt_t *next;
};

typedef struct program_ir {
   stmt_t *head;
   stmt_t *tail;
} program_ir_t;

void program_ir_init(program_ir_t *prog);
void program_ir_append(program_ir_t *prog, stmt_t *stmt);
void program_ir_free(program_ir_t *prog);

stmt_t *stmt_make_label(int line, char *label);
stmt_t *stmt_make_insn(int line, char *label, char *opcode, addr_mode_t mode, expr_t *expr, int has_operand);
stmt_t *stmt_make_dir(int line, char *label, directive_info_t *dir);

void stmt_print(const stmt_t *stmt);
void program_ir_print(const program_ir_t *prog);

#endif
