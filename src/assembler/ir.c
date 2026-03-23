#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ir.h"
#include "util.h"

static char *dup_upper(const char *s, size_t n)
{
   size_t i;
   char *p;

   p = (char *)malloc(n + 1);
   if (!p) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   for (i = 0; i < n; i++)
      p[i] = (char)toupper((unsigned char)s[i]);

   p[n] = '\0';
   return p;
}

static mode_spec_t parse_mode_spec(const char *suffix)
{
   if (!suffix || !*suffix)
      return MODE_SPEC_NONE;

   if (!strcmp(suffix, ".z"))
      return MODE_SPEC_Z;
   if (!strcmp(suffix, ".zx"))
      return MODE_SPEC_ZX;
   if (!strcmp(suffix, ".zy"))
      return MODE_SPEC_ZY;
   if (!strcmp(suffix, ".a"))
      return MODE_SPEC_A;
   if (!strcmp(suffix, ".ax"))
      return MODE_SPEC_AX;
   if (!strcmp(suffix, ".ay"))
      return MODE_SPEC_AY;
   if (!strcmp(suffix, ".i"))
      return MODE_SPEC_I;
   if (!strcmp(suffix, ".ix"))
      return MODE_SPEC_IX;
   if (!strcmp(suffix, ".iy"))
      return MODE_SPEC_IY;

   return MODE_SPEC_NONE;
}

const char *mode_spec_suffix(mode_spec_t spec)
{
   switch (spec) {
      case MODE_SPEC_NONE: return "";
      case MODE_SPEC_Z:    return ".z";
      case MODE_SPEC_ZX:   return ".zx";
      case MODE_SPEC_ZY:   return ".zy";
      case MODE_SPEC_A:    return ".a";
      case MODE_SPEC_AX:   return ".ax";
      case MODE_SPEC_AY:   return ".ay";
      case MODE_SPEC_I:    return ".i";
      case MODE_SPEC_IX:   return ".ix";
      case MODE_SPEC_IY:   return ".iy";
   }

   return "";
}

static void split_opcode_text(const char *opcode_text, char **opcode_out, mode_spec_t *spec_out)
{
   const char *dot;
   size_t len;

   dot = strchr(opcode_text, '.');
   if (!dot) {
      *opcode_out = dup_upper(opcode_text, strlen(opcode_text));
      *spec_out = MODE_SPEC_NONE;
      return;
   }

   len = (size_t)(dot - opcode_text);
   *opcode_out = dup_upper(opcode_text, len);
   *spec_out = parse_mode_spec(dot);
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

   free((char *)stmt->file);
   free(stmt->label);
   free(stmt->scope);
   free(stmt->segment);

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

      case STMT_CONST:
         free(stmt->u.cnst.name);
         expr_free(stmt->u.cnst.expr);
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

stmt_t *stmt_make_label(const char *file, int line, char *label)
{
   stmt_t *stmt;

   stmt = (stmt_t *)calloc(1, sizeof(*stmt));
   if (!stmt) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   stmt->kind = STMT_LABEL;
   stmt->file = xstrdup(file ? file : "<input>");
   stmt->line = line;
   stmt->address = 0;
   stmt->label = xstrdup(label);
   stmt->scope = NULL;
   stmt->segment = NULL;
   return stmt;
}

stmt_t *stmt_make_insn(const char *file, int line, char *label, char *opcode_text, addr_mode_t mode, expr_t *expr, int has_operand)
{
   stmt_t *stmt;

   stmt = (stmt_t *)calloc(1, sizeof(*stmt));
   if (!stmt) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   stmt->kind = STMT_INSN;
   stmt->file = xstrdup(file ? file : "<input>");
   stmt->line = line;
   stmt->address = 0;
   stmt->label = xstrdup(label);
   stmt->scope = NULL;
   stmt->segment = NULL;
   split_opcode_text(opcode_text, &stmt->u.insn.opcode, &stmt->u.insn.spec);
   stmt->u.insn.mode = mode;
   stmt->u.insn.expr = expr;
   stmt->u.insn.has_operand = has_operand;
   stmt->u.insn.final_mode = EM_IMPLIED;
   stmt->u.insn.size = 1;
   return stmt;
}

stmt_t *stmt_make_dir(const char *file, int line, char *label, directive_info_t *dir)
{
   stmt_t *stmt;

   stmt = (stmt_t *)calloc(1, sizeof(*stmt));
   if (!stmt) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   stmt->kind = STMT_DIR;
   stmt->file = xstrdup(file ? file : "<input>");
   stmt->line = line;
   stmt->address = 0;
   stmt->label = xstrdup(label);
   stmt->scope = NULL;
   stmt->segment = NULL;
   stmt->u.dir = dir;
   return stmt;
}

stmt_t *stmt_make_const(const char *file, int line, char *name, expr_t *expr)
{
   stmt_t *stmt;

   stmt = (stmt_t *)calloc(1, sizeof(*stmt));
   if (!stmt) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   stmt->kind = STMT_CONST;
   stmt->file = xstrdup(file ? file : "<input>");
   stmt->line = line;
   stmt->address = 0;
   stmt->label = NULL;
   stmt->scope = NULL;
   stmt->segment = NULL;
   stmt->u.cnst.name = xstrdup(name);
   stmt->u.cnst.expr = expr;
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

   printf("%s:%d: ", stmt->file ? stmt->file : "<input>", stmt->line);

   if (stmt->label)
      printf("label=%s ", stmt->label);
   if (stmt->scope)
      printf("scope=%s ", stmt->scope);
   if (stmt->segment)
      printf("segment=%s ", stmt->segment);

   switch (stmt->kind) {
      case STMT_LABEL:
         printf("label-only");
         break;

      case STMT_INSN:
         printf("insn %s%s %s size=%d emode=%d",
                stmt->u.insn.opcode,
                mode_spec_suffix(stmt->u.insn.spec),
                addr_mode_name(stmt->u.insn.mode),
                stmt->u.insn.size,
                (int)stmt->u.insn.final_mode);
         if (stmt->u.insn.expr) {
            printf(" expr=");
            expr_print(stmt->u.insn.expr);
         }
         break;

      case STMT_DIR:
         directive_print(stmt->u.dir);
         return;

      case STMT_CONST:
         printf("const %s = ", stmt->u.cnst.name);
         expr_print(stmt->u.cnst.expr);
         break;
   }

   printf("\n");
}

void program_ir_print(const program_ir_t *prog)
{
   const stmt_t *stmt;

   for (stmt = prog->head; stmt; stmt = stmt->next)
      stmt_print(stmt);
}
