#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include "asm_pass.h"
#include "opcode.h"
#include "util.h"

#define DEFAULT_SEGMENT_NAME "__default__"
#define O65_SEG_UNDEF 0
#define O65_SEG_ABS   1
#define O65_SEG_TEXT  2
#define O65_SEG_DATA  3
#define O65_SEG_BSS   4
#define O65_SEG_ZP    5

static void print_loc(FILE *fp, const stmt_t *stmt)
{
   fprintf(fp, "%s:%d", stmt->file ? stmt->file : "<input>", stmt->line);
}

static void asm_error(asm_context_t *ctx, const stmt_t *stmt, const char *fmt, ...)
{
   va_list ap;

   ctx->error_count++;

   print_loc(stderr, stmt);
   fprintf(stderr, ": ");

   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);

   fprintf(stderr, "\n");
}

static void asm_warning(const stmt_t *stmt, const char *fmt, ...)
{
   va_list ap;

   print_loc(stderr, stmt);
   fprintf(stderr, ": warning: ");

   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);

   fprintf(stderr, "\n");
}

static char *unquote_string(const char *s)
{
   size_t n;
   char *out;

   if (!s)
      return NULL;

   n = strlen(s);
   if (n >= 2 && s[0] == '"' && s[n - 1] == '"') {
      out = (char *)malloc(n - 2 + 1);
      if (!out) {
         fprintf(stderr, "out of memory\n");
         exit(1);
      }
      memcpy(out, s + 1, n - 2);
      out[n - 2] = '\0';
      return out;
   }

   return xstrdup(s);
}

static int decode_escaped_string(const char *quoted,
                                 unsigned char *out,
                                 int out_cap,
                                 int *out_len)
{
   size_t i;
   size_t n;
   int len;

   if (!quoted || !out_len)
      return 0;

   n = strlen(quoted);
   if (n < 2 || quoted[0] != '"' || quoted[n - 1] != '"')
      return 0;

   len = 0;

   for (i = 1; i + 1 < n; i++) {
      unsigned char ch;

      if (quoted[i] == '\\' && i + 2 < n) {
         i++;
         switch (quoted[i]) {
            case 'n':
               ch = '\n';
               break;
            case 'r':
               ch = '\r';
               break;
            case 't':
               ch = '\t';
               break;
            case '0':
               ch = '\0';
               break;
            case '\\':
               ch = '\\';
               break;
            case '"':
               ch = '"';
               break;
            case '\'':
               ch = '\'';
               break;
            default:
               ch = (unsigned char)quoted[i];
               break;
         }
      } else {
         ch = (unsigned char)quoted[i];
      }

      if (out && len < out_cap)
         out[len] = ch;
      len++;
   }

   *out_len = len;
   return 1;
}

static void free_imports(import_name_t *head)
{
   import_name_t *p;
   import_name_t *next;

   for (p = head; p; p = next) {
      next = p->next;
      free(p->name);
      free(p);
   }
}

static import_name_t *find_import(asm_context_t *ctx, const char *name)
{
   import_name_t *p;

   for (p = ctx->imports; p; p = p->next) {
      if (!strcmp(p->name, name))
         return p;
   }

   return NULL;
}

static const import_name_t *find_import_const(const asm_context_t *ctx, const char *name)
{
   const import_name_t *p;

   for (p = ctx->imports; p; p = p->next) {
      if (!strcmp(p->name, name))
         return p;
   }

   return NULL;
}

static int import_is_zp(const asm_context_t *ctx, const char *name)
{
   const import_name_t *p = find_import_const(ctx, name);
   return p ? p->addr_size_zp : 0;
}

static void free_weaks(weak_name_t *head)
{
   weak_name_t *p;
   weak_name_t *next;

   for (p = head; p; p = next) {
      next = p->next;
      free(p->name);
      free(p);
   }
}

static weak_name_t *find_weak(asm_context_t *ctx, const char *name)
{
   weak_name_t *p;

   for (p = ctx->weaks; p; p = p->next) {
      if (!strcmp(p->name, name))
         return p;
   }

   return NULL;
}

void asm_add_weak(asm_context_t *ctx, const stmt_t *stmt, const char *name)
{
   weak_name_t *p;

   p = find_weak(ctx, name);
   if (p)
      return;

   p = (weak_name_t *)calloc(1, sizeof(*p));
   if (!p) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   p->name = xstrdup(name);
   p->file = stmt->file;
   p->line = stmt->line;
   p->next = ctx->weaks;
   ctx->weaks = p;
}

int asm_symbol_is_weak(const asm_context_t *ctx, const char *name)
{
   const weak_name_t *p;

   for (p = ctx->weaks; p; p = p->next) {
      if (!strcmp(p->name, name))
         return 1;
   }

   return 0;
}

static int directive_name_implies_zp(const char *name)
{
   return name && (!strcmp(name, ".importzp") || !strcmp(name, ".exportzp") || !strcmp(name, ".globalzp"));
}

static int directive_is_import_family(const char *name)
{
   return name && (!strcmp(name, ".import") || !strcmp(name, ".importzp") || !strcmp(name, ".global") || !strcmp(name, ".globalzp"));
}

static int directive_is_export_family(const char *name)
{
   return name && (!strcmp(name, ".global") || !strcmp(name, ".globalzp") || !strcmp(name, ".export") || !strcmp(name, ".exportzp"));
}

static void add_import(asm_context_t *ctx, const stmt_t *stmt, const char *name, int addr_size_zp)
{
   import_name_t *p;

   p = find_import(ctx, name);
   if (p) {
      if (addr_size_zp)
         p->addr_size_zp = 1;
      return;
   }

   p = (import_name_t *)calloc(1, sizeof(*p));
   if (!p) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   p->name = xstrdup(name);
   p->file = stmt->file;
   p->line = stmt->line;
   p->addr_size_zp = addr_size_zp ? 1 : 0;
   p->next = ctx->imports;
   ctx->imports = p;
}

static int segment_name_matches(const char *name, const char *base)
{
   size_t n;

   if (!name || !base)
      return 0;

   n = strlen(base);
   return strncasecmp(name, base, n) == 0 && (name[n] == '\0' || name[n] == '.');
}

static int segment_name_to_o65(const char *name)
{
   if (!name || !strcasecmp(name, DEFAULT_SEGMENT_NAME) || segment_name_matches(name, "TEXT") || segment_name_matches(name, "CODE") || segment_name_matches(name, "RODATA"))
      return O65_SEG_TEXT;
   if (segment_name_matches(name, "DATA"))
      return O65_SEG_DATA;
   if (segment_name_matches(name, "BSS"))
      return O65_SEG_BSS;
   if (segment_name_matches(name, "ZP") || segment_name_matches(name, "ZEROPAGE") || segment_name_matches(name, "ZERO"))
      return O65_SEG_ZP;
   return O65_SEG_TEXT;
}

static asm_segment_t *segment_find(asm_context_t *ctx, const char *name)
{
   asm_segment_t *seg;

   for (seg = ctx->segments; seg; seg = seg->next) {
      if (!strcmp(seg->name, name))
         return seg;
   }

   return NULL;
}

