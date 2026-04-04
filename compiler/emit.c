#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include "emit.h"
#include "messages.h"
#include "xray.h"

typedef struct {
   char *text;
   char *trim;
   char *mnemonic;
   char *operand;
   char *label;
   bool is_generated;
   bool is_instruction;
   bool is_label_only;
   bool is_directive;
   bool is_blank_or_comment;
   bool keep;
   int size;
} PeepholeLine;

typedef struct {
   int total_before;
   int total_saved;
   int pass_bytes;
   int pass_removed;
   int pass_saved;
   int pass_dup_load;
   int pass_jump_next;
   int pass_branch_next;
} PeepholeStats;

static char *xstrndup_local(const char *s, size_t n) {
   char *ret = (char *) malloc(n + 1);
   memcpy(ret, s, n);
   ret[n] = '\0';
   return ret;
}

static bool is_compiler_zp_operand(const char *operand) {
   if (!operand || !*operand)
      return false;

   return !strcmp(operand, "arg0") ||
          !strcmp(operand, "arg1") ||
          !strcmp(operand, "fp") ||
          !strcmp(operand, "fp+1") ||
          !strcmp(operand, "sp") ||
          !strcmp(operand, "sp+1") ||
          !strcmp(operand, "ptr0") ||
          !strcmp(operand, "ptr0+1") ||
          !strcmp(operand, "ptr1") ||
          !strcmp(operand, "ptr1+1") ||
          !strcmp(operand, "ptr2") ||
          !strcmp(operand, "ptr2+1");
}

static bool operand_is_safe_load_value(const char *operand) {
   if (!operand || !*operand)
      return false;
   if (operand[0] == '#')
      return true;
   return is_compiler_zp_operand(operand);
}

static int instruction_size_for(const char *mnemonic, const char *operand) {
   if (!mnemonic || !*mnemonic)
      return 0;

   if (!strcmp(mnemonic, "rts") || !strcmp(mnemonic, "pha") || !strcmp(mnemonic, "pla") ||
       !strcmp(mnemonic, "clc") || !strcmp(mnemonic, "sec") || !strcmp(mnemonic, "iny")) {
      return 1;
   }

   if (!strcmp(mnemonic, "beq") || !strcmp(mnemonic, "bne")) {
      return 2;
   }

   if (!strcmp(mnemonic, "jmp") || !strcmp(mnemonic, "jsr")) {
      return 3;
   }

   if (!operand)
      return 0;

   if (operand[0] == '#')
      return 2;

   if (operand[0] == '(')
      return 2;

   if (is_compiler_zp_operand(operand))
      return 2;

   return 3;
}

static char *emit_sink_join(EmitSink *es) {
   size_t total = 0;
   char *buf;
   size_t off = 0;

   for (EmitPiece *ep = es->head; ep; ep = ep->next) {
      total += strlen(ep->txt);
   }

   buf = (char *) malloc(total + 1);
   for (EmitPiece *ep = es->head; ep; ep = ep->next) {
      size_t len = strlen(ep->txt);
      memcpy(buf + off, ep->txt, len);
      off += len;
   }
   buf[off] = '\0';
   return buf;
}

static void free_emit_sink_pieces(EmitSink *es) {
   EmitPiece *next;
   for (EmitPiece *ep = es->head; ep; ep = next) {
      next = ep->next;
      free((void *) ep->txt);
      free(ep);
   }
   es->head = es->tail = NULL;
}

static int split_lines(char *text, char ***out_lines) {
   char **lines = NULL;
   int count = 0;
   char *start = text;
   char *p = text;

   while (1) {
      if (*p == '\n' || *p == '\0') {
         lines = (char **) realloc(lines, sizeof(char *) * (count + 1));
         lines[count++] = xstrndup_local(start, (size_t) (p - start));
         if (*p == '\0')
            break;
         start = p + 1;
      }
      p++;
   }

   *out_lines = lines;
   return count;
}

