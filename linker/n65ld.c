#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define NAR_MAGIC "NAR65\0\1"
#define NAR_MAGIC_SIZE 7

#define O65_SEG_UNDEF 0
#define O65_SEG_ABS   1
#define O65_SEG_TEXT  2
#define O65_SEG_DATA  3
#define O65_SEG_BSS   4
#define O65_SEG_ZP    5

#define O65_RTYPE_LOW  0x20
#define O65_RTYPE_HIGH 0x40
#define O65_RTYPE_WORD 0x80

#define SYMBOL_BACKED_META_PREFIX "__sbpmeta$"

#define MAX_NAME 128
#define MAX_PATH 512

typedef struct {
   uint16_t start;
   uint16_t size;
   char type[8];
   int define_yes;
   char name[MAX_NAME];
} memory_region_t;

typedef struct {
   char name[MAX_NAME];
   char load_name[MAX_NAME];
   char run_name[MAX_NAME];
   char type[16];
   int define_yes;
} segment_rule_t;

typedef struct {
   memory_region_t mem[16];
   size_t mem_count;
   segment_rule_t seg[16];
   size_t seg_count;
} linker_config_t;

typedef struct {
   char *name;
   uint8_t segid;
   uint16_t value;
} symbol_t;

typedef struct {
   char *name;
   uint8_t segid;
   uint16_t packed_base;
   uint16_t size;
   uint16_t load_addr;
   uint16_t run_addr;
} object_layout_t;

typedef struct {
   uint32_t offset;
   uint8_t type;
   uint8_t segid;
   uint16_t undef_index;
   uint8_t aux_low;
   int has_aux_low;
} reloc_t;

typedef struct {
   uint8_t *data;
   size_t length;
   uint16_t base;
   reloc_t *relocs;
   size_t reloc_count;
} o65_segment_t;

typedef struct archive_member_s archive_member_t;

typedef struct {
   char origin[MAX_PATH];
   uint16_t mode;
   uint16_t tbase, dbase, bbase, zbase, stack;
   uint16_t blen, zlen;
   o65_segment_t text;
   o65_segment_t data;
   char **undefs;
   size_t undef_count;
   symbol_t *exports;
   size_t export_count;
   object_layout_t *layouts;
   size_t layout_count;
   uint16_t place_text_load;
   uint16_t place_data_load;
   uint16_t place_data_run;
   uint16_t place_bss_run;
   uint16_t place_zp_run;
   int selected_from_archive;
   int selected;
   int from_cmdline;
   archive_member_t *archive_member;
} object_file_t;

struct archive_member_s {
   char member_name[MAX_NAME];
   uint8_t *data;
   size_t size;
   int selected;
   object_file_t obj;
};

typedef struct {
   char path[MAX_PATH];
   archive_member_t *members;
   size_t member_count;
} archive_file_t;

typedef enum {
   INPUT_REF_OBJECT = 1,
   INPUT_REF_ARCHIVE = 2
} input_ref_kind_t;

typedef struct {
   input_ref_kind_t kind;
   size_t index;
} input_ref_t;

typedef struct {
   object_file_t *objects;
   size_t object_count;
   object_file_t *cmd_objects;
   size_t cmd_object_count;
   archive_file_t *archives;
   size_t archive_count;
   input_ref_t *order;
   size_t order_count;
} input_set_t;

typedef struct {
   char *name;
   uint16_t addr;
   uint8_t segid;
   const char *source;
} global_symbol_t;

typedef struct {
   char *name;
   int has_symbol_backed_params;
} call_graph_node_t;

typedef struct {
   int from;
   int to;
} call_graph_edge_t;

typedef struct {
   char name[MAX_NAME];
   uint16_t cur;
   uint32_t end;
} memory_cursor_t;

typedef struct {
   char *name;
   uint16_t load_addr;
   uint16_t run_addr;
   uint16_t size;
} copy_record_t;

typedef struct {
   char *name;
   uint16_t run_addr;
   uint16_t size;
} zero_record_t;

typedef struct {
   uint16_t code_load_cur;
   uint16_t data_load_cur;
   uint16_t data_run_cur;
   uint16_t bss_run_cur;
   uint16_t zp_run_cur;
   uint16_t code_load_end;
   uint16_t data_load_end;
   uint16_t data_run_end;
   uint16_t bss_run_end;
   uint16_t zp_run_end;
   uint16_t data_load_start;
   uint16_t data_load_size;
   uint16_t data_run_start;
   uint16_t data_run_size;
   uint16_t bss_start;
   uint16_t bss_size;
   uint16_t init_table_addr;
   uint16_t init_table_size;
   uint16_t copy_table_addr;
   uint16_t copy_table_size;
   uint16_t zero_table_addr;
   uint16_t zero_table_size;
   uint16_t stack_start;
   uint16_t stack_top;
   memory_cursor_t *cursors;
   size_t cursor_count;
   copy_record_t *copy_records;
   size_t copy_record_count;
   zero_record_t *zero_records;
   size_t zero_record_count;
   global_symbol_t *globals;
   size_t global_count;
} layout_t;

typedef struct {
   uint32_t value;
   size_t pos;
   int ok;
} parse_result_t;

typedef struct {
   const uint8_t *data;
   size_t size;
   size_t pos;
   const char *label;
} reader_t;

static void usage(FILE *fp)
{
   fprintf(fp,
      "Usage:\n"
      "  n65ld [options] file...\n"
      "\n"
      "Options:\n"
      "  -o FILE              Write Intel HEX output to FILE (default: a.hex)\n"
      "  -T FILE              Use FILE as linker script/config\n"
      "  --script=FILE        Same as -T FILE\n"
      "  -Map FILE            Write linker map to FILE\n"
      "  -Map=FILE            Same as -Map FILE\n"
      "  -h, --help           Show this help text\n"
      "  -v, --version        Show linker version\n"
      "\n"
      "Compatibility:\n"
      "  n65ld [layout.cfg] input1.o65 [input2.o65 ... inputN.a65] output.hex [output.map]\n");
}

static int ends_with(const char *s, const char *suffix)
{
   size_t slen = strlen(s);
   size_t tlen = strlen(suffix);
   if (slen < tlen)
      return 0;
   return strcmp(s + slen - tlen, suffix) == 0;
}

static int str_ieq(const char *a, const char *b)
{
   while (*a && *b) {
      int ca = toupper((unsigned char)*a++);
      int cb = toupper((unsigned char)*b++);
      if (ca != cb)
         return 0;
   }
   return *a == '\0' && *b == '\0';
}

static void *xmalloc(size_t size);

static char *xstrdup(const char *s)
{
   size_t n = strlen(s) + 1;
   char *p = (char *)malloc(n);
   if (!p) {
      fprintf(stderr, "n65ld: out of memory\n");
      exit(1);
   }
   memcpy(p, s, n);
   return p;
}

static int symbol_backed_metadata_has_prefix(const char *name)
{
   return name && strncmp(name, SYMBOL_BACKED_META_PREFIX, sizeof(SYMBOL_BACKED_META_PREFIX) - 1) == 0;
}

static int symbol_backed_metadata_parse_function(const char *name, const char **sym_out)
{
   const char *p;

   if (!symbol_backed_metadata_has_prefix(name))
      return 0;
   p = name + sizeof(SYMBOL_BACKED_META_PREFIX) - 1;
   if (strncmp(p, "F$", 2) != 0)
      return 0;
   p += 2;
   if (!*p)
      return 0;
   if (strchr(p, '$'))
      return 0;
   if (sym_out)
      *sym_out = p;
   return 1;
}

static int symbol_backed_metadata_parse_edge(const char *name, char **caller_out, char **callee_out)
{
   const char *p;
   const char *sep;
   size_t caller_len;

   if (!symbol_backed_metadata_has_prefix(name))
      return 0;
   p = name + sizeof(SYMBOL_BACKED_META_PREFIX) - 1;
   if (strncmp(p, "E$", 2) != 0)
      return 0;
   p += 2;
   sep = strchr(p, '$');
   if (!sep || sep == p || !sep[1])
      return 0;
   if (strchr(sep + 1, '$'))
      return 0;
   caller_len = (size_t)(sep - p);
   if (caller_out) {
      *caller_out = (char *)xmalloc(caller_len + 1);
      memcpy(*caller_out, p, caller_len);
      (*caller_out)[caller_len] = '\0';
   }
   if (callee_out)
      *callee_out = xstrdup(sep + 1);
   return 1;
}

static void *xmalloc(size_t size)
{
   void *p = malloc(size ? size : 1);
   if (!p) {
      fprintf(stderr, "n65ld: out of memory\n");
      exit(1);
   }
   return p;
}

static char *make_weak_name(const char *name)
{
   size_t n = strlen(name);
   char *out = (char *)xmalloc(n + 8);
   memcpy(out, "__weak_", 7);
   memcpy(out + 7, name, n + 1);
   return out;
}

static void *xcalloc(size_t count, size_t size)
{
   void *p = calloc(count ? count : 1, size ? size : 1);
   if (!p) {
      fprintf(stderr, "n65ld: out of memory\n");
      exit(1);
   }
   return p;
}

static void *xrealloc(void *ptr, size_t size)
{
   void *p = realloc(ptr, size ? size : 1);
   if (!p) {
      fprintf(stderr, "n65ld: out of memory\n");
      exit(1);
   }
   return p;
}

