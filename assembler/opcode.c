#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opcode.h"
#include "util.h"

typedef struct opcode_entry {
   char *mnemonic;
   emit_mode_t mode;
   unsigned char opcode;
   struct opcode_entry *next;
} opcode_entry_t;

static opcode_entry_t *g_opcodes;

static int hex_nibble(int ch)
{
   if (ch >= '0' && ch <= '9')
      return ch - '0';
   if (ch >= 'A' && ch <= 'F')
      return 10 + (ch - 'A');
   if (ch >= 'a' && ch <= 'f')
      return 10 + (ch - 'a');
   return -1;
}

static int ascii_tolower(int ch)
{
   if (ch >= 'A' && ch <= 'Z')
      return ch - 'A' + 'a';
   return ch;
}

static int ascii_toupper(int ch)
{
   if (ch >= 'a' && ch <= 'z')
      return ch - 'a' + 'A';
   return ch;
}

static int ascii_strcasecmp(const char *a, const char *b)
{
   while (*a && *b) {
      int ca = ascii_tolower((unsigned char)*a);
      int cb = ascii_tolower((unsigned char)*b);
      if (ca != cb)
         return ca - cb;
      ++a;
      ++b;
   }

   return ascii_tolower((unsigned char)*a) - ascii_tolower((unsigned char)*b);
}

static char *upper_copy_n(const char *s, size_t n)
{
   char *out;
   size_t i;

   out = (char *)malloc(n + 1);
   if (!out) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   for (i = 0; i < n; ++i)
      out[i] = (char)ascii_toupper((unsigned char)s[i]);
   out[n] = '\0';
   return out;
}

static opcode_entry_t *find_entry(const char *mnemonic, emit_mode_t mode)
{
   opcode_entry_t *e;

   for (e = g_opcodes; e; e = e->next) {
      if (e->mode == mode && !strcmp(e->mnemonic, mnemonic))
         return e;
   }

   return NULL;
}

static void opcode_add_mapping(const char *mnemonic, emit_mode_t mode, unsigned char opcode)
{
   opcode_entry_t *e;

   e = find_entry(mnemonic, mode);
   if (e) {
      e->opcode = opcode;
      return;
   }

   e = (opcode_entry_t *)calloc(1, sizeof(*e));
   if (!e) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   e->mnemonic = xstrdup(mnemonic);
   e->mode = mode;
   e->opcode = opcode;
   e->next = g_opcodes;
   g_opcodes = e;
}

static int parse_emit_mode_name(const char *text, emit_mode_t *mode_out)
{
   struct mode_name {
      const char *name;
      emit_mode_t mode;
   };
   static const struct mode_name names[] = {
      { "imp", EM_IMPLIED },
      { "implied", EM_IMPLIED },
      { "acc", EM_ACCUMULATOR },
      { "accumulator", EM_ACCUMULATOR },
      { "imm", EM_IMMEDIATE },
      { "immediate", EM_IMMEDIATE },
      { "zp", EM_ZP },
      { "zeropage", EM_ZP },
      { "zpx", EM_ZPX },
      { "zeropagex", EM_ZPX },
      { "zpy", EM_ZPY },
      { "zeropagey", EM_ZPY },
      { "abs", EM_ABS },
      { "absolute", EM_ABS },
      { "absx", EM_ABSX },
      { "absolutex", EM_ABSX },
      { "absy", EM_ABSY },
      { "absolutey", EM_ABSY },
      { "ind", EM_IND },
      { "indirect", EM_IND },
      { "indx", EM_INDX },
      { "indexed_indirect", EM_INDX },
      { "indirectx", EM_INDX },
      { "iyx", EM_INDX },
      { "indy", EM_INDY },
      { "indirect_indexed", EM_INDY },
      { "indirecty", EM_INDY },
      { "rel", EM_REL },
      { "relative", EM_REL },
      { NULL, 0 }
   };
   const struct mode_name *m;

   for (m = names; m->name; ++m) {
      if (ascii_strcasecmp(text, m->name) == 0) {
         *mode_out = m->mode;
         return 1;
      }
   }

   return 0;
}