static asm_segment_t *segment_get_or_create(asm_context_t *ctx, const char *name)
{
   asm_segment_t *seg;

   seg = segment_find(ctx, name);
   if (seg)
      return seg;

   seg = (asm_segment_t *)calloc(1, sizeof(*seg));
   if (!seg) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   seg->name = xstrdup(name);
   seg->base = 0;
   seg->size = 0x10000L;
   seg->pc = 0;
   seg->used_size = 0;
   seg->defined = 0;
   seg->overflow_warned = 0;
   seg->next = ctx->segments;
   ctx->segments = seg;
   return seg;
}

static void free_segments(asm_segment_t *head)
{
   asm_segment_t *seg;
   asm_segment_t *next;

   for (seg = head; seg; seg = next) {
      next = seg->next;
      free(seg->name);
      free(seg);
   }
}

static void reset_segment_pcs(asm_context_t *ctx)
{
   asm_segment_t *seg;

   for (seg = ctx->segments; seg; seg = seg->next) {
      seg->pc = 0;
      seg->overflow_warned = 0;
   }
}

static void snapshot_segment_used_sizes(asm_context_t *ctx)
{
   asm_segment_t *seg;

   for (seg = ctx->segments; seg; seg = seg->next)
      seg->used_size = seg->pc;
}

static void ensure_default_segment(asm_context_t *ctx)
{
   asm_segment_t *seg;

   seg = segment_get_or_create(ctx, DEFAULT_SEGMENT_NAME);
   seg->base = 0;
   seg->size = 0x10000L;
   seg->defined = 1;
}

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

static int is_accum_shorthand_opcode(const char *opcode)
{
   return !strcmp(opcode, "ASL") ||
          !strcmp(opcode, "LSR") ||
          !strcmp(opcode, "ROL") ||
          !strcmp(opcode, "ROR");
}

static int is_local_name(const char *name)
{
   return name && name[0] == '@';
}

static char *make_scoped_name(const char *scope, const char *name)
{
   char buf[4096];

   snprintf(buf, sizeof(buf), "%s::%s", scope ? scope : "__root__", name);
   return xstrdup(buf);
}

static char *make_file_scoped_name(const char *file, const char *name)
{
   char buf[4096];

   snprintf(buf, sizeof(buf), "%s::%s", file ? file : "<input>", name);
   return xstrdup(buf);
}

static int is_global_label_name(const char *name)
{
   return name && !is_local_name(name);
}

static int directive_expr_is_ident(const expr_t *expr, const char **name_out)
{
   if (!expr || expr->kind != EXPR_IDENT)
      return 0;

   *name_out = expr->u.ident;
   return 1;
}

static const char *proc_decl_name(const stmt_t *stmt)
{
   const char *name;

   if (!stmt || stmt->kind != STMT_DIR || !stmt->u.dir)
      return NULL;
   if (strcmp(stmt->u.dir->name, ".proc"))
      return NULL;
   if (!stmt->u.dir->exprs || stmt->u.dir->exprs->next)
      return NULL;
   if (!directive_expr_is_ident(stmt->u.dir->exprs->expr, &name))
      return NULL;
   return name;
}

static int is_exported_name(const program_ir_t *prog, const char *file, const char *name)
{
   const stmt_t *stmt;
   const expr_list_node_t *node;

   for (stmt = prog->head; stmt; stmt = stmt->next) {
      if (stmt->kind != STMT_DIR)
         continue;
      if (!stmt->file || strcmp(stmt->file, file))
         continue;
      if (!directive_is_export_family(stmt->u.dir->name))
         continue;

      for (node = stmt->u.dir->exprs; node; node = node->next) {
         const char *n;
         if (directive_expr_is_ident(node->expr, &n) && !strcmp(n, name))
            return 1;
      }
   }

   return 0;
}

static const char *symbol_storage_name(const program_ir_t *prog, const stmt_t *stmt, const char *name, char **owned_out)
{
   *owned_out = NULL;

   if (is_local_name(name)) {
      *owned_out = make_scoped_name(stmt->scope, name);
      return *owned_out;
   }

   if (is_exported_name(prog, stmt->file, name))
      return name;

   *owned_out = make_file_scoped_name(stmt->file, name);
   return *owned_out;
}

static void assign_segments(program_ir_t *prog)
{
   stmt_t *stmt;
   char *current_segment;

   current_segment = xstrdup(DEFAULT_SEGMENT_NAME);

   for (stmt = prog->head; stmt; stmt = stmt->next) {
      free(stmt->segment);
      stmt->segment = xstrdup(current_segment);

      if (stmt->kind == STMT_DIR &&
          stmt->u.dir &&
          !strcmp(stmt->u.dir->name, ".segment") &&
          stmt->u.dir->string) {
         char *segname;

         segname = unquote_string(stmt->u.dir->string);
         free(current_segment);
         current_segment = xstrdup(segname);

         free(stmt->segment);
         stmt->segment = xstrdup(segname);

         free(segname);
      }
   }

   free(current_segment);
}

static void assign_scopes(program_ir_t *prog)
{
   typedef struct scope_stack {
      char *name;
      struct scope_stack *next;
   } scope_stack_t;

   stmt_t *stmt;
   char *current_scope;
   scope_stack_t *proc_stack;

   current_scope = xstrdup("__root__");
   proc_stack = NULL;

   for (stmt = prog->head; stmt; stmt = stmt->next) {
      const char *proc_name;

      free(stmt->scope);
      stmt->scope = xstrdup(current_scope);

      if (stmt->kind == STMT_LABEL && stmt->label && is_global_label_name(stmt->label)) {
         char *owned;
         const char *name_key;

         name_key = symbol_storage_name(prog, stmt, stmt->label, &owned);
         free(current_scope);
         current_scope = xstrdup(name_key);
         free(stmt->scope);
         stmt->scope = xstrdup(current_scope);
         free(owned);
         continue;
      }

      if (stmt->label && is_global_label_name(stmt->label)) {
         char *owned;
         const char *name_key;

         name_key = symbol_storage_name(prog, stmt, stmt->label, &owned);
         free(current_scope);
         current_scope = xstrdup(name_key);
         free(stmt->scope);
         stmt->scope = xstrdup(current_scope);
         free(owned);
         continue;
      }

      proc_name = proc_decl_name(stmt);
      if (proc_name) {
         scope_stack_t *frame;
         char *owned;
         const char *name_key;

         frame = (scope_stack_t *)calloc(1, sizeof(*frame));
         if (!frame) {
            fprintf(stderr, "out of memory\n");
            exit(1);
         }
         frame->name = current_scope;
         frame->next = proc_stack;
         proc_stack = frame;

         name_key = symbol_storage_name(prog, stmt, proc_name, &owned);
         current_scope = xstrdup(name_key);
         free(owned);
         continue;
      }

      if (stmt->kind == STMT_DIR && stmt->u.dir && !strcmp(stmt->u.dir->name, ".endproc")) {
         if (proc_stack) {
            scope_stack_t *frame = proc_stack;
            free(current_scope);
            current_scope = frame->name;
            proc_stack = frame->next;
            free(frame);
         } else {
            free(current_scope);
            current_scope = xstrdup("__root__");
         }
      }
   }

   while (proc_stack) {
      scope_stack_t *frame = proc_stack->next;
      free(proc_stack->name);
      free(proc_stack);
      proc_stack = frame;
   }
   free(current_scope);
}