static uint8_t *read_entire_file(const char *path, size_t *size_out)
{
   FILE *fp;
   long size;
   uint8_t *buf;

   fp = fopen(path, "rb");
   if (!fp) {
      fprintf(stderr, "n65ld: cannot open '%s': %s\n", path, strerror(errno));
      exit(1);
   }

   if (fseek(fp, 0, SEEK_END) != 0) {
      fprintf(stderr, "n65ld: cannot seek '%s'\n", path);
      fclose(fp);
      exit(1);
   }
   size = ftell(fp);
   if (size < 0) {
      fprintf(stderr, "n65ld: cannot size '%s'\n", path);
      fclose(fp);
      exit(1);
   }
   if (fseek(fp, 0, SEEK_SET) != 0) {
      fprintf(stderr, "n65ld: cannot seek '%s'\n", path);
      fclose(fp);
      exit(1);
   }

   buf = (uint8_t *)xmalloc((size_t)size);
   if ((size_t)size && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
      fprintf(stderr, "n65ld: cannot read '%s'\n", path);
      fclose(fp);
      free(buf);
      exit(1);
   }

   fclose(fp);
   *size_out = (size_t)size;
   return buf;
}

static void reader_init(reader_t *r, const uint8_t *data, size_t size, const char *label)
{
   r->data = data;
   r->size = size;
   r->pos = 0;
   r->label = label;
}

static void reader_fail(const reader_t *r, const char *msg)
{
   fprintf(stderr, "n65ld: %s at offset 0x%zx in %s\n", msg, r->pos, r->label);
   exit(1);
}

static uint8_t rd_u8(reader_t *r)
{
   if (r->pos >= r->size)
      reader_fail(r, "unexpected EOF");
   return r->data[r->pos++];
}

static uint16_t rd_u16(reader_t *r)
{
   uint16_t lo = rd_u8(r);
   uint16_t hi = rd_u8(r);
   return (uint16_t)(lo | (hi << 8));
}

static void rd_bytes(reader_t *r, uint8_t *dst, size_t n)
{
   if (r->pos + n > r->size)
      reader_fail(r, "truncated data");
   memcpy(dst, r->data + r->pos, n);
   r->pos += n;
}

static char *rd_cstr(reader_t *r)
{
   size_t start = r->pos;
   while (r->pos < r->size && r->data[r->pos] != 0)
      r->pos++;
   if (r->pos >= r->size)
      reader_fail(r, "unterminated string");
   r->pos++;
   {
      size_t len = r->pos - start - 1;
      char *s = (char *)xmalloc(len + 1);
      memcpy(s, r->data + start, len);
      s[len] = '\0';
      return s;
   }
}

static parse_result_t parse_number(const char *s)
{
   parse_result_t r;
   char *end = NULL;

   while (isspace((unsigned char)*s))
      s++;

   r.ok = 0;
   r.value = 0;
   r.pos = 0;

   if (*s == '$') {
      r.value = strtoul(s + 1, &end, 16);
      if (end && end != s + 1)
         r.ok = 1;
   } else {
      r.value = strtoul(s, &end, 0);
      if (end && end != s)
         r.ok = 1;
   }

   if (r.ok)
      r.pos = (size_t)(end - s);
   return r;
}

static const memory_region_t *find_memory(const linker_config_t *cfg, const char *name)
{
   size_t i;
   for (i = 0; i < cfg->mem_count; ++i) {
      if (str_ieq(cfg->mem[i].name, name))
         return &cfg->mem[i];
   }
   return NULL;
}

static const segment_rule_t *find_segment_rule(const linker_config_t *cfg, const char *name)
{
   size_t i;
   for (i = 0; i < cfg->seg_count; ++i) {
      if (str_ieq(cfg->seg[i].name, name))
         return &cfg->seg[i];
   }
   return NULL;
}

static void init_default_config(linker_config_t *cfg)
{
   memset(cfg, 0, sizeof(*cfg));

   strcpy(cfg->mem[0].name, "ZP");
   cfg->mem[0].start = 0x0000;
   cfg->mem[0].size = 0x0100;
   strcpy(cfg->mem[0].type, "rw");
   cfg->mem[0].define_yes = 1;

   strcpy(cfg->mem[1].name, "CPUSTACK");
   cfg->mem[1].start = 0x0100;
   cfg->mem[1].size = 0x0100;
   strcpy(cfg->mem[1].type, "rw");
   cfg->mem[1].define_yes = 1;

   strcpy(cfg->mem[2].name, "RAM");
   cfg->mem[2].start = 0x0200;
   cfg->mem[2].size = 0x1E00;
   strcpy(cfg->mem[2].type, "rw");
   cfg->mem[2].define_yes = 1;

   strcpy(cfg->mem[3].name, "ROM");
   cfg->mem[3].start = 0x2000;
   cfg->mem[3].size = 0xE000;
   strcpy(cfg->mem[3].type, "ro");
   cfg->mem[3].define_yes = 1;
   cfg->mem_count = 4;

   strcpy(cfg->seg[0].name, "ZEROPAGE");
   strcpy(cfg->seg[0].load_name, "ROM");
   strcpy(cfg->seg[0].run_name, "ZP");
   strcpy(cfg->seg[0].type, "zp");
   cfg->seg[0].define_yes = 1;

   strcpy(cfg->seg[1].name, "CODE");
   strcpy(cfg->seg[1].load_name, "ROM");
   cfg->seg[1].run_name[0] = '\0';
   strcpy(cfg->seg[1].type, "ro");
   cfg->seg[1].define_yes = 1;

   strcpy(cfg->seg[2].name, "RODATA");
   strcpy(cfg->seg[2].load_name, "ROM");
   cfg->seg[2].run_name[0] = '\0';
   strcpy(cfg->seg[2].type, "ro");
   cfg->seg[2].define_yes = 1;

   strcpy(cfg->seg[3].name, "BSS");
   strcpy(cfg->seg[3].load_name, "RAM");
   cfg->seg[3].run_name[0] = '\0';
   strcpy(cfg->seg[3].type, "bss");
   cfg->seg[3].define_yes = 1;

   strcpy(cfg->seg[4].name, "DATA");
   strcpy(cfg->seg[4].load_name, "ROM");
   strcpy(cfg->seg[4].run_name, "RAM");
   strcpy(cfg->seg[4].type, "data");
   cfg->seg[4].define_yes = 1;
   cfg->seg_count = 5;
}

static char *trim(char *s)
{
   char *e;
   while (isspace((unsigned char)*s))
      s++;
   if (*s == '\0')
      return s;
   e = s + strlen(s) - 1;
   while (e > s && isspace((unsigned char)*e))
      *e-- = '\0';
   return s;
}

static void parse_memory_property(memory_region_t *mem, const char *key, const char *value)
{
   parse_result_t n;
   if (str_ieq(key, "start")) {
      n = parse_number(value);
      if (!n.ok || n.value > 0xFFFFu) {
         fprintf(stderr, "n65ld: bad memory start '%s'\n", value);
         exit(1);
      }
      mem->start = (uint16_t)n.value;
   } else if (str_ieq(key, "size")) {
      n = parse_number(value);
      if (!n.ok || n.value > 0xFFFFu) {
         fprintf(stderr, "n65ld: bad memory size '%s'\n", value);
         exit(1);
      }
      mem->size = (uint16_t)n.value;
   } else if (str_ieq(key, "type")) {
      snprintf(mem->type, sizeof(mem->type), "%s", trim((char *)value));
   } else if (str_ieq(key, "define")) {
      mem->define_yes = str_ieq(trim((char *)value), "yes");
   }
}

static void parse_segment_property(segment_rule_t *seg, const char *key, const char *value)
{
   value = trim((char *)value);
   if (str_ieq(key, "load")) {
      snprintf(seg->load_name, sizeof(seg->load_name), "%s", value);
   } else if (str_ieq(key, "run")) {
      snprintf(seg->run_name, sizeof(seg->run_name), "%s", value);
   } else if (str_ieq(key, "type")) {
      snprintf(seg->type, sizeof(seg->type), "%s", value);
   } else if (str_ieq(key, "define")) {
      seg->define_yes = str_ieq(value, "yes");
   }
}