static char *trim_in_place(char *s) {
   char *end;
   while (*s && isspace((unsigned char) *s))
      s++;
   end = s + strlen(s);
   while (end > s && isspace((unsigned char) end[-1]))
      *--end = '\0';
   return s;
}

static void parse_line(PeepholeLine *line) {
   char *p;
   char *comment;
   line->trim = trim_in_place(line->text);
   line->mnemonic = NULL;
   line->operand = NULL;
   line->label = NULL;
   line->is_generated = (line->text[0] == ' ' || line->text[0] == '\t');
   line->is_instruction = false;
   line->is_label_only = false;
   line->is_directive = false;
   line->is_blank_or_comment = false;
   line->keep = true;
   line->size = 0;

   if (!line->trim[0] || line->trim[0] == ';') {
      line->is_blank_or_comment = true;
      return;
   }

   if (!line->is_generated) {
      size_t len = strlen(line->trim);
      if (len > 0 && line->trim[len - 1] == ':') {
         line->is_label_only = true;
         line->label = xstrndup_local(line->trim, len - 1);
         return;
      }
      if (line->trim[0] == '.') {
         line->is_directive = true;
         return;
      }
      return;
   }

   if (line->trim[0] == '.') {
      line->is_directive = true;
      return;
   }

   p = line->trim;
   while (*p && isalpha((unsigned char) *p))
      p++;
   if (p == line->trim)
      return;

   line->mnemonic = xstrndup_local(line->trim, (size_t) (p - line->trim));
   for (char *q = line->mnemonic; *q; q++) {
      *q = (char) tolower((unsigned char) *q);
   }

   while (*p && isspace((unsigned char) *p))
      p++;
   comment = strchr(p, ';');
   if (comment)
      *comment = '\0';
   p = trim_in_place(p);
   if (*p)
      line->operand = strdup(p);

   line->is_instruction = true;
   line->size = instruction_size_for(line->mnemonic, line->operand);
}

static void free_lines(PeepholeLine *lines, int count) {
   for (int i = 0; i < count; i++) {
      free(lines[i].text);
      free(lines[i].mnemonic);
      free(lines[i].operand);
      free(lines[i].label);
   }
   free(lines);
}

static int next_kept_nonblank_index(PeepholeLine *lines, int count, int index) {
   for (int i = index + 1; i < count; i++) {
      if (!lines[i].keep)
         continue;
      if (lines[i].is_blank_or_comment)
         continue;
      return i;
   }
   return -1;
}

static void reset_reg_state(char **a, char **x, char **y) {
   free(*a); *a = NULL;
   free(*x); *x = NULL;
   free(*y); *y = NULL;
}

static void set_reg_state(char **slot, const char *value) {
   free(*slot);
   *slot = value ? strdup(value) : NULL;
}

static void log_rewrite(const char *kind, int index, const PeepholeLine *line, int saved) {
   if (get_xray(XRAY_DEBUG)) {
      debug("peephole:%s line=%d saved=%d :: %s", kind, index + 1, saved, line->trim ? line->trim : "");
   }
}

static bool same_text(const char *a, const char *b) {
   if (!a || !b)
      return false;
   return !strcmp(a, b);
}