static void gather_segment_uses(asm_context_t *ctx)
{
   stmt_t *stmt;

   ensure_default_segment(ctx);

   for (stmt = ctx->prog->head; stmt; stmt = stmt->next) {
      if (stmt->segment)
         segment_get_or_create(ctx, stmt->segment);
   }
}

static void define_or_update_abs_symbol(symtab_t *tab, const char *name, long value)
{
   symbol_t *sym;

   sym = symtab_find(tab, name);
   if (!sym)
      sym = symtab_declare(tab, name, "<segments>", 0);

   if (sym)
      symtab_set_value_segment(sym, value, O65_SEG_ABS);
}

static void publish_segment_symbols(asm_context_t *ctx)
{
   asm_segment_t *seg;
   char buf[4096];

   for (seg = ctx->segments; seg; seg = seg->next) {
      snprintf(buf, sizeof(buf), "%s_BASE", seg->name);
      define_or_update_abs_symbol(&ctx->symbols, buf, seg->base);

      snprintf(buf, sizeof(buf), "%s_SIZE", seg->name);
      define_or_update_abs_symbol(&ctx->symbols, buf, seg->used_size);

      snprintf(buf, sizeof(buf), "%s_END", seg->name);
      define_or_update_abs_symbol(&ctx->symbols, buf, seg->base + seg->used_size);

      snprintf(buf, sizeof(buf), "%s_CAPACITY", seg->name);
      define_or_update_abs_symbol(&ctx->symbols, buf, seg->size);
   }
}

static void gather_segment_defs(asm_context_t *ctx)
{
   stmt_t *stmt;

   for (stmt = ctx->prog->head; stmt; stmt = stmt->next) {
      const directive_info_t *dir;
      const expr_list_node_t *node;
      asm_segment_t *seg;
      char *segname;
      long base;
      long size;
      expr_eval_status_t rc;

      if (stmt->kind != STMT_DIR)
         continue;

      dir = stmt->u.dir;
      if (strcmp(dir->name, ".segmentdef"))
         continue;

      if (!dir->string) {
         asm_error(ctx, stmt, ".segmentdef expects a quoted segment name");
         continue;
      }

      if (!dir->exprs || !dir->exprs->next || dir->exprs->next->next) {
         asm_error(ctx, stmt, ".segmentdef expects exactly two expressions: base, size");
         continue;
      }

      segname = unquote_string(dir->string);
      seg = segment_get_or_create(ctx, segname);

      node = dir->exprs;
      rc = expr_eval(node->expr, &ctx->symbols, stmt->scope, stmt->file, 0, &base);
      if (rc != EXPR_EVAL_OK) {
         asm_error(ctx, stmt, ".segmentdef base could not be resolved");
         free(segname);
         continue;
      }

      node = node->next;
      rc = expr_eval(node->expr, &ctx->symbols, stmt->scope, stmt->file, 0, &size);
      if (rc != EXPR_EVAL_OK) {
         asm_error(ctx, stmt, ".segmentdef size could not be resolved");
         free(segname);
         continue;
      }

      if (base < 0 || size < 0) {
         asm_error(ctx, stmt, ".segmentdef base and size must be non-negative");
         free(segname);
         continue;
      }

      seg->base = base;
      seg->size = size;
      seg->defined = 1;

      publish_segment_symbols(ctx);
      free(segname);
   }
}

static void validate_segment_defs(asm_context_t *ctx)
{
   asm_segment_t *seg;

   for (seg = ctx->segments; seg; seg = seg->next) {
      if (!seg->defined) {
         ctx->error_count++;
         fprintf(stderr, "segment '%s' was used but never defined with .segmentdef\n", seg->name);
      }
   }
}