static void parse_cfg_file(linker_config_t *cfg, const char *path)
{
   FILE *fp = fopen(path, "r");
   char line[1024];
   enum { NONE, MEMORY, SEGMENTS } block = NONE;

   if (!fp) {
      fprintf(stderr, "n65ld: cannot open '%s': %s\n", path, strerror(errno));
      exit(1);
   }

   memset(cfg, 0, sizeof(*cfg));

   while (fgets(line, sizeof(line), fp)) {
      char *s = line;
      char *brace;
      char *comment = strchr(s, '#');
      if (comment)
         *comment = '\0';
      s = trim(s);
      if (*s == '\0')
         continue;

      if (str_ieq(s, "MEMORY {") || str_ieq(s, "MEMORY{")) {
         block = MEMORY;
         continue;
      }
      if (str_ieq(s, "SEGMENTS {") || str_ieq(s, "SEGMENTS{")) {
         block = SEGMENTS;
         continue;
      }
      if (strcmp(s, "}") == 0) {
         block = NONE;
         continue;
      }
      if (block == NONE)
         continue;

      brace = strchr(s, ':');
      if (!brace)
         continue;
      *brace++ = '\0';
      s = trim(s);
      brace = trim(brace);
      {
         char *semi = strrchr(brace, ';');
         char *tok;
         if (semi)
            *semi = '\0';

         if (block == MEMORY) {
            memory_region_t *mem;
            if (cfg->mem_count >= ARRAY_LEN(cfg->mem)) {
               fprintf(stderr, "n65ld: too many MEMORY entries\n");
               exit(1);
            }
            mem = &cfg->mem[cfg->mem_count++];
            memset(mem, 0, sizeof(*mem));
            snprintf(mem->name, sizeof(mem->name), "%s", s);
            tok = strtok(brace, ",");
            while (tok) {
               char *eq = strchr(tok, '=');
               if (eq) {
                  *eq++ = '\0';
                  parse_memory_property(mem, trim(tok), trim(eq));
               }
               tok = strtok(NULL, ",");
            }
         } else {
            segment_rule_t *seg;
            if (cfg->seg_count >= ARRAY_LEN(cfg->seg)) {
               fprintf(stderr, "n65ld: too many SEGMENTS entries\n");
               exit(1);
            }
            seg = &cfg->seg[cfg->seg_count++];
            memset(seg, 0, sizeof(*seg));
            snprintf(seg->name, sizeof(seg->name), "%s", s);
            tok = strtok(brace, ",");
            while (tok) {
               char *eq = strchr(tok, '=');
               if (eq) {
                  *eq++ = '\0';
                  parse_segment_property(seg, trim(tok), trim(eq));
               }
               tok = strtok(NULL, ",");
            }
         }
      }
   }

   fclose(fp);
}




static int parse_reloc_table_old(reader_t *r, reloc_t **out, size_t *count_out)
{
   reloc_t *items = NULL;
   size_t count = 0;
   long prev = -1;

   for (;;) {
      uint8_t delta = rd_u8(r);
      if (delta == 0)
         break;
      if (delta == 255) {
         prev += 254;
         continue;
      }
      items = (reloc_t *)xrealloc(items, (count + 1) * sizeof(*items));
      memset(&items[count], 0, sizeof(items[count]));
      prev += delta;
      items[count].offset = (uint32_t)prev;
      items[count].type = rd_u8(r);
      items[count].segid = rd_u8(r);
      if (items[count].segid == O65_SEG_UNDEF)
         items[count].undef_index = rd_u16(r);
      count++;
   }

   *out = items;
   *count_out = count;
   return 1;
}

static int parse_exports(reader_t *r, symbol_t **out, size_t *count_out)
{
   size_t i;
   uint16_t count = rd_u16(r);
   symbol_t *items = (symbol_t *)xcalloc(count, sizeof(*items));
   for (i = 0; i < count; ++i) {
      items[i].name = rd_cstr(r);
      items[i].segid = rd_u8(r);
      items[i].value = rd_u16(r);
   }
   *out = items;
   *count_out = count;
   return 1;
}

static int parse_layouts(reader_t *r, object_layout_t **out, size_t *count_out)
{
   size_t i;
   uint16_t count = rd_u16(r);
   object_layout_t *items = (object_layout_t *)xcalloc(count, sizeof(*items));
   for (i = 0; i < count; ++i) {
      items[i].name = rd_cstr(r);
      items[i].segid = rd_u8(r);
      items[i].packed_base = rd_u16(r);
      items[i].size = rd_u16(r);
   }
   *out = items;
   *count_out = count;
   return 1;
}

static int parse_undefs(reader_t *r, char ***out, size_t *count_out)
{
   size_t i;
   uint16_t count = rd_u16(r);
   char **items = (char **)xcalloc(count, sizeof(*items));
   for (i = 0; i < count; ++i)
      items[i] = rd_cstr(r);
   *out = items;
   *count_out = count;
   return 1;
}

static void free_exports_array(symbol_t *items, size_t count)
{
   size_t i;
   for (i = 0; i < count; ++i)
      free(items[i].name);
   free(items);
}

static void free_layout_array(object_layout_t *items, size_t count)
{
   size_t i;
   for (i = 0; i < count; ++i)
      free(items[i].name);
   free(items);
}

static int try_parse_tail(const uint8_t *tail, size_t tail_size,
   reloc_t **text_relocs, size_t *text_reloc_count,
   reloc_t **data_relocs, size_t *data_reloc_count,
   symbol_t **exports, size_t *export_count,
   object_layout_t **layouts, size_t *layout_count,
   char ***undefs, size_t *undef_count,
   const char *label)
{
   reader_t r;
   size_t save;

   reader_init(&r, tail, tail_size, label);
   parse_undefs(&r, undefs, undef_count);
   save = r.pos;

   if (parse_reloc_table_old(&r, text_relocs, text_reloc_count) &&
         parse_reloc_table_old(&r, data_relocs, data_reloc_count) &&
         parse_exports(&r, exports, export_count)) {
      if (r.pos == r.size)
         return 1;
      if (parse_layouts(&r, layouts, layout_count) && r.pos == r.size)
         return 1;
   }

   free(*text_relocs); *text_relocs = NULL; *text_reloc_count = 0;
   free(*data_relocs); *data_relocs = NULL; *data_reloc_count = 0;
   free_exports_array(*exports, *export_count); *exports = NULL; *export_count = 0;
   free_layout_array(*layouts, *layout_count); *layouts = NULL; *layout_count = 0;

   r.pos = save;
   if (parse_reloc_table_old(&r, data_relocs, data_reloc_count) &&
         parse_reloc_table_old(&r, text_relocs, text_reloc_count) &&
         parse_exports(&r, exports, export_count)) {
      if (r.pos == r.size)
         return 1;
      if (parse_layouts(&r, layouts, layout_count) && r.pos == r.size)
         return 1;
   }

   free(*text_relocs); *text_relocs = NULL; *text_reloc_count = 0;
   free(*data_relocs); *data_relocs = NULL; *data_reloc_count = 0;
   free_exports_array(*exports, *export_count); *exports = NULL; *export_count = 0;
   free_layout_array(*layouts, *layout_count); *layouts = NULL; *layout_count = 0;
   return 0;
}

static void synthesize_default_layouts(object_file_t *obj)
{
   size_t count = 0;
   object_layout_t *items;

   if (obj->layout_count > 0)
      return;

   if (obj->text.length > 0)
      count++;
   if (obj->data.length > 0)
      count++;
   if (obj->blen > 0)
      count++;
   if (obj->zlen > 0)
      count++;

   items = (object_layout_t *)xcalloc(count ? count : 1, sizeof(*items));
   count = 0;
   if (obj->text.length > 0) {
      items[count].name = xstrdup("CODE");
      items[count].segid = O65_SEG_TEXT;
      items[count].packed_base = 0;
      items[count].size = (uint16_t)obj->text.length;
      count++;
   }
   if (obj->data.length > 0) {
      items[count].name = xstrdup("DATA");
      items[count].segid = O65_SEG_DATA;
      items[count].packed_base = 0;
      items[count].size = (uint16_t)obj->data.length;
      count++;
   }
   if (obj->blen > 0) {
      items[count].name = xstrdup("BSS");
      items[count].segid = O65_SEG_BSS;
      items[count].packed_base = 0;
      items[count].size = obj->blen;
      count++;
   }
   if (obj->zlen > 0) {
      items[count].name = xstrdup("ZEROPAGE");
      items[count].segid = O65_SEG_ZP;
      items[count].packed_base = 0;
      items[count].size = obj->zlen;
      count++;
   }

   obj->layouts = items;
   obj->layout_count = count;
}

static void parse_o65_object_from_memory(object_file_t *obj, const uint8_t *data, size_t size, const char *label)
{
   reader_t r;
   uint8_t header[5];
   uint8_t optlen;
   size_t header_end;
   symbol_t *exports = NULL;
   size_t export_count = 0;
   char **undefs = NULL;
   size_t undef_count = 0;
   reloc_t *text_relocs = NULL;
   size_t text_reloc_count = 0;
   reloc_t *data_relocs = NULL;
   size_t data_reloc_count = 0;
   object_layout_t *layouts = NULL;
   size_t layout_count = 0;

   memset(obj, 0, sizeof(*obj));
   snprintf(obj->origin, sizeof(obj->origin), "%s", label);

   reader_init(&r, data, size, label);
   rd_bytes(&r, header, sizeof(header));
   if (!(header[0] == 1 && header[1] == 0 && header[2] == 'o' && header[3] == '6' && header[4] == '5')) {
      fprintf(stderr, "n65ld: '%s' is not an o65 file\n", label);
      exit(1);
   }

   (void)rd_u8(&r); /* version */
   obj->mode = rd_u16(&r);
   obj->tbase = rd_u16(&r);
   obj->text.length = rd_u16(&r);
   obj->dbase = rd_u16(&r);
   obj->data.length = rd_u16(&r);
   obj->bbase = rd_u16(&r);
   obj->blen = rd_u16(&r);
   obj->zbase = rd_u16(&r);
   obj->zlen = rd_u16(&r);
   obj->stack = rd_u16(&r);

   for (;;) {
      optlen = rd_u8(&r);
      if (optlen == 0)
         break;
      if (optlen < 1 || r.pos + (size_t)optlen - 1 > r.size)
         reader_fail(&r, "bad o65 options block");
      r.pos += (size_t)optlen - 1;
   }

   header_end = r.pos;

   obj->text.data = (uint8_t *)xmalloc(obj->text.length);
   obj->data.data = (uint8_t *)xmalloc(obj->data.length);
   rd_bytes(&r, obj->text.data, obj->text.length);
   rd_bytes(&r, obj->data.data, obj->data.length);

   if (!try_parse_tail(data + r.pos, size - r.pos,
         &text_relocs, &text_reloc_count,
         &data_relocs, &data_reloc_count,
         &exports, &export_count,
         &layouts, &layout_count,
         &undefs, &undef_count,
         label)) {
      fprintf(stderr, "n65ld: failed to parse o65 relocation/export tail in '%s' (header ended at 0x%zx)\n", label, header_end);
      exit(1);
   }

   obj->text.relocs = text_relocs;
   obj->text.reloc_count = text_reloc_count;
   obj->data.relocs = data_relocs;
   obj->data.reloc_count = data_reloc_count;
   obj->undefs = undefs;
   obj->undef_count = undef_count;
   obj->exports = exports;
   obj->export_count = export_count;
   obj->layouts = layouts;
   obj->layout_count = layout_count;
   synthesize_default_layouts(obj);
}