static int run_peephole_pass(PeepholeLine *lines, int count, PeepholeStats *stats) {
   char *reg_a = NULL;
   char *reg_x = NULL;
   char *reg_y = NULL;
   int changed = 0;

   stats->pass_bytes = 0;
   stats->pass_removed = 0;
   stats->pass_saved = 0;
   stats->pass_dup_load = 0;
   stats->pass_jump_next = 0;
   stats->pass_branch_next = 0;

   for (int i = 0; i < count; i++) {
      PeepholeLine *line = &lines[i];

      if (!line->keep)
         continue;

      if (line->is_instruction)
         stats->pass_bytes += line->size;

      if (line->is_label_only || line->is_directive || !line->is_generated) {
         reset_reg_state(&reg_a, &reg_x, &reg_y);
         continue;
      }

      if (!line->is_instruction)
         continue;

      if ((!strcmp(line->mnemonic, "jmp") || !strcmp(line->mnemonic, "beq") || !strcmp(line->mnemonic, "bne")) && line->operand) {
         int j = next_kept_nonblank_index(lines, count, i);
         if (j >= 0 && lines[j].is_label_only && lines[j].label && !strcmp(lines[j].label, line->operand)) {
            line->keep = false;
            stats->pass_removed++;
            stats->pass_saved += line->size;
            stats->total_saved += line->size;
            if (!strcmp(line->mnemonic, "jmp")) {
               stats->pass_jump_next++;
               log_rewrite("jump_next", i, line, line->size);
            }
            else {
               stats->pass_branch_next++;
               log_rewrite("branch_next", i, line, line->size);
            }
            changed = 1;
            continue;
         }
      }

      if (!strcmp(line->mnemonic, "lda") && operand_is_safe_load_value(line->operand) && same_text(reg_a, line->operand)) {
         line->keep = false;
         stats->pass_removed++;
         stats->pass_saved += line->size;
         stats->total_saved += line->size;
         stats->pass_dup_load++;
         log_rewrite("dup_lda", i, line, line->size);
         changed = 1;
         continue;
      }
      if (!strcmp(line->mnemonic, "ldx") && operand_is_safe_load_value(line->operand) && same_text(reg_x, line->operand)) {
         line->keep = false;
         stats->pass_removed++;
         stats->pass_saved += line->size;
         stats->total_saved += line->size;
         stats->pass_dup_load++;
         log_rewrite("dup_ldx", i, line, line->size);
         changed = 1;
         continue;
      }
      if (!strcmp(line->mnemonic, "ldy") && operand_is_safe_load_value(line->operand) && same_text(reg_y, line->operand)) {
         line->keep = false;
         stats->pass_removed++;
         stats->pass_saved += line->size;
         stats->total_saved += line->size;
         stats->pass_dup_load++;
         log_rewrite("dup_ldy", i, line, line->size);
         changed = 1;
         continue;
      }

      if (!strcmp(line->mnemonic, "lda")) {
         if (operand_is_safe_load_value(line->operand))
            set_reg_state(&reg_a, line->operand);
         else
            set_reg_state(&reg_a, NULL);
      }
      else if (!strcmp(line->mnemonic, "ldx")) {
         if (operand_is_safe_load_value(line->operand))
            set_reg_state(&reg_x, line->operand);
         else
            set_reg_state(&reg_x, NULL);
      }
      else if (!strcmp(line->mnemonic, "ldy")) {
         if (operand_is_safe_load_value(line->operand))
            set_reg_state(&reg_y, line->operand);
         else
            set_reg_state(&reg_y, NULL);
      }
      else if (!strcmp(line->mnemonic, "sta") || !strcmp(line->mnemonic, "adc") || !strcmp(line->mnemonic, "and") ||
               !strcmp(line->mnemonic, "cmp") || !strcmp(line->mnemonic, "eor") || !strcmp(line->mnemonic, "ora") ||
               !strcmp(line->mnemonic, "sbc") || !strcmp(line->mnemonic, "pla")) {
         set_reg_state(&reg_a, NULL);
      }
      else if (!strcmp(line->mnemonic, "iny")) {
         set_reg_state(&reg_y, NULL);
      }
      else if (!strcmp(line->mnemonic, "pha")) {
         /* A unchanged */
      }
      else if (!strcmp(line->mnemonic, "jsr")) {
         reset_reg_state(&reg_a, &reg_x, &reg_y);
      }
      else if (!strcmp(line->mnemonic, "rts")) {
         reset_reg_state(&reg_a, &reg_x, &reg_y);
      }
   }

   reset_reg_state(&reg_a, &reg_x, &reg_y);
   return changed;
}