static int parse_opcode_byte(const char *text, unsigned char *opcode_out)
{
   const char *p;
   char *end;
   unsigned long value;

   p = text;
   if (*p == '$')
      ++p;
   else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
      p += 2;

   if (!*p)
      return 0;

   value = strtoul(p, &end, 16);
   if (*end != '\0' || value > 0xFFUL)
      return 0;

   *opcode_out = (unsigned char)value;
   return 1;
}

static char *trim_left(char *s)
{
   while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
      ++s;
   return s;
}

static void trim_right(char *s)
{
   size_t n = strlen(s);

   while (n > 0) {
      unsigned char ch = (unsigned char)s[n - 1];
      if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
         break;
      s[n - 1] = '\0';
      --n;
   }
}

void opcode_registry_reset(void)
{
   opcode_registry_free();
   g_opcodes = NULL;
}

void opcode_registry_free(void)
{
   opcode_entry_t *e = g_opcodes;
   while (e) {
      opcode_entry_t *next = e->next;
      free(e->mnemonic);
      free(e);
      e = next;
   }
   g_opcodes = NULL;
}

int opcode_load_config_file(const char *path)
{
   FILE *fp;
   char linebuf[512];
   int lineno;

   fp = fopen(path, "r");
   if (!fp) {
      perror(path);
      return 0;
   }

   lineno = 0;
   while (fgets(linebuf, sizeof(linebuf), fp)) {
      char *line;
      char *mnemonic_tok;
      char *mode_tok;
      char *opcode_tok;
      char *extra_tok;
      emit_mode_t mode;
      unsigned char opcode;
      char *mnemonic_up;

      ++lineno;
      line = linebuf;

      for (char *p = line; *p; ++p) {
         if (*p == '#' || *p == ';') {
            *p = '\0';
            break;
         }
      }

      trim_right(line);
      line = trim_left(line);
      if (*line == '\0')
         continue;

      mnemonic_tok = strtok(line, " \t");
      mode_tok = strtok(NULL, " \t");
      opcode_tok = strtok(NULL, " \t");
      extra_tok = strtok(NULL, " \t");

      if (!mnemonic_tok || !mode_tok || !opcode_tok || extra_tok) {
         fprintf(stderr, "%s:%d: malformed opcode config line\n", path, lineno);
         fclose(fp);
         return 0;
      }

      if (!parse_emit_mode_name(mode_tok, &mode)) {
         fprintf(stderr, "%s:%d: unknown opcode mode '%s'\n", path, lineno, mode_tok);
         fclose(fp);
         return 0;
      }

      if (!parse_opcode_byte(opcode_tok, &opcode)) {
         fprintf(stderr, "%s:%d: invalid opcode byte '%s'\n", path, lineno, opcode_tok);
         fclose(fp);
         return 0;
      }

      mnemonic_up = upper_copy_n(mnemonic_tok, strlen(mnemonic_tok));
      opcode_add_mapping(mnemonic_up, mode, opcode);
      free(mnemonic_up);
   }

   fclose(fp);
   return 1;
}

int opcode_parse_raw_byte(const char *mnemonic, unsigned char *opcode_out)
{
   int hi;
   int lo;

   if (!mnemonic || strlen(mnemonic) != 4)
      return 0;

   if (mnemonic[0] != 'O' && mnemonic[0] != 'o')
      return 0;
   if (mnemonic[1] != 'P' && mnemonic[1] != 'p')
      return 0;

   hi = hex_nibble((unsigned char)mnemonic[2]);
   lo = hex_nibble((unsigned char)mnemonic[3]);
   if (hi < 0 || lo < 0)
      return 0;

   *opcode_out = (unsigned char)((hi << 4) | lo);
   return 1;
}