static void load_archive(const char *path, archive_file_t *archive)
{
   reader_t r;
   size_t size;
   uint8_t *buf = read_entire_file(path, &size);
   uint8_t magic[NAR_MAGIC_SIZE];

   memset(archive, 0, sizeof(*archive));
   snprintf(archive->path, sizeof(archive->path), "%s", path);

   reader_init(&r, buf, size, path);
   rd_bytes(&r, magic, sizeof(magic));
   if (memcmp(magic, NAR_MAGIC, NAR_MAGIC_SIZE) != 0) {
      fprintf(stderr, "n65ld: '%s' is not an a65 archive created by n65ar\n", path);
      free(buf);
      exit(1);
   }

   while (r.pos < r.size) {
      uint16_t name_len;
      uint32_t member_size;
      archive_member_t *m;
      char member_label[MAX_PATH + MAX_NAME + 8];

      name_len = rd_u16(&r);
      member_size = (uint32_t)rd_u16(&r) | ((uint32_t)rd_u16(&r) << 16);
      archive->members = (archive_member_t *)xrealloc(archive->members,
         (archive->member_count + 1) * sizeof(*archive->members));
      m = &archive->members[archive->member_count++];
      memset(m, 0, sizeof(*m));
      if (name_len >= sizeof(m->member_name)) {
         fprintf(stderr, "n65ld: member name too long in '%s'\n", path);
         exit(1);
      }
      rd_bytes(&r, (uint8_t *)m->member_name, name_len);
      m->member_name[name_len] = '\0';
      if (r.pos + member_size > r.size)
         reader_fail(&r, "truncated archive member payload");
      m->data = (uint8_t *)xmalloc(member_size);
      memcpy(m->data, r.data + r.pos, member_size);
      m->size = member_size;
      r.pos += member_size;
      snprintf(member_label, sizeof(member_label), "%s(%s)", path, m->member_name);
      parse_o65_object_from_memory(&m->obj, m->data, m->size, member_label);
      m->obj.selected_from_archive = 1;
   }

   for (size_t i = 0; i < archive->member_count; ++i) {
      archive->members[i].obj.archive_member = &archive->members[i];
   }

   free(buf);
}

static void load_object(const char *path, object_file_t *obj)
{
   size_t size;
   uint8_t *buf = read_entire_file(path, &size);
   parse_o65_object_from_memory(obj, buf, size, path);
   free(buf);
}

static int object_exports_symbol(const object_file_t *obj, const char *name)
{
   size_t i;
   for (i = 0; i < obj->export_count; ++i) {
      if (strcmp(obj->exports[i].name, name) == 0)
         return 1;
   }
   return 0;
}

static int object_exports_symbol_or_weak(const object_file_t *obj, const char *name)
{
   char *weak = make_weak_name(name);
   int found = object_exports_symbol(obj, name) || object_exports_symbol(obj, weak);
   free(weak);
   return found;
}

static int symbol_in_list(char **items, size_t count, const char *name)
{
   size_t i;
   for (i = 0; i < count; ++i) {
      if (strcmp(items[i], name) == 0)
         return 1;
   }
   return 0;
}

static int symbol_is_init_function(const char *name)
{
   return strcmp(name, "__init") == 0 || strncmp(name, "__init_", 7) == 0;
}

static void add_unique_string(char ***items, size_t *count, const char *name)
{
   if (!symbol_in_list(*items, *count, name)) {
      *items = (char **)xrealloc(*items, (*count + 1) * sizeof(**items));
      (*items)[(*count)++] = xstrdup(name);
   }
}

static int selected_objects_export_symbol(const input_set_t *in, const char *name)
{
   size_t i;
   for (i = 0; i < in->object_count; ++i) {
      if (object_exports_symbol_or_weak(&in->objects[i], name))
         return 1;
   }
   return 0;
}

static void collect_needed_symbols(const input_set_t *in, char ***out, size_t *count_out)
{
   char **needed = NULL;
   size_t needed_count = 0;
   size_t i, j;

   add_unique_string(&needed, &needed_count, "__reset");
   add_unique_string(&needed, &needed_count, "__nmi");
   add_unique_string(&needed, &needed_count, "__irqbrk");

   for (i = 0; i < in->object_count; ++i) {
      for (j = 0; j < in->objects[i].undef_count; ++j)
         add_unique_string(&needed, &needed_count, in->objects[i].undefs[j]);
   }

   *out = needed;
   *count_out = needed_count;
}

static object_file_t *find_provider_in_archive(archive_file_t *arc, const char *symbol_name)
{
   size_t m;
   for (m = 0; m < arc->member_count; ++m) {
      archive_member_t *mem = &arc->members[m];
      if (mem->selected)
         continue;
      if (object_exports_symbol(&mem->obj, symbol_name))
         return &mem->obj;
   }
   return NULL;
}

static object_file_t *find_provider_in_object(object_file_t *obj, const char *symbol_name)
{
   if (obj->selected)
      return NULL;
   return object_exports_symbol(obj, symbol_name) ? obj : NULL;
}

static object_file_t *find_best_provider(input_set_t *in, const char *name)
{
   size_t i;
   char *weak = make_weak_name(name);
   object_file_t *provider = NULL;

   for (i = 0; i < in->order_count; ++i) {
      input_ref_t *ref = &in->order[i];
      if (ref->kind == INPUT_REF_OBJECT) {
         provider = find_provider_in_object(&in->cmd_objects[ref->index], name);
      } else {
         provider = find_provider_in_archive(&in->archives[ref->index], name);
      }
      if (provider) {
         free(weak);
         return provider;
      }
   }

   for (i = 0; i < in->order_count; ++i) {
      input_ref_t *ref = &in->order[i];
      if (ref->kind == INPUT_REF_OBJECT) {
         provider = find_provider_in_object(&in->cmd_objects[ref->index], weak);
      } else {
         provider = find_provider_in_archive(&in->archives[ref->index], weak);
      }
      if (provider) {
         free(weak);
         return provider;
      }
   }

   free(weak);
   return NULL;
}

static void include_object(input_set_t *in, object_file_t *obj)
{
   if (obj->selected)
      return;
   obj->selected = 1;
   if (obj->archive_member)
      obj->archive_member->selected = 1;
   in->objects = (object_file_t *)xrealloc(in->objects,
      (in->object_count + 1) * sizeof(*in->objects));
   in->objects[in->object_count++] = *obj;
}

static void select_needed_objects(input_set_t *in)
{
   int progress;
   do {
      char **needed = NULL;
      size_t needed_count = 0;
      size_t i;

      collect_needed_symbols(in, &needed, &needed_count);
      progress = 0;

      for (i = 0; i < needed_count; ++i) {
         object_file_t *provider;
         if (selected_objects_export_symbol(in, needed[i]))
            continue;
         provider = find_best_provider(in, needed[i]);
         if (provider) {
            include_object(in, provider);
            progress = 1;
         }
      }

      for (i = 0; i < needed_count; ++i)
         free(needed[i]);
      free(needed);
   } while (progress);
}

static void warn_unused_cmdline_objects(const input_set_t *in)
{
   size_t i;
   for (i = 0; i < in->cmd_object_count; ++i) {
      if (!in->cmd_objects[i].selected)
         fprintf(stderr, "n65ld: warning: unused object '%s' not linked\n", in->cmd_objects[i].origin);
   }

   for (i = 0; i < in->archive_count; ++i) {
      const archive_file_t *arc = &in->archives[i];
      size_t m;
      int any_selected = 0;
      for (m = 0; m < arc->member_count; ++m) {
         if (arc->members[m].selected || arc->members[m].obj.selected) {
            any_selected = 1;
            break;
         }
      }
      if (!any_selected)
         fprintf(stderr, "n65ld: warning: unused archive '%s' not linked\n", arc->path);
   }
}

static int call_graph_find_or_add_node(call_graph_node_t **nodes, size_t *count, const char *name)
{
   size_t i;

   for (i = 0; i < *count; ++i) {
      if (strcmp((*nodes)[i].name, name) == 0)
         return (int)i;
   }

   *nodes = (call_graph_node_t *)xrealloc(*nodes, (*count + 1) * sizeof(**nodes));
   (*nodes)[*count].name = xstrdup(name);
   (*nodes)[*count].has_symbol_backed_params = 0;
   return (int)(*count)++;
}