static int count_instruction_bytes(PeepholeLine *lines, int count) {
   int total = 0;
   for (int i = 0; i < count; i++) {
      if (lines[i].keep && lines[i].is_instruction)
         total += lines[i].size;
   }
   return total;
}

static int count_instructions(PeepholeLine *lines, int count) {
   int total = 0;
   for (int i = 0; i < count; i++) {
      if (lines[i].keep && lines[i].is_instruction)
         total++;
   }
   return total;
}

static void print_peephole_stats(int pass_index, const char *phase, PeepholeLine *lines, int count, const PeepholeStats *stats) {
   if (!get_xray(XRAY_PEEPHOLE))
      return;

   message("%03d %-10s bytes=%d insns=%d removed=%d saved=%d total_saved=%d dup_load=%d jump_next=%d branch_next=%d",
         pass_index,
         phase,
         count_instruction_bytes(lines, count),
         count_instructions(lines, count),
         stats->pass_removed,
         stats->pass_saved,
         stats->total_saved,
         stats->pass_dup_load,
         stats->pass_jump_next,
         stats->pass_branch_next);
}

void emit_peephole_optimize(EmitSink *es) {
   char *joined;
   char **raw_lines = NULL;
   PeepholeLine *lines;
   int count;
   PeepholeStats stats;
   int pass_index = 0;
   int changed;
   size_t total_len = 0;
   char *out;
   size_t off = 0;
   EmitPiece *piece;

   if (!es || !es->head)
      return;

   joined = emit_sink_join(es);
   count = split_lines(joined, &raw_lines);
   lines = (PeepholeLine *) calloc((size_t) count, sizeof(PeepholeLine));
   for (int i = 0; i < count; i++) {
      lines[i].text = raw_lines[i];
      parse_line(&lines[i]);
   }
   free(raw_lines);
   free(joined);

   memset(&stats, 0, sizeof(stats));
   stats.total_before = count_instruction_bytes(lines, count);

   do {
      pass_index++;
      changed = run_peephole_pass(lines, count, &stats);
      print_peephole_stats(pass_index, changed ? "peephole" : "stable", lines, count, &stats);
   } while (changed && pass_index < 20);

   for (int i = 0; i < count; i++) {
      if (!lines[i].keep)
         continue;
      total_len += strlen(lines[i].text) + 1;
   }
   out = (char *) malloc(total_len + 1);
   for (int i = 0; i < count; i++) {
      size_t len;
      if (!lines[i].keep)
         continue;
      len = strlen(lines[i].text);
      memcpy(out + off, lines[i].text, len);
      off += len;
      out[off++] = '\n';
   }
   out[off] = '\0';

   free_emit_sink_pieces(es);
   piece = (EmitPiece *) malloc(sizeof(EmitPiece));
   piece->txt = out;
   piece->next = NULL;
   es->head = es->tail = piece;

   free_lines(lines, count);
}

void emit(EmitSink *es, const char *fmt, ...) {
   int len;
   va_list args;

   va_start(args, fmt);
   len = vsnprintf(NULL, 0, fmt, args);
   va_end(args);

   EmitPiece *piece = (EmitPiece *) malloc (sizeof(EmitPiece));
   piece->txt = (char *) malloc(len + 1);

   va_start(args, fmt);
   vsprintf((char *) piece->txt, fmt, args);
   va_end(args);

   piece->next = NULL;
   if (es->head == NULL) {
      es->head = es->tail = piece;
   }
   else {
      es->tail->next = piece;
      es->tail = piece;
   }
}

void emit_print(EmitSink *es, FILE *out) {
   if (!out) {
      out = stdout;
   }

   for (EmitPiece *ep = es->head; ep; ep = ep->next) {
      fprintf(out, "%s", ep->txt);
   }
}