static addr_mode_t normalize_mode(const char *opcode, addr_mode_t mode)
{
   unsigned char raw_opcode;

   if ((is_branch_opcode(opcode) ||
        (opcode_parse_raw_byte(opcode, &raw_opcode) && opcode_raw_is_conditional_branch(raw_opcode))) &&
       mode == AM_ZP_OR_ABS)
      return AM_RELATIVE;

   if ((is_accum_shorthand_opcode(opcode) ||
        (opcode_parse_raw_byte(opcode, &raw_opcode) && opcode_raw_is_accumulator_shorthand(raw_opcode))) &&
       mode == AM_IMPLIED)
      return AM_ACCUMULATOR;

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

static int insn_is_long_branch_candidate(const insn_info_t *insn, emit_mode_t mode)
{
   return mode == EM_REL && opcode_is_conditional_branch(insn->opcode);
}

static int insn_can_relax_long_branch(const stmt_t *stmt, const asm_context_t *ctx)
{
   long value;
   long disp;

   if (!stmt || stmt->kind != STMT_INSN)
      return 0;

   if (stmt->u.insn.final_mode != EM_REL_LONG || !stmt->u.insn.expr)
      return 0;

   if (expr_eval(stmt->u.insn.expr, &ctx->symbols, stmt->scope, stmt->file, stmt->address + 1, &value) != EXPR_EVAL_OK)
      return 0;

   disp = value - (stmt->address + 2);
   return disp >= -128 && disp <= 127;
}

static int choose_initial_emit_mode(const insn_info_t *insn, emit_mode_t *out_mode, const char **why)
{
   addr_mode_t mode;
   unsigned char dummy;
   int is_raw_opcode;

   mode = normalize_mode(insn->opcode, insn->mode);
   is_raw_opcode = opcode_parse_raw_byte(insn->opcode, &dummy);

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
         if (insn_is_long_branch_candidate(insn, *out_mode))
            *out_mode = EM_REL_LONG;
         return 1;

      case AM_ZP_OR_ABS:
         if (is_raw_opcode) {
            if (why)
               *why = "raw opcodes need an explicit mode suffix (.z/.a/.i) for ambiguous operand shapes";
            return 0;
         }
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
         if (is_raw_opcode) {
            if (why)
               *why = "raw opcodes need an explicit mode suffix (.zx/.ax) for indexed ambiguous operand shapes";
            return 0;
         }
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
         if (is_raw_opcode) {
            if (why)
               *why = "raw opcodes need an explicit mode suffix (.zy/.ay) for indexed ambiguous operand shapes";
            return 0;
         }
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

static int insn_size_from_mode(emit_mode_t mode)
{
   return emit_mode_size(mode);
}

static int eval_or_report(asm_context_t *ctx,
                          const expr_t *expr,
                          const symtab_t *symbols,
                          const char *scope,
                          const char *file_scope,
                          long pc,
                          long *value,
                          const stmt_t *stmt)
{
   expr_eval_status_t rc;

   rc = expr_eval(expr, symbols, scope, file_scope, pc, value);
   if (rc == EXPR_EVAL_OK)
      return 0;

   if (rc == EXPR_EVAL_DIVZERO) {
      asm_error(ctx, stmt, "divide by zero in expression");
   } else {
      fprintf(stderr, "%s:%d: ", stmt->file ? stmt->file : "<input>", stmt->line);
      expr_fprint(stderr, expr);
      fprintf(stderr, " -> unresolved expression\n");
      ctx->error_count++;
   }

   return 1;
}

static void segment_advance(asm_context_t *ctx, asm_segment_t *seg, const stmt_t *stmt, long amount)
{
   if (amount < 0) {
      asm_error(ctx, stmt, "negative segment advance");
      return;
   }

   seg->pc += amount;

   if (seg->size >= 0 && seg->pc > seg->size && !seg->overflow_warned) {
      asm_warning(stmt,
                  "segment '%s' overflowed: used $%lX bytes, declared size $%lX",
                  seg->name, seg->pc, seg->size);
      seg->overflow_warned = 1;
   }
}

typedef struct asm_pass_stats {
   int insn_count;
   int dir_count;
   int label_count;
   int const_count;
   int total_bytes;
   int zp_like;
   int abs_like;
   int long_count;
   int long_relax_count;
   int error_count;
} asm_pass_stats_t;

static void collect_pass_stats(const asm_context_t *ctx, asm_pass_stats_t *stats)
{
   const stmt_t *stmt;

   memset(stats, 0, sizeof(*stats));

   for (stmt = ctx->prog->head; stmt; stmt = stmt->next) {
      switch (stmt->kind) {
         case STMT_LABEL:
            stats->label_count++;
            break;

         case STMT_DIR:
            stats->dir_count++;
            break;

         case STMT_CONST:
            stats->const_count++;
            break;

         case STMT_INSN:
            stats->insn_count++;
            stats->total_bytes += stmt->u.insn.size;

            switch (stmt->u.insn.final_mode) {
               case EM_ZP:
               case EM_ZPX:
               case EM_ZPY:
                  stats->zp_like++;
                  break;

               case EM_ABS:
               case EM_ABSX:
               case EM_ABSY:
                  stats->abs_like++;
                  break;

               case EM_REL_LONG:
                  stats->long_count++;
                  if (insn_can_relax_long_branch(stmt, ctx))
                     stats->long_relax_count++;
                  break;

               default:
                  break;
            }
            break;
      }
   }

   stats->error_count = ctx->error_count;
}

static const char *describe_pass_phase(const char *phase)
{
   if (!strcmp(phase, "layout"))
      return "layout";
   if (!strcmp(phase, "relaxed"))
      return "after relaxation";
   if (!strcmp(phase, "stable"))
      return "stable";
   if (!strcmp(phase, "emit"))
      return "final emission";
   return phase;
}

static void format_delta(char *buf, size_t buf_size, int current, int previous, int have_previous)
{
   int delta;

   if (!have_previous) {
      buf[0] = '\0';
      return;
   }

   delta = current - previous;
   if (delta == 0) {
      buf[0] = '\0';
      return;
   }

   snprintf(buf, buf_size, " (%+d)", delta);
}

static void print_pass_stats(const asm_context_t *ctx, int pass_index, const char *phase)
{
   static int have_previous = 0;
   static asm_pass_stats_t previous;
   asm_pass_stats_t current;
   const char *label;
   char bytes_delta[32];
   char insn_delta[32];
   char dir_delta[32];
   char label_delta[32];
   char const_delta[32];
   char zp_delta[32];
   char abs_delta[32];
   char long_delta[32];
   char relax_delta[32];
   char error_delta[32];

   collect_pass_stats(ctx, &current);

   if (pass_index == 1 && !strcmp(phase, "layout"))
      have_previous = 0;

   label = describe_pass_phase(phase);
   format_delta(bytes_delta, sizeof(bytes_delta), current.total_bytes, previous.total_bytes, have_previous);
   format_delta(insn_delta, sizeof(insn_delta), current.insn_count, previous.insn_count, have_previous);
   format_delta(dir_delta, sizeof(dir_delta), current.dir_count, previous.dir_count, have_previous);
   format_delta(label_delta, sizeof(label_delta), current.label_count, previous.label_count, have_previous);
   format_delta(const_delta, sizeof(const_delta), current.const_count, previous.const_count, have_previous);
   format_delta(zp_delta, sizeof(zp_delta), current.zp_like, previous.zp_like, have_previous);
   format_delta(abs_delta, sizeof(abs_delta), current.abs_like, previous.abs_like, have_previous);
   format_delta(long_delta, sizeof(long_delta), current.long_count, previous.long_count, have_previous);
   format_delta(relax_delta, sizeof(relax_delta), current.long_relax_count, previous.long_relax_count, have_previous);
   format_delta(error_delta, sizeof(error_delta), current.error_count, previous.error_count, have_previous);

   printf("pass %03d %s: bytes %d%s, instructions %d%s, directives %d%s, labels %d%s, constants %d%s, zero-page %d%s, absolute %d%s, long branches %d%s, still relaxable %d%s, errors %d%s\n",
         pass_index,
         label,
         current.total_bytes, bytes_delta,
         current.insn_count, insn_delta,
         current.dir_count, dir_delta,
         current.label_count, label_delta,
         current.const_count, const_delta,
         current.zp_like, zp_delta,
         current.abs_like, abs_delta,
         current.long_count, long_delta,
         current.long_relax_count, relax_delta,
         current.error_count, error_delta);

   previous = current;
   have_previous = 1;
}

void asm_context_init(asm_context_t *ctx, program_ir_t *prog, listing_writer_t *listing, int object_mode_o65)
{
   stmt_t *stmt;
   const char *why;

   ctx->prog = prog;
   ctx->origin = 0;
   symtab_init(&ctx->symbols);
   ihex_image_init(&ctx->image);
   ctx->listing = listing;
   ctx->error_count = 0;
   ctx->object_mode_o65 = object_mode_o65;
   ctx->imports = NULL;
   ctx->weaks = NULL;
   ctx->segments = NULL;

   assign_segments(prog);
   assign_scopes(prog);

   gather_segment_uses(ctx);
   ensure_default_segment(ctx);

   for (stmt = prog->head; stmt; stmt = stmt->next) {
      if (stmt->kind != STMT_INSN)
         continue;

      if (!choose_initial_emit_mode(&stmt->u.insn, &stmt->u.insn.final_mode, &why)) {
         asm_error(ctx, stmt, "%s%s ... %s",
                   stmt->u.insn.opcode,
                   mode_spec_suffix(stmt->u.insn.spec),
                   why);
         stmt->u.insn.final_mode = EM_IMPLIED;
         stmt->u.insn.size = 1;
      } else {
         stmt->u.insn.size = insn_size_from_mode(stmt->u.insn.final_mode);
      }
   }
}

void asm_context_free(asm_context_t *ctx)
{
   symtab_free(&ctx->symbols);
   free_imports(ctx->imports);
   free_weaks(ctx->weaks);
   ctx->imports = NULL;
   free_segments(ctx->segments);
   ctx->segments = NULL;
}

static int declare_symbol_or_report(asm_context_t *ctx, const char *name, const stmt_t *stmt)
{
   symbol_t *sym;
   const symbol_t *prev;
   char *owned;
   const char *lookup_name;

   lookup_name = symbol_storage_name(ctx->prog, stmt, name, &owned);

   sym = symtab_declare(&ctx->symbols, lookup_name, stmt->file, stmt->line);
   if (sym) {
      free(owned);
      return 1;
   }

   prev = symtab_find_const(&ctx->symbols, lookup_name);
   asm_error(ctx, stmt, "duplicate symbol '%s'", name);
   if (prev && prev->def_file) {
      fprintf(stderr, "%s:%d: first defined here\n",
              prev->def_file, prev->def_line);
   }

   free(owned);
   return 0;
}

static symbol_t *find_declared_symbol(symtab_t *tab, const program_ir_t *prog, const stmt_t *stmt, const char *name)
{
   char *owned;
   symbol_t *sym;
   const char *lookup_name;

   lookup_name = symbol_storage_name(prog, stmt, name, &owned);
   sym = symtab_find(tab, lookup_name);
   free(owned);
   return sym;
}

static void gather_imports(asm_context_t *ctx)
{
   stmt_t *stmt;
   const expr_list_node_t *node;

   free_imports(ctx->imports);
   free_weaks(ctx->weaks);
   ctx->imports = NULL;
   ctx->weaks = NULL;

   for (stmt = ctx->prog->head; stmt; stmt = stmt->next) {
      if (stmt->kind != STMT_DIR)
         continue;

      if (!strcmp(stmt->u.dir->name, ".weak")) {
         for (node = stmt->u.dir->exprs; node; node = node->next) {
            const char *name;
            if (!directive_expr_is_ident(node->expr, &name)) {
               asm_error(ctx, stmt, ".weak expects identifier names");
               continue;
            }
            if (is_local_name(name)) {
               asm_error(ctx, stmt, ".weak does not allow local labels");
               continue;
            }
            asm_add_weak(ctx, stmt, name);
         }
         continue;
      }

      if (!directive_is_import_family(stmt->u.dir->name))
         continue;

      for (node = stmt->u.dir->exprs; node; node = node->next) {
         const char *name;
         int want_zp = directive_name_implies_zp(stmt->u.dir->name);

         if (!directive_expr_is_ident(node->expr, &name)) {
            asm_error(ctx, stmt, "%s expects identifier names", stmt->u.dir->name);
            continue;
         }
         if (is_local_name(name)) {
            asm_error(ctx, stmt, "%s does not allow local labels", stmt->u.dir->name);
            continue;
         }
         add_import(ctx, stmt, name, want_zp);
      }
   }
}

static void validate_imports(asm_context_t *ctx)
{
   import_name_t *p;
   const symbol_t *sym;

   if (ctx->object_mode_o65)
      return;

   for (p = ctx->imports; p; p = p->next) {
      sym = symtab_find_const(&ctx->symbols, p->name);
      if (!sym || !sym->defined) {
         ctx->error_count++;
         fprintf(stderr, "%s:%d: imported symbol '%s' was not resolved\n",
                 p->file ? p->file : "<input>", p->line, p->name);
      }
   }
}

static int resolve_constants(asm_context_t *ctx)
{
   int iter;
   int changed;
   stmt_t *stmt;

   for (iter = 0; iter < 64; iter++) {
      changed = 0;

      for (stmt = ctx->prog->head; stmt; stmt = stmt->next) {
         const symbol_t *sym;
         long value;
         symbol_t *mut;

         if (stmt->kind != STMT_CONST)
            continue;

         if (expr_eval(stmt->u.cnst.expr, &ctx->symbols, stmt->scope, stmt->file, stmt->address, &value) != EXPR_EVAL_OK)
            continue;

         sym = find_declared_symbol(&ctx->symbols, ctx->prog, stmt, stmt->u.cnst.name);
         if (!sym)
            continue;

         if (!sym->defined || sym->value != value) {
            mut = find_declared_symbol(&ctx->symbols, ctx->prog, stmt, stmt->u.cnst.name);
            symtab_set_value_segment(mut, value, O65_SEG_ABS);
            changed = 1;
         }
      }

      if (!changed)
         break;
   }

   for (stmt = ctx->prog->head; stmt; stmt = stmt->next) {
      const symbol_t *sym;

      if (stmt->kind != STMT_CONST)
         continue;

      sym = find_declared_symbol(&ctx->symbols, ctx->prog, stmt, stmt->u.cnst.name);
      if (!sym || !sym->defined) {
         eval_or_report(ctx, stmt->u.cnst.expr, &ctx->symbols, stmt->scope, stmt->file, stmt->address, &stmt->address, stmt);
      }
   }

   return 0;
}

int asm_pass1(asm_context_t *ctx, int pass_index)
{
   stmt_t *stmt;

   symtab_free(&ctx->symbols);
   symtab_init(&ctx->symbols);

   gather_imports(ctx);
   reset_segment_pcs(ctx);
   publish_segment_symbols(ctx);
   if (ctx->object_mode_o65) {
      stmt_t *wstmt;
      for (wstmt = ctx->prog->head; wstmt; wstmt = wstmt->next) {
         if (wstmt->kind != STMT_DIR || !wstmt->u.dir)
            continue;
         if (!strcmp(wstmt->u.dir->name, ".segmentdef")) {
            asm_warning(wstmt, ".segmentdef is ignored when writing o65 object files");
         } else if (!strcmp(wstmt->u.dir->name, ".org")) {
            asm_warning(wstmt, ".org in o65 object mode changes the relative offset within the segment; no absolute placement is recorded");
         }
      }
   } else {
      gather_segment_defs(ctx);
      validate_segment_defs(ctx);
   }

   for (stmt = ctx->prog->head; stmt; stmt = stmt->next) {
      asm_segment_t *seg;
      long pc_abs;
      symbol_t *sym;

      seg = segment_find(ctx, stmt->segment ? stmt->segment : DEFAULT_SEGMENT_NAME);
      if (!seg) {
         asm_error(ctx, stmt, "unknown segment '%s'", stmt->segment ? stmt->segment : DEFAULT_SEGMENT_NAME);
         continue;
      }

      pc_abs = seg->base + seg->pc;
      stmt->address = pc_abs;

      if (stmt->label) {
         if (declare_symbol_or_report(ctx, stmt->label, stmt)) {
            sym = find_declared_symbol(&ctx->symbols, ctx->prog, stmt, stmt->label);
            symtab_set_value_segment_named(sym, pc_abs,
                                      segment_name_to_o65(stmt->segment ? stmt->segment : DEFAULT_SEGMENT_NAME),
                                      stmt->segment ? stmt->segment : DEFAULT_SEGMENT_NAME);
         }
      }

      if (stmt->kind == STMT_DIR && stmt->u.dir && !strcmp(stmt->u.dir->name, ".proc")) {
         const char *proc_name = proc_decl_name(stmt);
         if (!proc_name) {
            asm_error(ctx, stmt, ".proc expects exactly one identifier name");
         } else if (declare_symbol_or_report(ctx, proc_name, stmt)) {
            sym = find_declared_symbol(&ctx->symbols, ctx->prog, stmt, proc_name);
            symtab_set_value_segment_named(sym, pc_abs,
                                      segment_name_to_o65(stmt->segment ? stmt->segment : DEFAULT_SEGMENT_NAME),
                                      stmt->segment ? stmt->segment : DEFAULT_SEGMENT_NAME);
         }
      }

      switch (stmt->kind) {
         case STMT_LABEL:
            break;

         case STMT_CONST:
            declare_symbol_or_report(ctx, stmt->u.cnst.name, stmt);
            break;

         case STMT_INSN:
            segment_advance(ctx, seg, stmt, stmt->u.insn.size);
            break;

         case STMT_DIR:
            if (!strcmp(stmt->u.dir->name, ".segment") ||
                !strcmp(stmt->u.dir->name, ".segmentdef") ||
                !strcmp(stmt->u.dir->name, ".global") ||
                !strcmp(stmt->u.dir->name, ".export") ||
                !strcmp(stmt->u.dir->name, ".import") ||
                !strcmp(stmt->u.dir->name, ".globalzp") ||
                !strcmp(stmt->u.dir->name, ".exportzp") ||
                !strcmp(stmt->u.dir->name, ".importzp") ||
                !strcmp(stmt->u.dir->name, ".weak") ||
                !strcmp(stmt->u.dir->name, ".proc") ||
                !strcmp(stmt->u.dir->name, ".endproc")) {
               break;
            }

            if (!strcmp(stmt->u.dir->name, ".org")) {
               long new_abs;
               long new_pc;

               if (!stmt->u.dir->exprs || stmt->u.dir->exprs->next) {
                  asm_error(ctx, stmt, ".org expects exactly one expression");
                  break;
               }

               if (eval_or_report(ctx, stmt->u.dir->exprs->expr, &ctx->symbols, stmt->scope, stmt->file, pc_abs, &new_abs, stmt))
                  break;

               new_pc = new_abs - seg->base;
               if (new_pc < 0) {
                  asm_error(ctx, stmt, ".org address $%lX is below base of segment '%s' ($%lX)",
                            new_abs, seg->name, seg->base);
                  break;
               }

               seg->pc = new_pc;
               if (seg->size >= 0 && seg->pc > seg->size && !seg->overflow_warned) {
                  asm_warning(stmt,
                              "segment '%s' overflowed: used $%lX bytes, declared size $%lX",
                              seg->name, seg->pc, seg->size);
                  seg->overflow_warned = 1;
               }
               break;
            }

            if (!strcmp(stmt->u.dir->name, ".byte")) {
               int count = 0;
               const expr_list_node_t *node;

               for (node = stmt->u.dir->exprs; node; node = node->next)
                  count++;
               segment_advance(ctx, seg, stmt, count);
               break;
            }

            if (!strcmp(stmt->u.dir->name, ".word")) {
               int count = 0;
               const expr_list_node_t *node;

               for (node = stmt->u.dir->exprs; node; node = node->next)
                  count++;
               segment_advance(ctx, seg, stmt, count * 2);
               break;
            }

            if (!strcmp(stmt->u.dir->name, ".text") ||
                !strcmp(stmt->u.dir->name, ".ascii")) {
               int len = 0;

               if (stmt->u.dir->string && !decode_escaped_string(stmt->u.dir->string, NULL, 0, &len)) {
                  asm_error(ctx, stmt, "malformed quoted string");
                  break;
               }

               segment_advance(ctx, seg, stmt, len);
               break;
            }

            if (!strcmp(stmt->u.dir->name, ".asciiz")) {
               int len = 0;

               if (stmt->u.dir->string && !decode_escaped_string(stmt->u.dir->string, NULL, 0, &len)) {
                  asm_error(ctx, stmt, "malformed quoted string");
                  break;
               }

               segment_advance(ctx, seg, stmt, len + 1);
               break;
            }

            if (!strcmp(stmt->u.dir->name, ".res")) {
               long value;

               if (!stmt->u.dir->exprs || stmt->u.dir->exprs->next) {
                  asm_error(ctx, stmt, ".res expects exactly one expression");
                  break;
               }

               if (eval_or_report(ctx, stmt->u.dir->exprs->expr, &ctx->symbols, stmt->scope, stmt->file, pc_abs, &value, stmt))
                  break;

               if (value < 0) {
                  asm_error(ctx, stmt, ".res requires a non-negative size");
                  break;
               }

               segment_advance(ctx, seg, stmt, value);
               break;
            }

            break;
      }
   }

   snapshot_segment_used_sizes(ctx);
   publish_segment_symbols(ctx);
   resolve_constants(ctx);
   validate_imports(ctx);
   print_pass_stats(ctx, pass_index, "layout");
   return 0;
}


static int expr_is_imported_zp_reference(const asm_context_t *ctx, const expr_t *expr)
{
   if (!expr || expr->kind != EXPR_IDENT)
      return 0;

   return import_is_zp(ctx, expr->u.ident);
}

static int can_relax_to_zp_family(const insn_info_t *insn, emit_mode_t current_mode, emit_mode_t *relaxed_mode)
{
   unsigned char dummy;

   if (insn->spec != MODE_SPEC_NONE)
      return 0;

   switch (current_mode) {
      case EM_ABS:
         if (opcode_lookup(insn->opcode, EM_ZP, &dummy)) {
            *relaxed_mode = EM_ZP;
            return 1;
         }
         break;

      case EM_ABSX:
         if (opcode_lookup(insn->opcode, EM_ZPX, &dummy)) {
            *relaxed_mode = EM_ZPX;
            return 1;
         }
         break;

      case EM_ABSY:
         if (opcode_lookup(insn->opcode, EM_ZPY, &dummy)) {
            *relaxed_mode = EM_ZPY;
            return 1;
         }
         break;

      default:
         break;
   }

   return 0;
}

int asm_relax(asm_context_t *ctx)
{
   int iter;
   int changed;
   stmt_t *stmt;
   asm_segment_t *seg;

   for (iter = 1; iter <= 50; iter++) {
      long segment_signature_before = 0;
      long segment_signature_after = 0;

      for (seg = ctx->segments; seg; seg = seg->next) {
         segment_signature_before ^= seg->base;
         segment_signature_before ^= (seg->size << 1);
         segment_signature_before ^= (seg->used_size << 2);
      }

      asm_pass1(ctx, iter);

      for (seg = ctx->segments; seg; seg = seg->next) {
         segment_signature_after ^= seg->base;
         segment_signature_after ^= (seg->size << 1);
         segment_signature_after ^= (seg->used_size << 2);
      }

      changed = (segment_signature_before != segment_signature_after);

      for (stmt = ctx->prog->head; stmt; stmt = stmt->next) {
         long value;
         emit_mode_t candidate;

         if (stmt->kind != STMT_INSN)
            continue;

         if (stmt->u.insn.final_mode == EM_REL_LONG && insn_can_relax_long_branch(stmt, ctx)) {
            stmt->u.insn.final_mode = EM_REL;
            stmt->u.insn.size = insn_size_from_mode(EM_REL);
            changed = 1;
            continue;
         }

         if (!can_relax_to_zp_family(&stmt->u.insn, stmt->u.insn.final_mode, &candidate))
            continue;

         if (!stmt->u.insn.expr)
            continue;

         if (expr_eval(stmt->u.insn.expr, &ctx->symbols, stmt->scope, stmt->file, stmt->address, &value) != EXPR_EVAL_OK) {
            if (!expr_is_imported_zp_reference(ctx, stmt->u.insn.expr))
               continue;
         } else if (!expr_is_u8_value(value)) {
            continue;
         }

         if (candidate != stmt->u.insn.final_mode) {
            stmt->u.insn.final_mode = candidate;
            stmt->u.insn.size = insn_size_from_mode(candidate);
            changed = 1;
         }
      }

      print_pass_stats(ctx, iter, changed ? "relaxed" : "stable");

      if (!changed)
         break;
   }

   return ctx->error_count ? 1 : 0;
}

static int emit_byte(asm_context_t *ctx, long addr, unsigned char b, const stmt_t *stmt)
{
   if (!ihex_write_byte(&ctx->image, addr, b)) {
      asm_error(ctx, stmt, "output address out of range: $%lX", addr);
      return 0;
   }

   return 1;
}

static int emit_word(asm_context_t *ctx, long addr, unsigned short w, const stmt_t *stmt)
{
   if (!ihex_write_word(&ctx->image, addr, w)) {
      asm_error(ctx, stmt, "output address out of range: $%lX", addr);
      return 0;
   }

   return 1;
}

/* returns 0 success, -1 statement error */
static int directive_emit_pass2(asm_context_t *ctx,
                                const stmt_t *stmt,
                                const directive_info_t *dir)
{
   const expr_list_node_t *node;
   long value;
   long pc;
   long start_pc;
   unsigned char rec[256];
   int rec_count;

   start_pc = stmt->address;
   pc = stmt->address;
   rec_count = 0;

   if (!strcmp(dir->name, ".org") ||
       !strcmp(dir->name, ".segment") ||
       !strcmp(dir->name, ".segmentdef") ||
       !strcmp(dir->name, ".global") ||
       !strcmp(dir->name, ".export") ||
       !strcmp(dir->name, ".import") ||
       !strcmp(dir->name, ".globalzp") ||
       !strcmp(dir->name, ".exportzp") ||
       !strcmp(dir->name, ".importzp") ||
       !strcmp(dir->name, ".weak") ||
       !strcmp(dir->name, ".proc") ||
       !strcmp(dir->name, ".endproc")) {
      if (ctx->listing)
         listing_write_no_bytes(ctx->listing, stmt);
      return 0;
   }

   if (!strcmp(dir->name, ".byte")) {
      for (node = dir->exprs; node; node = node->next) {
         if (eval_or_report(ctx, node->expr, &ctx->symbols, stmt->scope, stmt->file, pc, &value, stmt))
            return -1;
         if (!emit_byte(ctx, pc, (unsigned char)(value & 0xFF), stmt))
            return -1;
         if (rec_count < (int)sizeof(rec))
            rec[rec_count++] = (unsigned char)(value & 0xFF);
         pc++;
      }
      if (ctx->listing)
         listing_write_record(ctx->listing, stmt, start_pc, rec, rec_count);
      return 0;
   }

   if (!strcmp(dir->name, ".word")) {
      for (node = dir->exprs; node; node = node->next) {
         if (eval_or_report(ctx, node->expr, &ctx->symbols, stmt->scope, stmt->file, pc, &value, stmt))
            return -1;
         if (!emit_word(ctx, pc, (unsigned short)(value & 0xFFFF), stmt))
            return -1;
         if (rec_count + 1 < (int)sizeof(rec)) {
            rec[rec_count++] = (unsigned char)(value & 0xFF);
            rec[rec_count++] = (unsigned char)((value >> 8) & 0xFF);
         }
         pc += 2;
      }
      if (ctx->listing)
         listing_write_record(ctx->listing, stmt, start_pc, rec, rec_count);
      return 0;
   }

   if (!strcmp(dir->name, ".text") || !strcmp(dir->name, ".ascii")) {
      unsigned char buf[256];
      int len;
      int i;

      if (!dir->string) {
         if (ctx->listing)
            listing_write_no_bytes(ctx->listing, stmt);
         return 0;
      }

      if (!decode_escaped_string(dir->string, buf, (int)sizeof(buf), &len)) {
         asm_error(ctx, stmt, "malformed quoted string");
         return -1;
      }

      for (i = 0; i < len; i++) {
         if (!emit_byte(ctx, pc, buf[i], stmt))
            return -1;
         if (rec_count < (int)sizeof(rec))
            rec[rec_count++] = buf[i];
         pc++;
      }

      if (ctx->listing)
         listing_write_record(ctx->listing, stmt, start_pc, rec, rec_count);
      return 0;
   }

   if (!strcmp(dir->name, ".asciiz")) {
      unsigned char buf[256];
      int len;
      int i;

      if (!dir->string) {
         if (!emit_byte(ctx, pc, 0x00, stmt))
            return -1;
         rec[rec_count++] = 0x00;
         if (ctx->listing)
            listing_write_record(ctx->listing, stmt, start_pc, rec, rec_count);
         return 0;
      }

      if (!decode_escaped_string(dir->string, buf, (int)sizeof(buf), &len)) {
         asm_error(ctx, stmt, "malformed quoted string");
         return -1;
      }

      for (i = 0; i < len; i++) {
         if (!emit_byte(ctx, pc, buf[i], stmt))
            return -1;
         if (rec_count < (int)sizeof(rec))
            rec[rec_count++] = buf[i];
         pc++;
      }

      if (!emit_byte(ctx, pc, 0x00, stmt))
         return -1;
      if (rec_count < (int)sizeof(rec))
         rec[rec_count++] = 0x00;

      if (ctx->listing)
         listing_write_record(ctx->listing, stmt, start_pc, rec, rec_count);
      return 0;
   }

   if (!strcmp(dir->name, ".res")) {
      long i;

      if (!dir->exprs || dir->exprs->next) {
         asm_error(ctx, stmt, ".res expects exactly one expression");
         return -1;
      }

      if (eval_or_report(ctx, dir->exprs->expr, &ctx->symbols, stmt->scope, stmt->file, pc, &value, stmt))
         return -1;

      if (value < 0) {
         asm_error(ctx, stmt, ".res requires a non-negative size");
         return -1;
      }

      for (i = 0; i < value; i++) {
         if (!emit_byte(ctx, pc, 0x00, stmt))
            return -1;
         if (rec_count < (int)sizeof(rec))
            rec[rec_count++] = 0x00;
         pc++;
      }

      if (ctx->listing)
         listing_write_record(ctx->listing, stmt, start_pc, rec, rec_count);
      return 0;
   }

   asm_error(ctx, stmt, "unhandled directive %s", dir->name);
   return -1;
}

/* returns 0 success, -1 statement error */
static int insn_emit_pass2(asm_context_t *ctx,
                           const stmt_t *stmt,
                           const insn_info_t *insn)
{
   long value;
   unsigned char opcode;
   unsigned char rec[8];
   int rec_count;
   long start_pc;
   long pc;
   emit_mode_t emode;

   emode = insn->final_mode;
   start_pc = stmt->address;
   pc = stmt->address;
   rec_count = 0;

   if (emode == EM_REL_LONG) {
      unsigned char inv_opcode;

      if (!opcode_invert_branch(insn->opcode, &inv_opcode)) {
         asm_error(ctx, stmt, "internal error: no inverse branch for %s", insn->opcode);
         return -1;
      }

      if (!emit_byte(ctx, pc, inv_opcode, stmt))
         return -1;
      rec[rec_count++] = inv_opcode;
      pc++;

      if (!emit_byte(ctx, pc, 0x03, stmt))
         return -1;
      rec[rec_count++] = 0x03;
      pc++;

      if (!opcode_lookup("JMP", EM_ABS, &opcode)) {
         asm_error(ctx, stmt, "internal error: missing JMP opcode");
         return -1;
      }

      if (!emit_byte(ctx, pc, opcode, stmt))
         return -1;
      rec[rec_count++] = opcode;
      pc++;
   } else {
      if (!opcode_lookup(insn->opcode, emode, &opcode)) {
         asm_error(ctx, stmt, "illegal addressing mode for %s%s",
                   insn->opcode, mode_spec_suffix(insn->spec));
         return -1;
      }

      if (!emit_byte(ctx, pc, opcode, stmt))
         return -1;
      rec[rec_count++] = opcode;
      pc++;
   }

   switch (emode) {
      case EM_IMPLIED:
      case EM_ACCUMULATOR:
         if (ctx->listing)
            listing_write_record(ctx->listing, stmt, start_pc, rec, rec_count);
         return 0;

      default:
         break;
   }

   if (!insn->has_operand || !insn->expr) {
      asm_error(ctx, stmt, "internal error: missing operand expression");
      return -1;
   }

   if (eval_or_report(ctx, insn->expr, &ctx->symbols, stmt->scope, stmt->file, pc, &value, stmt))
      return -1;

   switch (emode) {
      case EM_IMMEDIATE:
         if (!expr_is_s8_or_u8_value(value)) {
            asm_error(ctx, stmt, "%s%s immediate operand out of range: %ld",
                      insn->opcode, mode_spec_suffix(insn->spec), value);
            return -1;
         }
         break;

      case EM_ZP:
      case EM_ZPX:
      case EM_ZPY:
      case EM_INDX:
      case EM_INDY:
         if (!expr_is_u8_value(value)) {
            asm_error(ctx, stmt, "%s%s requires a zero-page operand, got $%lX",
                      insn->opcode, mode_spec_suffix(insn->spec), value & 0xFFFF);
            return -1;
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
         if (!emit_byte(ctx, pc, (unsigned char)(value & 0xFF), stmt))
            return -1;
         rec[rec_count++] = (unsigned char)(value & 0xFF);
         pc++;
         break;

      case EM_REL: {
         long disp;

         disp = value - (pc + 1);
         if (disp < -128 || disp > 127) {
            asm_error(ctx, stmt, "branch out of range");
            return -1;
         }

         if (!emit_byte(ctx, pc, (unsigned char)(disp & 0xFF), stmt))
            return -1;
         rec[rec_count++] = (unsigned char)(disp & 0xFF);
         pc++;
         break;
      }

      case EM_REL_LONG:
      case EM_ABS:
      case EM_ABSX:
      case EM_ABSY:
      case EM_IND:
         if (!emit_word(ctx, pc, (unsigned short)(value & 0xFFFF), stmt))
            return -1;
         rec[rec_count++] = (unsigned char)(value & 0xFF);
         rec[rec_count++] = (unsigned char)((value >> 8) & 0xFF);
         pc += 2;
         break;

      default:
         asm_error(ctx, stmt, "internal emitter error");
         return -1;
   }

   if (ctx->listing)
      listing_write_record(ctx->listing, stmt, start_pc, rec, rec_count);

   return 0;
}

static int cmp_segment_ptrs(const void *a, const void *b)
{
   const asm_segment_t *sa;
   const asm_segment_t *sb;

   sa = *(const asm_segment_t * const *)a;
   sb = *(const asm_segment_t * const *)b;

   if (sa->base < sb->base)
      return -1;
   if (sa->base > sb->base)
      return 1;
   return strcmp(sa->name, sb->name);
}

static int cmp_symbol_ptrs(const void *a, const void *b)
{
   const symbol_t *sa;
   const symbol_t *sb;

   sa = *(const symbol_t * const *)a;
   sb = *(const symbol_t * const *)b;

   if (sa->defined != sb->defined)
      return sb->defined - sa->defined;

   if (sa->defined) {
      if (sa->value < sb->value)
         return -1;
      if (sa->value > sb->value)
         return 1;
   }

   return strcmp(sa->name, sb->name);
}

int asm_write_map_file(FILE *fp, const asm_context_t *ctx)
{
   const asm_segment_t *seg;
   const symbol_t *sym;
   asm_segment_t **segv;
   symbol_t **symv;
   int segc;
   int symc;
   int i;

   if (!fp || !ctx)
      return 0;

   segc = 0;
   for (seg = ctx->segments; seg; seg = seg->next)
      segc++;

   symc = 0;
   for (sym = ctx->symbols.head; sym; sym = sym->next)
      symc++;

   segv = NULL;
   symv = NULL;

   if (segc > 0) {
      segv = (asm_segment_t **)malloc((size_t)segc * sizeof(*segv));
      if (!segv) {
         fprintf(stderr, "out of memory\n");
         return 0;
      }
   }

   if (symc > 0) {
      symv = (symbol_t **)malloc((size_t)symc * sizeof(*symv));
      if (!symv) {
         free(segv);
         fprintf(stderr, "out of memory\n");
         return 0;
      }
   }

   i = 0;
   for (seg = ctx->segments; seg; seg = seg->next)
      segv[i++] = (asm_segment_t *)seg;

   i = 0;
   for (sym = ctx->symbols.head; sym; sym = sym->next)
      symv[i++] = (symbol_t *)sym;

   if (segc > 1)
      qsort(segv, (size_t)segc, sizeof(*segv), cmp_segment_ptrs);
   if (symc > 1)
      qsort(symv, (size_t)symc, sizeof(*symv), cmp_symbol_ptrs);

   fprintf(fp, "SEGMENTS\n");
   fprintf(fp, "========\n\n");
   fprintf(fp, "%-20s %-10s %-10s %-10s %-10s\n",
           "NAME", "BASE", "SIZE", "END", "CAPACITY");

   for (i = 0; i < segc; i++) {
      long end_addr;

      end_addr = segv[i]->base + segv[i]->used_size;
      fprintf(fp, "%-20s $%08lX $%08lX $%08lX $%08lX\n",
              segv[i]->name,
              segv[i]->base & 0xFFFFFFFFL,
              segv[i]->used_size & 0xFFFFFFFFL,
              end_addr & 0xFFFFFFFFL,
              segv[i]->size & 0xFFFFFFFFL);
   }

   fprintf(fp, "\nSYMBOLS\n");
   fprintf(fp, "=======\n\n");
   fprintf(fp, "%-10s %s\n", "ADDRESS", "NAME");

   for (i = 0; i < symc; i++) {
      if (symv[i]->defined)
         fprintf(fp, "$%08lX %s\n", symv[i]->value & 0xFFFFFFFFL, symv[i]->name);
      else
         fprintf(fp, "???????? %s\n", symv[i]->name);
   }

   free(segv);
   free(symv);
   return 1;
}

int asm_pass2(asm_context_t *ctx)
{
   stmt_t *stmt;
   int rc;

   ihex_image_init(&ctx->image);
   print_pass_stats(ctx, 999, "emit");

   for (stmt = ctx->prog->head; stmt; stmt = stmt->next) {
      switch (stmt->kind) {
         case STMT_LABEL:
            if (ctx->listing)
               listing_write_no_bytes(ctx->listing, stmt);
            break;

         case STMT_CONST:
            if (ctx->listing)
               listing_write_no_bytes(ctx->listing, stmt);
            break;

         case STMT_DIR:
            rc = directive_emit_pass2(ctx, stmt, stmt->u.dir);
            (void)rc;
            break;

         case STMT_INSN:
            rc = insn_emit_pass2(ctx, stmt, &stmt->u.insn);
            (void)rc;
            break;
      }
   }

   return ctx->error_count ? 1 : 0;
}