static void call_graph_add_edge(call_graph_edge_t **edges, size_t *count, int from, int to)
{
   size_t i;

   for (i = 0; i < *count; ++i) {
      if ((*edges)[i].from == from && (*edges)[i].to == to)
         return;
   }

   *edges = (call_graph_edge_t *)xrealloc(*edges, (*count + 1) * sizeof(**edges));
   (*edges)[*count].from = from;
   (*edges)[*count].to = to;
   (*count)++;
}

static void call_graph_collect_from_object(const object_file_t *obj,
                                           call_graph_node_t **nodes, size_t *node_count,
                                           call_graph_edge_t **edges, size_t *edge_count)
{
   size_t i;

   for (i = 0; i < obj->export_count; ++i) {
      const char *name = obj->exports[i].name;
      const char *sym = NULL;
      char *caller = NULL;
      char *callee = NULL;

      if (symbol_backed_metadata_parse_function(name, &sym)) {
         int idx = call_graph_find_or_add_node(nodes, node_count, sym);
         (*nodes)[idx].has_symbol_backed_params = 1;
         continue;
      }

      if (symbol_backed_metadata_parse_edge(name, &caller, &callee)) {
         int from = call_graph_find_or_add_node(nodes, node_count, caller);
         int to = call_graph_find_or_add_node(nodes, node_count, callee);
         call_graph_add_edge(edges, edge_count, from, to);
      }

      free(caller);
      free(callee);
   }
}

static void call_graph_tarjan_visit(int v,
                                    const call_graph_edge_t *edges, size_t edge_count,
                                    int *index_counter,
                                    int *stack, int *stack_top,
                                    int *indices, int *lowlink, unsigned char *onstack,
                                    int *component, int *component_sizes, int *component_count)
{
   size_t i;

   indices[v] = *index_counter;
   lowlink[v] = *index_counter;
   (*index_counter)++;
   stack[(*stack_top)++] = v;
   onstack[v] = 1;

   for (i = 0; i < edge_count; ++i) {
      int w;

      if (edges[i].from != v)
         continue;
      w = edges[i].to;
      if (indices[w] < 0) {
         call_graph_tarjan_visit(w, edges, edge_count, index_counter, stack, stack_top,
            indices, lowlink, onstack, component, component_sizes, component_count);
         if (lowlink[w] < lowlink[v])
            lowlink[v] = lowlink[w];
      }
      else if (onstack[w] && indices[w] < lowlink[v]) {
         lowlink[v] = indices[w];
      }
   }

   if (lowlink[v] == indices[v]) {
      int cid = (*component_count)++;
      component_sizes[cid] = 0;
      for (;;) {
         int w = stack[--(*stack_top)];
         onstack[w] = 0;
         component[w] = cid;
         component_sizes[cid]++;
         if (w == v)
            break;
      }
   }
}

static const char *display_function_symbol(const char *name)
{
   static char buf[512];
   size_t len;

   if (!name)
      return "?";

   len = strlen(name);
   if (len > 0 && name[len - 1] == '?') {
      if (len >= sizeof(buf))
         len = sizeof(buf) - 1;
      memcpy(buf, name, len - 1);
      buf[len - 1] = 0;
      return buf;
   }

   return name;
}

static void enforce_symbol_backed_call_graph(const input_set_t *in)
{
   call_graph_node_t *nodes = NULL;
   call_graph_edge_t *edges = NULL;
   size_t node_count = 0;
   size_t edge_count = 0;
   int *indices = NULL;
   int *lowlink = NULL;
   int *stack = NULL;
   int *component = NULL;
   int *component_sizes = NULL;
   unsigned char *onstack = NULL;
   unsigned char *component_has_symbol_backed = NULL;
   unsigned char *component_has_cycle = NULL;
   int stack_top = 0;
   int index_counter = 0;
   int component_count = 0;
   size_t i;

   for (i = 0; i < in->object_count; ++i)
      call_graph_collect_from_object(&in->objects[i], &nodes, &node_count, &edges, &edge_count);

   if (node_count == 0)
      goto cleanup;

   indices = (int *)xmalloc(sizeof(*indices) * node_count);
   lowlink = (int *)xmalloc(sizeof(*lowlink) * node_count);
   stack = (int *)xmalloc(sizeof(*stack) * node_count);
   component = (int *)xmalloc(sizeof(*component) * node_count);
   component_sizes = (int *)xcalloc(node_count, sizeof(*component_sizes));
   onstack = (unsigned char *)xcalloc(node_count, sizeof(*onstack));
   component_has_symbol_backed = (unsigned char *)xcalloc(node_count, sizeof(*component_has_symbol_backed));
   component_has_cycle = (unsigned char *)xcalloc(node_count, sizeof(*component_has_cycle));

   for (i = 0; i < node_count; ++i) {
      indices[i] = -1;
      lowlink[i] = -1;
      component[i] = -1;
   }

   for (i = 0; i < node_count; ++i) {
      if (indices[i] < 0) {
         call_graph_tarjan_visit((int)i, edges, edge_count, &index_counter, stack, &stack_top,
            indices, lowlink, onstack, component, component_sizes, &component_count);
      }
   }

   for (i = 0; i < node_count; ++i) {
      if (component[i] >= 0 && nodes[i].has_symbol_backed_params)
         component_has_symbol_backed[component[i]] = 1;
   }
   for (i = 0; i < (size_t)component_count; ++i) {
      if (component_sizes[i] > 1)
         component_has_cycle[i] = 1;
   }
   for (i = 0; i < edge_count; ++i) {
      if (component[edges[i].from] == component[edges[i].to])
         component_has_cycle[component[edges[i].from]] = 1;
   }

   for (i = 0; i < (size_t)component_count; ++i) {
      size_t j;

      if (!component_has_cycle[i] || !component_has_symbol_backed[i])
         continue;

      for (j = 0; j < node_count; ++j) {
         if (component[j] == (int)i && nodes[j].has_symbol_backed_params) {
            fprintf(stderr, "n65ld: call graph cycle reaches function '%s' with symbol-backed parameters\n", display_function_symbol(nodes[j].name));
            exit(1);
         }
      }
   }

cleanup:
   for (i = 0; i < node_count; ++i)
      free(nodes[i].name);
   free(nodes);
   free(edges);
   free(indices);
   free(lowlink);
   free(stack);
   free(component);
   free(component_sizes);
   free(onstack);
   free(component_has_symbol_backed);
   free(component_has_cycle);
}

static void add_global(layout_t *layout, const char *name, uint16_t addr, uint8_t segid, const char *source)
{
   size_t i;
   for (i = 0; i < layout->global_count; ++i) {
      if (strcmp(layout->globals[i].name, name) == 0) {
         fprintf(stderr, "n65ld: duplicate global symbol '%s' from %s and %s\n",
            name, layout->globals[i].source, source);
         exit(1);
      }
   }
   layout->globals = (global_symbol_t *)xrealloc(layout->globals,
      (layout->global_count + 1) * sizeof(*layout->globals));
   layout->globals[layout->global_count].name = xstrdup(name);
   layout->globals[layout->global_count].addr = addr;
   layout->globals[layout->global_count].segid = segid;
   layout->globals[layout->global_count].source = source;
   layout->global_count++;
}

static void add_generated_symbols(layout_t *layout)
{
   add_global(layout, "__copy_table", layout->copy_table_addr, O65_SEG_ABS, "<linker>");
   add_global(layout, "__zero_table", layout->zero_table_addr, O65_SEG_ABS, "<linker>");
   add_global(layout, "__init_table", layout->init_table_addr, O65_SEG_ABS, "<linker>");
   add_global(layout, "__stack_start", layout->stack_start, O65_SEG_ABS, "<linker>");
   add_global(layout, "__stack_top", layout->stack_top, O65_SEG_ABS, "<linker>");
}

static uint16_t lookup_global_addr(const layout_t *layout, const char *name)
{
   size_t i;
   char *weak;

   for (i = 0; i < layout->global_count; ++i) {
      if (strcmp(layout->globals[i].name, name) == 0)
         return layout->globals[i].addr;
   }

   weak = make_weak_name(name);
   for (i = 0; i < layout->global_count; ++i) {
      if (strcmp(layout->globals[i].name, weak) == 0) {
         uint16_t addr = layout->globals[i].addr;
         free(weak);
         return addr;
      }
   }
   free(weak);

   fprintf(stderr, "n65ld: unresolved symbol '%s'\n", name);
   exit(1);
}

static size_t count_init_functions_in_input(const input_set_t *in)
{
   size_t i, j;
   size_t count = 0;

   for (i = 0; i < in->object_count; ++i) {
      const object_file_t *obj = &in->objects[i];
      for (j = 0; j < obj->export_count; ++j) {
         if (symbol_is_init_function(obj->exports[j].name))
            count++;
      }
   }

   return count;
}

static int segment_name_matches_prefix(const char *name, const char *prefix)
{
   size_t n;

   if (!name || !prefix)
      return 0;

   n = strlen(prefix);
   return strncasecmp(name, prefix, n) == 0 && (name[n] == '\0' || name[n] == '.');
}

static const char *segment_name_suffix(const char *name)
{
   const char *dot;

   if (!name)
      return NULL;
   dot = strchr(name, '.');
   return (dot && dot[1]) ? dot + 1 : NULL;
}

static const char *rule_run_region_name(const segment_rule_t *rule)
{
   if (!rule)
      return NULL;
   return rule->run_name[0] ? rule->run_name : rule->load_name;
}