int opcode_raw_is_conditional_branch(unsigned char opcode)
{
   return opcode == 0x10 || opcode == 0x30 || opcode == 0x50 || opcode == 0x70 ||
          opcode == 0x90 || opcode == 0xB0 || opcode == 0xD0 || opcode == 0xF0;
}

int opcode_raw_is_accumulator_shorthand(unsigned char opcode)
{
   return opcode == 0x0A || opcode == 0x2A || opcode == 0x4A || opcode == 0x6A;
}

int opcode_mnemonic_known(const char *mnemonic)
{
   opcode_entry_t *e;
   unsigned char dummy;

   if (opcode_parse_raw_byte(mnemonic, &dummy))
      return 1;

   for (e = g_opcodes; e; e = e->next) {
      if (!strcmp(e->mnemonic, mnemonic))
         return 1;
   }

   return 0;
}

int opcode_token_is_mnemonic(const char *token)
{
   const char *dot;
   char *mnemonic;
   int result;

   if (!token)
      return 0;

   dot = strchr(token, '.');
   if (dot)
      mnemonic = upper_copy_n(token, (size_t)(dot - token));
   else
      mnemonic = upper_copy_n(token, strlen(token));

   result = opcode_mnemonic_known(mnemonic);
   free(mnemonic);
   return result;
}

int opcode_has_mode(const char *mnemonic, emit_mode_t mode)
{
   return find_entry(mnemonic, mode) != NULL;
}

int opcode_lookup(const char *mnemonic, emit_mode_t mode, unsigned char *opcode_out)
{
   opcode_entry_t *e;

   if (opcode_parse_raw_byte(mnemonic, opcode_out) && mode != EM_REL_LONG)
      return 1;

   e = find_entry(mnemonic, mode);
   if (!e)
      return 0;

   *opcode_out = e->opcode;
   return 1;
}

int opcode_is_conditional_branch(const char *mnemonic)
{
   return !strcmp(mnemonic, "BCC") || !strcmp(mnemonic, "BCS") ||
          !strcmp(mnemonic, "BEQ") || !strcmp(mnemonic, "BMI") ||
          !strcmp(mnemonic, "BNE") || !strcmp(mnemonic, "BPL") ||
          !strcmp(mnemonic, "BVC") || !strcmp(mnemonic, "BVS");
}

int opcode_invert_branch(const char *mnemonic, unsigned char *opcode_out)
{
   if (!strcmp(mnemonic, "BCC")) { *opcode_out = 0xB0; return 1; }
   if (!strcmp(mnemonic, "BCS")) { *opcode_out = 0x90; return 1; }
   if (!strcmp(mnemonic, "BEQ")) { *opcode_out = 0xD0; return 1; }
   if (!strcmp(mnemonic, "BMI")) { *opcode_out = 0x10; return 1; }
   if (!strcmp(mnemonic, "BNE")) { *opcode_out = 0xF0; return 1; }
   if (!strcmp(mnemonic, "BPL")) { *opcode_out = 0x30; return 1; }
   if (!strcmp(mnemonic, "BVC")) { *opcode_out = 0x70; return 1; }
   if (!strcmp(mnemonic, "BVS")) { *opcode_out = 0x50; return 1; }
   return 0;
}

int emit_mode_size(emit_mode_t mode)
{
   switch (mode) {
      case EM_IMPLIED:
      case EM_ACCUMULATOR:
         return 1;
      case EM_IMMEDIATE:
      case EM_ZP:
      case EM_ZPX:
      case EM_ZPY:
      case EM_INDX:
      case EM_INDY:
      case EM_REL:
         return 2;
      case EM_REL_LONG:
         return 5;
      case EM_ABS:
      case EM_ABSX:
      case EM_ABSY:
      case EM_IND:
         return 3;
   }

   return 0;
}
