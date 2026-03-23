#ifndef IR_H
#define IR_H

#include "addr_mode.h"
#include "expr.h"
#include "directive.h"
#include "opcode.h"

typedef enum mode_spec {
   MODE_SPEC_NONE = 0,
   MODE_SPEC_Z,
   MODE_SPEC_ZX,
   MODE_SPEC_ZY,
   MODE_SPEC_A,
   MODE_SPEC_AX,
   MODE_SPEC_AY,
   MODE_SPEC_I,
   MODE_SPEC_IX,
   MODE_SPEC_IY
} mode_spec_t;

typedef enum stmt_kind {
   STMT_INSN = 0,
   STMT_DIR,
   STMT_LABEL,
   STMT_CONST
} stmt_kind_t;

typedef struct insn_info {
   char *opcode;
   mode_spec_t spec;
   addr_mode_t mode;
   expr_t *expr;
   int has_operand;

   emit_mode_t final_mode;
   int size;
} insn_info_t;

typedef struct const_info {
   char *name;
   expr_t *expr;
} const_info_t;

typedef struct stmt stmt_t;

struct stmt {
   stmt_kind_t kind;
   const char *file;
   int line;
   long address;
   char *label;
   char *scope;
   union {
      insn_info_t insn;
      directive_info_t *dir;
      const_info_t cnst;
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

stmt_t *stmt_make_label(const char *file, int line, char *label);
stmt_t *stmt_make_insn(const char *file, int line, char *label, char *opcode_text, addr_mode_t mode, expr_t *expr, int has_operand);
stmt_t *stmt_make_dir(const char *file, int line, char *label, directive_info_t *dir);
stmt_t *stmt_make_const(const char *file, int line, char *name, expr_t *expr);

const char *mode_spec_suffix(mode_spec_t spec);

void stmt_print(const stmt_t *stmt);
void program_ir_print(const program_ir_t *prog);

#endif