static memory_cursor_t *ensure_cursor(layout_t *layout, const linker_config_t *cfg, const char *mem_name)
{
   size_t i;
   const memory_region_t *mem;

   for (i = 0; i < layout->cursor_count; ++i) {
      if (str_ieq(layout->cursors[i].name, mem_name))
         return &layout->cursors[i];
   }

   mem = find_memory(cfg, mem_name);
   if (!mem) {
      fprintf(stderr, "n65ld: MEMORY region '%s' not found\n", mem_name);
      exit(1);
   }

   layout->cursors = (memory_cursor_t *)xrealloc(layout->cursors,
      (layout->cursor_count + 1) * sizeof(*layout->cursors));
   memset(&layout->cursors[layout->cursor_count], 0, sizeof(*layout->cursors));
   snprintf(layout->cursors[layout->cursor_count].name, sizeof(layout->cursors[layout->cursor_count].name), "%s", mem->name);
   layout->cursors[layout->cursor_count].cur = mem->start;
   layout->cursors[layout->cursor_count].end = (uint32_t)mem->start + (uint32_t)mem->size;
   return &layout->cursors[layout->cursor_count++];
}

static uint16_t alloc_from_region(layout_t *layout, const linker_config_t *cfg, const char *mem_name,
   uint16_t size, const char *what, const char *origin)
{
   memory_cursor_t *cursor = ensure_cursor(layout, cfg, mem_name);
   uint32_t addr = cursor->cur;
   uint32_t end = addr + size;

   if (end > 0x10000u || end > cursor->end || (str_ieq(mem_name, "ROM") && end > 0xFFFAu)) {
      fprintf(stderr, "n65ld: %s overflow while placing %s from %s in %s\n", mem_name, what, origin, mem_name);
      exit(1);
   }

   cursor->cur = (uint16_t)end;
   return (uint16_t)addr;
}

static void add_copy_record(layout_t *layout, const char *name, uint16_t load_addr, uint16_t run_addr, uint16_t size)
{
   if (size == 0)
      return;
   layout->copy_records = (copy_record_t *)xrealloc(layout->copy_records,
      (layout->copy_record_count + 1) * sizeof(*layout->copy_records));
   layout->copy_records[layout->copy_record_count].name = xstrdup(name ? name : "DATA");
   layout->copy_records[layout->copy_record_count].load_addr = load_addr;
   layout->copy_records[layout->copy_record_count].run_addr = run_addr;
   layout->copy_records[layout->copy_record_count].size = size;
   layout->copy_record_count++;
}

static void add_zero_record(layout_t *layout, const char *name, uint16_t run_addr, uint16_t size)
{
   if (size == 0)
      return;
   layout->zero_records = (zero_record_t *)xrealloc(layout->zero_records,
      (layout->zero_record_count + 1) * sizeof(*layout->zero_records));
   layout->zero_records[layout->zero_record_count].name = xstrdup(name ? name : "BSS");
   layout->zero_records[layout->zero_record_count].run_addr = run_addr;
   layout->zero_records[layout->zero_record_count].size = size;
   layout->zero_record_count++;
}

static const object_layout_t *find_layout_for_value(const object_file_t *obj, uint8_t segid, uint16_t packed_value)
{
   const object_layout_t *fallback = NULL;
   size_t i;

   for (i = 0; i < obj->layout_count; ++i) {
      const object_layout_t *lay = &obj->layouts[i];
      uint32_t start = lay->packed_base;
      uint32_t end = (uint32_t)lay->packed_base + lay->size;

      if (lay->segid != segid)
         continue;
      if (packed_value >= start && packed_value < end)
         return lay;
      if (packed_value == end)
         fallback = lay;
   }

   return fallback;
}

static uint16_t object_runtime_addr_for_value(const object_file_t *obj, uint8_t segid, uint16_t packed_value)
{
   const object_layout_t *lay;
   uint16_t base;

   if (segid == O65_SEG_ABS)
      return packed_value;

   lay = find_layout_for_value(obj, segid, packed_value);
   if (!lay) {
      fprintf(stderr, "n65ld: could not map packed value $%04X in %s for segment %u\n", packed_value, obj->origin, (unsigned)segid);
      exit(1);
   }

   base = (segid == O65_SEG_TEXT) ? lay->load_addr : lay->run_addr;
   return (uint16_t)(base + (packed_value - lay->packed_base));
}

static void layout_objects(const linker_config_t *cfg, input_set_t *in, layout_t *layout)
{
   const segment_rule_t *code = find_segment_rule(cfg, "CODE");
   const segment_rule_t *data = find_segment_rule(cfg, "DATA");
   const segment_rule_t *bss = find_segment_rule(cfg, "BSS");
   const segment_rule_t *zp = find_segment_rule(cfg, "ZEROPAGE");
   const char *code_load_name = code ? code->load_name : NULL;
   const char *data_load_name = data ? data->load_name : NULL;
   const char *data_run_name = rule_run_region_name(data);
   const char *bss_run_name = rule_run_region_name(bss);
   const char *zp_run_name = rule_run_region_name(zp);
   size_t i, j;

   if (!code_load_name || !data_load_name || !data_run_name || !bss_run_name || !zp_run_name) {
      fprintf(stderr, "n65ld: config must define CODE, DATA, BSS, and ZEROPAGE segments with valid MEMORY targets\n");
      exit(1);
   }

   memset(layout, 0, sizeof(*layout));
   (void)ensure_cursor(layout, cfg, code_load_name);
   (void)ensure_cursor(layout, cfg, data_load_name);
   (void)ensure_cursor(layout, cfg, data_run_name);
   (void)ensure_cursor(layout, cfg, bss_run_name);
   (void)ensure_cursor(layout, cfg, zp_run_name);

   for (i = 0; i < in->object_count; ++i) {
      object_file_t *obj = &in->objects[i];
      obj->place_text_load = alloc_from_region(layout, cfg, code_load_name, (uint16_t)obj->text.length, "text", obj->origin);
      obj->place_data_load = alloc_from_region(layout, cfg, data_load_name, (uint16_t)obj->data.length, "data load image", obj->origin);

      for (j = 0; j < obj->layout_count; ++j) {
         object_layout_t *lay = &obj->layouts[j];
         const char *suffix = segment_name_suffix(lay->name);

         lay->load_addr = 0;
         lay->run_addr = 0;

         switch (lay->segid) {
            case O65_SEG_TEXT:
               lay->load_addr = (uint16_t)(obj->place_text_load + lay->packed_base);
               lay->run_addr = lay->load_addr;
               break;

            case O65_SEG_DATA: {
               const char *run_name = (suffix && segment_name_matches_prefix(lay->name, "DATA")) ? suffix : data_run_name;
               lay->load_addr = (uint16_t)(obj->place_data_load + lay->packed_base);
               lay->run_addr = alloc_from_region(layout, cfg, run_name, lay->size, lay->name, obj->origin);
               add_copy_record(layout, lay->name, lay->load_addr, lay->run_addr, lay->size);
               break;
            }

            case O65_SEG_BSS: {
               const char *run_name = (suffix && segment_name_matches_prefix(lay->name, "BSS")) ? suffix : bss_run_name;
               lay->run_addr = alloc_from_region(layout, cfg, run_name, lay->size, lay->name, obj->origin);
               add_zero_record(layout, lay->name, lay->run_addr, lay->size);
               break;
            }

            case O65_SEG_ZP: {
               const char *run_name = (suffix && (segment_name_matches_prefix(lay->name, "ZEROPAGE") || segment_name_matches_prefix(lay->name, "ZP") || segment_name_matches_prefix(lay->name, "ZERO"))) ? suffix : zp_run_name;
               lay->run_addr = alloc_from_region(layout, cfg, run_name, lay->size, lay->name, obj->origin);
               break;
            }
         }
      }
   }

   layout->copy_table_addr = alloc_from_region(layout, cfg, data_load_name,
      (uint16_t)((layout->copy_record_count + 1) * 6), "__copy_table", "<linker>");
   layout->zero_table_addr = alloc_from_region(layout, cfg, data_load_name,
      (uint16_t)((layout->zero_record_count + 1) * 4), "__zero_table", "<linker>");
   {
      size_t init_count = count_init_functions_in_input(in);
      layout->init_table_addr = alloc_from_region(layout, cfg, data_load_name,
         (uint16_t)((init_count + 1) * 2), "__init_table", "<linker>");
      layout->init_table_size = (uint16_t)((init_count + 1) * 2);
   }
   layout->copy_table_size = (uint16_t)((layout->copy_record_count + 1) * 6);
   layout->zero_table_size = (uint16_t)((layout->zero_record_count + 1) * 4);

   {
      memory_cursor_t *stack_cursor = ensure_cursor(layout, cfg, data_run_name);
      layout->stack_start = stack_cursor->cur;
      layout->stack_top = (uint16_t)(stack_cursor->end - 1u);
   }

   for (i = 0; i < in->object_count; ++i) {
      object_file_t *obj = &in->objects[i];
      for (j = 0; j < obj->export_count; ++j) {
         uint16_t addr;

         if (symbol_backed_metadata_has_prefix(obj->exports[j].name))
            continue;

         if (obj->exports[j].segid == O65_SEG_ABS)
            addr = obj->exports[j].value;
         else
            addr = object_runtime_addr_for_value(obj, obj->exports[j].segid, obj->exports[j].value);
         add_global(layout, obj->exports[j].name, addr, obj->exports[j].segid, obj->origin);
      }
   }
}

static void patch_u8(uint8_t *buf, size_t len, uint32_t off, uint8_t v, const char *origin)
{
   if (off >= len) {
      fprintf(stderr, "n65ld: relocation offset out of range in %s\n", origin);
      exit(1);
   }
   buf[off] = v;
}

static void patch_u16(uint8_t *buf, size_t len, uint32_t off, uint16_t v, const char *origin)
{
   if (off + 1 >= len) {
      fprintf(stderr, "n65ld: relocation word offset out of range in %s\n", origin);
      exit(1);
   }
   buf[off] = (uint8_t)(v & 0xFFu);
   buf[off + 1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void apply_segment_relocs(object_file_t *obj, o65_segment_t *seg, const layout_t *layout, const char *seg_name)
{
   size_t i;
   for (i = 0; i < seg->reloc_count; ++i) {
      reloc_t *r = &seg->relocs[i];
      uint16_t target = 0;
      uint16_t current_word;
      const char *who = obj->origin;
      (void)seg_name;

      if (r->segid == O65_SEG_UNDEF) {
         if (r->undef_index >= obj->undef_count) {
            fprintf(stderr, "n65ld: bad undefined-symbol index in %s\n", who);
            exit(1);
         }
         target = lookup_global_addr(layout, obj->undefs[r->undef_index]);
      } else {
         current_word = (r->type == O65_RTYPE_WORD)
            ? (uint16_t)(seg->data[r->offset] | (seg->data[r->offset + 1] << 8))
            : seg->data[r->offset];
         target = object_runtime_addr_for_value(obj, r->segid, current_word);
      }

      switch (r->type) {
         case O65_RTYPE_LOW:
            patch_u8(seg->data, seg->length, r->offset, (uint8_t)(target & 0xFFu), who);
            break;
         case O65_RTYPE_HIGH:
            patch_u8(seg->data, seg->length, r->offset, (uint8_t)((target >> 8) & 0xFFu), who);
            break;
         case O65_RTYPE_WORD:
            current_word = (uint16_t)(seg->data[r->offset] | (seg->data[r->offset + 1] << 8));
            if (r->segid == O65_SEG_UNDEF)
               target = (uint16_t)(target + current_word);
            patch_u16(seg->data, seg->length, r->offset, target, who);
            break;
         default:
            fprintf(stderr, "n65ld: unsupported relocation type 0x%02x in %s\n", r->type, who);
            exit(1);
      }
   }
}

static void resolve_all(input_set_t *in, const layout_t *layout)
{
   size_t i;
   for (i = 0; i < in->object_count; ++i) {
      apply_segment_relocs(&in->objects[i], &in->objects[i].text, layout, "text");
      apply_segment_relocs(&in->objects[i], &in->objects[i].data, layout, "data");
   }
}

static void image_write(uint8_t *image, uint8_t *used, uint16_t addr, const uint8_t *src, size_t len, const char *who)
{
   size_t i;
   for (i = 0; i < len; ++i) {
      uint32_t a = (uint32_t)addr + i;
      if (a > 0xFFFFu) {
         fprintf(stderr, "n65ld: image write overflow from %s\n", who);
         exit(1);
      }
      image[a] = src[i];
      used[a] = 1;
   }
}

static void build_init_table_image(const input_set_t *in, const layout_t *layout, uint8_t *table)
{
   size_t i, j;
   size_t out = 0;

   memset(table, 0, layout->init_table_size);

   for (i = 0; i < in->object_count; ++i) {
      const object_file_t *obj = &in->objects[i];
      for (j = 0; j < obj->export_count; ++j) {
         uint16_t addr;

         if (!symbol_is_init_function(obj->exports[j].name))
            continue;
         addr = lookup_global_addr(layout, obj->exports[j].name);
         table[out++] = (uint8_t)(addr & 0xFFu);
         table[out++] = (uint8_t)((addr >> 8) & 0xFFu);
      }
   }
}

static void build_copy_table_image(const layout_t *layout, uint8_t *table)
{
   size_t i;
   size_t out = 0;

   memset(table, 0, layout->copy_table_size);
   for (i = 0; i < layout->copy_record_count; ++i) {
      const copy_record_t *rec = &layout->copy_records[i];
      table[out++] = (uint8_t)(rec->load_addr & 0xFFu);
      table[out++] = (uint8_t)((rec->load_addr >> 8) & 0xFFu);
      table[out++] = (uint8_t)(rec->run_addr & 0xFFu);
      table[out++] = (uint8_t)((rec->run_addr >> 8) & 0xFFu);
      table[out++] = (uint8_t)(rec->size & 0xFFu);
      table[out++] = (uint8_t)((rec->size >> 8) & 0xFFu);
   }
}

static void build_zero_table_image(const layout_t *layout, uint8_t *table)
{
   size_t i;
   size_t out = 0;

   memset(table, 0, layout->zero_table_size);
   for (i = 0; i < layout->zero_record_count; ++i) {
      const zero_record_t *rec = &layout->zero_records[i];
      table[out++] = (uint8_t)(rec->run_addr & 0xFFu);
      table[out++] = (uint8_t)((rec->run_addr >> 8) & 0xFFu);
      table[out++] = (uint8_t)(rec->size & 0xFFu);
      table[out++] = (uint8_t)((rec->size >> 8) & 0xFFu);
   }
}

static void build_rom_image(const linker_config_t *cfg, input_set_t *in, const layout_t *layout, uint8_t *image, uint8_t *used)
{
   const memory_region_t *rom = find_memory(cfg, "ROM");
   size_t i;
   uint16_t reset, nmi, irqbrk;
   if (!rom) {
      fprintf(stderr, "n65ld: ROM memory region not found\n");
      exit(1);
   }
   memset(image, 0xFF, 65536);
   memset(used, 0, 65536);

   for (i = 0; i < in->object_count; ++i) {
      image_write(image, used, in->objects[i].place_text_load, in->objects[i].text.data,
         in->objects[i].text.length, in->objects[i].origin);
      image_write(image, used, in->objects[i].place_data_load, in->objects[i].data.data,
         in->objects[i].data.length, in->objects[i].origin);
   }

   if (layout->copy_table_size > 0) {
      uint8_t *table = (uint8_t *)xmalloc(layout->copy_table_size);
      build_copy_table_image(layout, table);
      image_write(image, used, layout->copy_table_addr, table, layout->copy_table_size, "<linker:__copy_table>");
      free(table);
   }

   if (layout->zero_table_size > 0) {
      uint8_t *table = (uint8_t *)xmalloc(layout->zero_table_size);
      build_zero_table_image(layout, table);
      image_write(image, used, layout->zero_table_addr, table, layout->zero_table_size, "<linker:__zero_table>");
      free(table);
   }

   if (layout->init_table_size > 0) {
      uint8_t *table = (uint8_t *)xmalloc(layout->init_table_size);
      build_init_table_image(in, layout, table);
      image_write(image, used, layout->init_table_addr, table, layout->init_table_size, "<linker:__init_table>");
      free(table);
   }

   reset = lookup_global_addr(layout, "__reset");
   nmi = lookup_global_addr(layout, "__nmi");
   irqbrk = lookup_global_addr(layout, "__irqbrk");

   image[0xFFFA] = (uint8_t)(nmi & 0xFFu);
   image[0xFFFB] = (uint8_t)((nmi >> 8) & 0xFFu);
   image[0xFFFC] = (uint8_t)(reset & 0xFFu);
   image[0xFFFD] = (uint8_t)((reset >> 8) & 0xFFu);
   image[0xFFFE] = (uint8_t)(irqbrk & 0xFFu);
   image[0xFFFF] = (uint8_t)((irqbrk >> 8) & 0xFFu);
   used[0xFFFA] = used[0xFFFB] = used[0xFFFC] = used[0xFFFD] = used[0xFFFE] = used[0xFFFF] = 1;
}

static uint8_t hex_checksum(const uint8_t *bytes, size_t n)
{
   uint32_t sum = 0;
   size_t i;
   for (i = 0; i < n; ++i)
      sum += bytes[i];
   return (uint8_t)((~sum + 1) & 0xFFu);
}

static void emit_hex_record(FILE *fp, uint16_t addr, const uint8_t *data, uint8_t len, uint8_t type)
{
   uint8_t hdr[4];
   size_t i;
   hdr[0] = len;
   hdr[1] = (uint8_t)((addr >> 8) & 0xFFu);
   hdr[2] = (uint8_t)(addr & 0xFFu);
   hdr[3] = type;
   fprintf(fp, ":%02X%04X%02X", len, addr, type);
   for (i = 0; i < len; ++i)
      fprintf(fp, "%02X", data[i]);
   {
      uint8_t csum = hex_checksum(hdr, sizeof(hdr));
      for (i = 0; i < len; ++i)
         csum = (uint8_t)(csum - data[i]);
      fprintf(fp, "%02X\n", csum);
   }
}

static void write_intel_hex(const char *path, const uint8_t *image, const uint8_t *used)
{
   FILE *fp = fopen(path, "w");
   uint32_t addr = 0;
   if (!fp) {
      fprintf(stderr, "n65ld: cannot create '%s': %s\n", path, strerror(errno));
      exit(1);
   }
   while (addr < 65536u) {
      uint8_t chunk[16];
      uint8_t len = 0;
      while (addr < 65536u && !used[addr])
         addr++;
      if (addr >= 65536u)
         break;
      while (addr + len < 65536u && used[addr + len] && len < sizeof(chunk)) {
         chunk[len] = image[addr + len];
         len++;
      }
      emit_hex_record(fp, (uint16_t)addr, chunk, len, 0x00);
      addr += len;
   }
   fprintf(fp, ":00000001FF\n");
   fclose(fp);
}

static void write_map_file(const char *path, const linker_config_t *cfg, const input_set_t *in, const layout_t *layout)
{
   FILE *fp;
   size_t i;
   if (!path)
      return;
   fp = fopen(path, "w");
   if (!fp) {
      fprintf(stderr, "n65ld: cannot create '%s': %s\n", path, strerror(errno));
      exit(1);
   }

   fprintf(fp, "MEMORY\n");
   for (i = 0; i < cfg->mem_count; ++i) {
      fprintf(fp, "  %-10s start=$%04X size=$%04X type=%s\n",
         cfg->mem[i].name, cfg->mem[i].start, cfg->mem[i].size, cfg->mem[i].type);
   }

   fprintf(fp, "\nOBJECTS\n");
   for (i = 0; i < in->object_count; ++i) {
      const object_file_t *o = &in->objects[i];
      size_t j;
      fprintf(fp, "  %s\n", o->origin);
      for (j = 0; j < o->layout_count; ++j) {
         const object_layout_t *lay = &o->layouts[j];
         if (lay->segid == O65_SEG_TEXT) {
            fprintf(fp, "     %-16s load=$%04X size=$%04X\n", lay->name, lay->load_addr, lay->size);
         }
         else if (lay->segid == O65_SEG_DATA) {
            fprintf(fp, "     %-16s load=$%04X run=$%04X size=$%04X\n", lay->name, lay->load_addr, lay->run_addr, lay->size);
         }
         else {
            fprintf(fp, "     %-16s run=$%04X size=$%04X\n", lay->name, lay->run_addr, lay->size);
         }
      }
   }

   fprintf(fp, "\nTABLES\n");
   fprintf(fp, "  __copy_table  $%04X size=$%04X\n", layout->copy_table_addr, layout->copy_table_size);
   fprintf(fp, "  __zero_table  $%04X size=$%04X\n", layout->zero_table_addr, layout->zero_table_size);
   fprintf(fp, "  __init_table  $%04X size=$%04X\n", layout->init_table_addr, layout->init_table_size);
   fprintf(fp, "  __stack_start $%04X\n", layout->stack_start);
   fprintf(fp, "  __stack_top   $%04X\n", layout->stack_top);

   fprintf(fp, "\nSYMBOLS\n");
   for (i = 0; i < layout->global_count; ++i) {
      fprintf(fp, "  $%04X  %-20s  %s\n",
         layout->globals[i].addr, layout->globals[i].name, layout->globals[i].source);
   }

   fclose(fp);
}

static void free_object(object_file_t *obj)
{
   size_t i;
   free(obj->text.data);
   free(obj->text.relocs);
   free(obj->data.data);
   free(obj->data.relocs);
   for (i = 0; i < obj->undef_count; ++i)
      free(obj->undefs[i]);
   free(obj->undefs);
   for (i = 0; i < obj->export_count; ++i)
      free(obj->exports[i].name);
   free(obj->exports);
   for (i = 0; i < obj->layout_count; ++i)
      free(obj->layouts[i].name);
   free(obj->layouts);
}

int main(int argc, char **argv)
{
   int argi;
   int end_of_options = 0;
   int hex_path_set = 0;
   const char *cfg_path = NULL;
   const char *compat_hex_path = NULL;
   const char *hex_path = "a.hex";
   const char *map_path = NULL;
   linker_config_t cfg;
   input_set_t inputs;
   layout_t layout;
   uint8_t *image;
   uint8_t *used;
   size_t i;

   memset(&inputs, 0, sizeof(inputs));
   memset(&layout, 0, sizeof(layout));

   if (argc < 2) {
      usage(stderr);
      return 1;
   }

   for (argi = 1; argi < argc; ++argi) {
      const char *arg = argv[argi];

      if (!end_of_options && strcmp(arg, "--") == 0) {
         end_of_options = 1;
         continue;
      }

      if (!end_of_options && arg[0] == '-' && arg[1] != '\0') {
         if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            return 0;
         }
         if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            printf("n65ld\n");
            return 0;
         }
         if (strcmp(arg, "-o") == 0) {
            if (++argi >= argc) {
               fprintf(stderr, "n65ld: missing argument for -o\n");
               return 1;
            }
            hex_path = argv[argi];
            hex_path_set = 1;
            continue;
         }
         if (strncmp(arg, "-o", 2) == 0 && arg[2] != '\0') {
            hex_path = arg + 2;
            hex_path_set = 1;
            continue;
         }
         if (strcmp(arg, "-T") == 0) {
            if (++argi >= argc) {
               fprintf(stderr, "n65ld: missing argument for -T\n");
               return 1;
            }
            cfg_path = argv[argi];
            continue;
         }
         if (strncmp(arg, "-T", 2) == 0 && arg[2] != '\0') {
            cfg_path = arg + 2;
            continue;
         }
         if (strcmp(arg, "--script") == 0) {
            if (++argi >= argc) {
               fprintf(stderr, "n65ld: missing argument for --script\n");
               return 1;
            }
            cfg_path = argv[argi];
            continue;
         }
         if (strncmp(arg, "--script=", 9) == 0) {
            cfg_path = arg + 9;
            continue;
         }
         if (strcmp(arg, "-Map") == 0) {
            if (++argi >= argc) {
               fprintf(stderr, "n65ld: missing argument for -Map\n");
               return 1;
            }
            map_path = argv[argi];
            continue;
         }
         if (strncmp(arg, "-Map=", 5) == 0) {
            map_path = arg + 5;
            continue;
         }

         fprintf(stderr, "n65ld: unsupported option '%s'\n", arg);
         return 1;
      }

      if (ends_with(arg, ".cfg") && cfg_path == NULL) {
         cfg_path = arg;
         continue;
      }

      if (ends_with(arg, ".o65")) {
         inputs.cmd_objects = (object_file_t *)xrealloc(inputs.cmd_objects,
            (inputs.cmd_object_count + 1) * sizeof(*inputs.cmd_objects));
         load_object(arg, &inputs.cmd_objects[inputs.cmd_object_count]);
         inputs.cmd_objects[inputs.cmd_object_count].from_cmdline = 1;
         inputs.order = (input_ref_t *)xrealloc(inputs.order,
            (inputs.order_count + 1) * sizeof(*inputs.order));
         inputs.order[inputs.order_count].kind = INPUT_REF_OBJECT;
         inputs.order[inputs.order_count].index = inputs.cmd_object_count;
         inputs.order_count++;
         inputs.cmd_object_count++;
         continue;
      }

      if (ends_with(arg, ".a65")) {
         inputs.archives = (archive_file_t *)xrealloc(inputs.archives,
            (inputs.archive_count + 1) * sizeof(*inputs.archives));
         load_archive(arg, &inputs.archives[inputs.archive_count]);
         inputs.order = (input_ref_t *)xrealloc(inputs.order,
            (inputs.order_count + 1) * sizeof(*inputs.order));
         inputs.order[inputs.order_count].kind = INPUT_REF_ARCHIVE;
         inputs.order[inputs.order_count].index = inputs.archive_count;
         inputs.order_count++;
         inputs.archive_count++;
         continue;
      }

      if (!hex_path_set && compat_hex_path == NULL && ends_with(arg, ".hex")) {
         compat_hex_path = arg;
         continue;
      }

      if (compat_hex_path != NULL && map_path == NULL) {
         map_path = arg;
         continue;
      }

      fprintf(stderr, "n65ld: cannot classify input '%s'\n", arg);
      return 1;
   }

   if (compat_hex_path != NULL)
      hex_path = compat_hex_path;

   if (inputs.cmd_object_count == 0 && inputs.archive_count == 0) {
      fprintf(stderr, "n65ld: no input objects or archives\n");
      return 1;
   }

   if (cfg_path)
      parse_cfg_file(&cfg, cfg_path);
   else
      init_default_config(&cfg);

   select_needed_objects(&inputs);
   enforce_symbol_backed_call_graph(&inputs);
   warn_unused_cmdline_objects(&inputs);
   layout_objects(&cfg, &inputs, &layout);
   add_generated_symbols(&layout);
   resolve_all(&inputs, &layout);

   image = (uint8_t *)xmalloc(65536);
   used = (uint8_t *)xmalloc(65536);
   build_rom_image(&cfg, &inputs, &layout, image, used);
   write_intel_hex(hex_path, image, used);
   write_map_file(map_path, &cfg, &inputs, &layout);

   free(image);
   free(used);

   for (i = 0; i < inputs.object_count; ++i)
      free_object(&inputs.objects[i]);
   free(inputs.objects);
   free(inputs.cmd_objects);
   free(inputs.order);
   free(inputs.archives);
   for (i = 0; i < layout.global_count; ++i)
      free(layout.globals[i].name);
   free(layout.globals);
   for (i = 0; i < layout.copy_record_count; ++i)
      free(layout.copy_records[i].name);
   free(layout.copy_records);
   for (i = 0; i < layout.zero_record_count; ++i)
      free(layout.zero_records[i].name);
   free(layout.zero_records);
   free(layout.cursors);

   return 0;
}
