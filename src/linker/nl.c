#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
      "  nl [layout.cfg] input1.o65 [input2.o65 ... inputN.a65] output.hex [output.map]\n");
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

static char *xstrdup(const char *s)
{
   size_t n = strlen(s) + 1;
   char *p = (char *)malloc(n);
   if (!p) {
      fprintf(stderr, "nl: out of memory\n");
      exit(1);
   }
   memcpy(p, s, n);
   return p;
}

static void *xmalloc(size_t size)
{
   void *p = malloc(size ? size : 1);
   if (!p) {
      fprintf(stderr, "nl: out of memory\n");
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
      fprintf(stderr, "nl: out of memory\n");
      exit(1);
   }
   return p;
}

static void *xrealloc(void *ptr, size_t size)
{
   void *p = realloc(ptr, size ? size : 1);
   if (!p) {
      fprintf(stderr, "nl: out of memory\n");
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
      fprintf(stderr, "nl: cannot open '%s': %s\n", path, strerror(errno));
      exit(1);
   }

   if (fseek(fp, 0, SEEK_END) != 0) {
      fprintf(stderr, "nl: cannot seek '%s'\n", path);
      fclose(fp);
      exit(1);
   }
   size = ftell(fp);
   if (size < 0) {
      fprintf(stderr, "nl: cannot size '%s'\n", path);
      fclose(fp);
      exit(1);
   }
   if (fseek(fp, 0, SEEK_SET) != 0) {
      fprintf(stderr, "nl: cannot seek '%s'\n", path);
      fclose(fp);
      exit(1);
   }

   buf = (uint8_t *)xmalloc((size_t)size);
   if ((size_t)size && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
      fprintf(stderr, "nl: cannot read '%s'\n", path);
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
   fprintf(stderr, "nl: %s at offset 0x%zx in %s\n", msg, r->pos, r->label);
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
         fprintf(stderr, "nl: bad memory start '%s'\n", value);
         exit(1);
      }
      mem->start = (uint16_t)n.value;
   } else if (str_ieq(key, "size")) {
      n = parse_number(value);
      if (!n.ok || n.value > 0xFFFFu) {
         fprintf(stderr, "nl: bad memory size '%s'\n", value);
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
      fprintf(stderr, "nl: cannot open '%s': %s\n", path, strerror(errno));
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
               fprintf(stderr, "nl: too many MEMORY entries\n");
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
               fprintf(stderr, "nl: too many SEGMENTS entries\n");
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

static int try_parse_tail(const uint8_t *tail, size_t tail_size,
   reloc_t **text_relocs, size_t *text_reloc_count,
   reloc_t **data_relocs, size_t *data_reloc_count,
   symbol_t **exports, size_t *export_count,
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
         parse_exports(&r, exports, export_count) &&
         r.pos == r.size)
      return 1;

   free(*text_relocs); *text_relocs = NULL; *text_reloc_count = 0;
   free(*data_relocs); *data_relocs = NULL; *data_reloc_count = 0;
   {
      size_t i;
      for (i = 0; i < *export_count; ++i)
         free((*exports)[i].name);
      free(*exports);
      *exports = NULL;
      *export_count = 0;
   }

   r.pos = save;
   if (parse_reloc_table_old(&r, data_relocs, data_reloc_count) &&
         parse_reloc_table_old(&r, text_relocs, text_reloc_count) &&
         parse_exports(&r, exports, export_count) &&
         r.pos == r.size)
      return 1;

   return 0;
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

   memset(obj, 0, sizeof(*obj));
   snprintf(obj->origin, sizeof(obj->origin), "%s", label);

   reader_init(&r, data, size, label);
   rd_bytes(&r, header, sizeof(header));
   if (!(header[0] == 1 && header[1] == 0 && header[2] == 'o' && header[3] == '6' && header[4] == '5')) {
      fprintf(stderr, "nl: '%s' is not an o65 file\n", label);
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
         &undefs, &undef_count,
         label)) {
      fprintf(stderr, "nl: failed to parse o65 relocation/export tail in '%s' (header ended at 0x%zx)\n", label, header_end);
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
      fprintf(stderr, "nl: '%s' is not an a65 archive created by nar\n", path);
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
         fprintf(stderr, "nl: member name too long in '%s'\n", path);
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
      m->obj.archive_member = m;
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
         fprintf(stderr, "nl: warning: unused object '%s' not linked\n", in->cmd_objects[i].origin);
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
         fprintf(stderr, "nl: warning: unused archive '%s' not linked\n", arc->path);
   }
}

static void add_global(layout_t *layout, const char *name, uint16_t addr, uint8_t segid, const char *source)
{
   size_t i;
   for (i = 0; i < layout->global_count; ++i) {
      if (strcmp(layout->globals[i].name, name) == 0) {
         fprintf(stderr, "nl: duplicate global symbol '%s' from %s and %s\n",
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
   add_global(layout, "__data_load_start", layout->data_load_start, O65_SEG_ABS, "<linker>");
   add_global(layout, "__data_load_end", (uint16_t)(layout->data_load_start + layout->data_load_size), O65_SEG_ABS, "<linker>");
   add_global(layout, "__data_run_start", layout->data_run_start, O65_SEG_ABS, "<linker>");
   add_global(layout, "__data_run_end", (uint16_t)(layout->data_run_start + layout->data_run_size), O65_SEG_ABS, "<linker>");
   add_global(layout, "__data_size", layout->data_run_size, O65_SEG_ABS, "<linker>");

   add_global(layout, "__bss_start", layout->bss_start, O65_SEG_ABS, "<linker>");
   add_global(layout, "__bss_end", (uint16_t)(layout->bss_start + layout->bss_size), O65_SEG_ABS, "<linker>");
   add_global(layout, "__bss_size", layout->bss_size, O65_SEG_ABS, "<linker>");

   add_global(layout, "__init_table", layout->init_table_addr, O65_SEG_ABS, "<linker>");
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

   fprintf(stderr, "nl: unresolved symbol '%s'\n", name);
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

static void layout_objects(const linker_config_t *cfg, input_set_t *in, layout_t *layout)
{
   const segment_rule_t *code = find_segment_rule(cfg, "CODE");
   const segment_rule_t *data = find_segment_rule(cfg, "DATA");
   const segment_rule_t *bss = find_segment_rule(cfg, "BSS");
   const segment_rule_t *zp = find_segment_rule(cfg, "ZEROPAGE");
   const memory_region_t *code_mem = code ? find_memory(cfg, code->load_name) : NULL;
   const memory_region_t *data_load_mem = data ? find_memory(cfg, data->load_name) : NULL;
   const memory_region_t *data_run_mem = data && data->run_name[0] ? find_memory(cfg, data->run_name) : NULL;
   const memory_region_t *bss_run_mem = bss ? find_memory(cfg, bss->load_name) : NULL;
   const memory_region_t *zp_run_mem = zp && zp->run_name[0] ? find_memory(cfg, zp->run_name) : NULL;
   int shared_rom;
   int shared_ram;
   uint32_t code_load_limit;
   uint32_t data_load_limit;
   uint32_t data_run_limit;
   uint32_t bss_run_limit;
   uint32_t zp_run_limit;
   size_t i, j;

   if (!code_mem || !data_load_mem || !data_run_mem || !bss_run_mem || !zp_run_mem) {
      fprintf(stderr, "nl: config must define CODE, DATA, BSS, and ZEROPAGE segments with valid MEMORY targets\n");
      exit(1);
   }

   shared_rom = code_mem == data_load_mem;
   shared_ram = data_run_mem == bss_run_mem;
   code_load_limit = (uint32_t)code_mem->start + code_mem->size;
   data_load_limit = (uint32_t)data_load_mem->start + data_load_mem->size;
   data_run_limit = (uint32_t)data_run_mem->start + data_run_mem->size;
   bss_run_limit = (uint32_t)bss_run_mem->start + bss_run_mem->size;
   zp_run_limit = (uint32_t)zp_run_mem->start + zp_run_mem->size;

   memset(layout, 0, sizeof(*layout));
   layout->code_load_cur = code_mem->start;
   layout->code_load_end = (uint16_t)(code_mem->start + code_mem->size);
   layout->data_load_cur = data_load_mem->start;
   layout->data_load_end = (uint16_t)(data_load_mem->start + data_load_mem->size);
   layout->data_run_cur = data_run_mem->start;
   layout->data_run_end = (uint16_t)(data_run_mem->start + data_run_mem->size);
   layout->bss_run_cur = bss_run_mem->start;
   layout->bss_run_end = (uint16_t)(bss_run_mem->start + bss_run_mem->size);
   layout->zp_run_cur = zp_run_mem->start;
   layout->zp_run_end = (uint16_t)(zp_run_mem->start + zp_run_mem->size);
   layout->data_load_start = 0;
   layout->data_load_size = 0;
   layout->data_run_start = layout->data_run_cur;
   layout->data_run_size = 0;
   layout->bss_start = layout->bss_run_cur;
   layout->bss_size = 0;
   layout->init_table_addr = 0;
   layout->init_table_size = 0;

   for (i = 0; i < in->object_count; ++i) {
      object_file_t *obj = &in->objects[i];
      obj->place_text_load = layout->code_load_cur;
      if ((uint32_t)obj->place_text_load + obj->text.length > 0xFFFAu ||
          (uint32_t)obj->place_text_load + obj->text.length > code_load_limit) {
         fprintf(stderr, "nl: ROM overflow while placing text from %s\n", obj->origin);
         exit(1);
      }
      layout->code_load_cur = (uint16_t)(layout->code_load_cur + obj->text.length);
   }

   if (shared_rom) {
      layout->data_load_cur = layout->code_load_cur;
   }
   layout->data_load_start = layout->data_load_cur;

   for (i = 0; i < in->object_count; ++i) {
      object_file_t *obj = &in->objects[i];
      obj->place_data_load = layout->data_load_cur;
      if ((uint32_t)obj->place_data_load + obj->data.length > 0xFFFAu ||
          (uint32_t)obj->place_data_load + obj->data.length > data_load_limit) {
         fprintf(stderr, "nl: ROM overflow while placing data load image from %s\n", obj->origin);
         exit(1);
      }
      layout->data_load_cur = (uint16_t)(layout->data_load_cur + obj->data.length);

      obj->place_data_run = layout->data_run_cur;
      if ((uint32_t)obj->place_data_run + obj->data.length > data_run_limit) {
         fprintf(stderr, "nl: RAM overflow while placing DATA run image from %s\n", obj->origin);
         exit(1);
      }
      layout->data_run_cur = (uint16_t)(layout->data_run_cur + obj->data.length);
      layout->data_run_size = (uint16_t)(layout->data_run_size + obj->data.length);
   }

   if (shared_ram && layout->bss_run_cur < layout->data_run_cur) {
      layout->bss_run_cur = layout->data_run_cur;
   }
   layout->bss_start = layout->bss_run_cur;
   layout->data_load_size = (uint16_t)(layout->data_load_cur - layout->data_load_start);

   for (i = 0; i < in->object_count; ++i) {
      object_file_t *obj = &in->objects[i];
      obj->place_bss_run = layout->bss_run_cur;
      if ((uint32_t)obj->place_bss_run + obj->blen > bss_run_limit) {
         fprintf(stderr, "nl: RAM overflow while placing BSS from %s\n", obj->origin);
         exit(1);
      }
      layout->bss_run_cur = (uint16_t)(layout->bss_run_cur + obj->blen);
      layout->bss_size = (uint16_t)(layout->bss_size + obj->blen);

      obj->place_zp_run = layout->zp_run_cur;
      if ((uint32_t)obj->place_zp_run + obj->zlen > zp_run_limit) {
         fprintf(stderr, "nl: ZP overflow while placing ZEROPAGE from %s\n", obj->origin);
         exit(1);
      }
      layout->zp_run_cur = (uint16_t)(layout->zp_run_cur + obj->zlen);
   }

   {
      size_t init_count = count_init_functions_in_input(in);
      size_t init_table_size = (init_count + 1) * 2;

      layout->init_table_addr = layout->data_load_cur;
      layout->init_table_size = (uint16_t)init_table_size;
      if ((uint32_t)layout->init_table_addr + init_table_size > 0xFFFAu ||
          (uint32_t)layout->init_table_addr + init_table_size > data_load_limit) {
         fprintf(stderr, "nl: ROM overflow while placing __init_table\n");
         exit(1);
      }
      layout->data_load_cur = (uint16_t)(layout->data_load_cur + init_table_size);
   }

   for (i = 0; i < in->object_count; ++i) {
      object_file_t *obj = &in->objects[i];
      for (j = 0; j < obj->export_count; ++j) {
         uint16_t addr;

         switch (obj->exports[j].segid) {
            case O65_SEG_TEXT: addr = (uint16_t)(obj->place_text_load + obj->exports[j].value); break;
            case O65_SEG_DATA: addr = (uint16_t)(obj->place_data_run + obj->exports[j].value); break;
            case O65_SEG_BSS:  addr = (uint16_t)(obj->place_bss_run + obj->exports[j].value); break;
            case O65_SEG_ZP:   addr = (uint16_t)(obj->place_zp_run + obj->exports[j].value); break;
            case O65_SEG_ABS:  addr = obj->exports[j].value; break;
            default: continue;
         }
         add_global(layout, obj->exports[j].name, addr, obj->exports[j].segid, obj->origin);
      }
   }
}

static void patch_u8(uint8_t *buf, size_t len, uint32_t off, uint8_t v, const char *origin)
{
   if (off >= len) {
      fprintf(stderr, "nl: relocation offset out of range in %s\n", origin);
      exit(1);
   }
   buf[off] = v;
}

static void patch_u16(uint8_t *buf, size_t len, uint32_t off, uint16_t v, const char *origin)
{
   if (off + 1 >= len) {
      fprintf(stderr, "nl: relocation word offset out of range in %s\n", origin);
      exit(1);
   }
   buf[off] = (uint8_t)(v & 0xFFu);
   buf[off + 1] = (uint8_t)((v >> 8) & 0xFFu);
}

static uint16_t segment_base_runtime(const object_file_t *obj, uint8_t segid, const layout_t *layout)
{
   (void)layout;
   switch (segid) {
      case O65_SEG_TEXT: return obj->place_text_load;
      case O65_SEG_DATA: return obj->place_data_run;
      case O65_SEG_BSS:  return obj->place_bss_run;
      case O65_SEG_ZP:   return obj->place_zp_run;
      case O65_SEG_ABS:  return 0;
      default:           return 0;
   }
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
            fprintf(stderr, "nl: bad undefined-symbol index in %s\n", who);
            exit(1);
         }
         target = lookup_global_addr(layout, obj->undefs[r->undef_index]);
      } else {
         target = (uint16_t)(segment_base_runtime(obj, r->segid, layout) +
            ((r->type == O65_RTYPE_WORD) ? (uint16_t)(seg->data[r->offset] | (seg->data[r->offset + 1] << 8)) : seg->data[r->offset]));
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
            fprintf(stderr, "nl: unsupported relocation type 0x%02x in %s\n", r->type, who);
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
         fprintf(stderr, "nl: image write overflow from %s\n", who);
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

static void build_rom_image(const linker_config_t *cfg, input_set_t *in, const layout_t *layout, uint8_t *image, uint8_t *used)
{
   const memory_region_t *rom = find_memory(cfg, "ROM");
   size_t i;
   uint16_t reset, nmi, irqbrk;
   if (!rom) {
      fprintf(stderr, "nl: ROM memory region not found\n");
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
      fprintf(stderr, "nl: cannot create '%s': %s\n", path, strerror(errno));
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
      fprintf(stderr, "nl: cannot create '%s': %s\n", path, strerror(errno));
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
      fprintf(fp,
         "  %s\n"
         "     TEXT load=$%04X size=$%04zX\n"
         "     DATA load=$%04X run =$%04X size=$%04zX\n"
         "     BSS  run =$%04X size=$%04X\n"
         "     ZP   run =$%04X size=$%04X\n",
         o->origin, o->place_text_load, o->text.length,
         o->place_data_load, o->place_data_run, o->data.length,
         o->place_bss_run, o->blen,
         o->place_zp_run, o->zlen);
   }

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
}

int main(int argc, char **argv)
{
   int argi = 1;
   const char *cfg_path = NULL;
   const char *hex_path;
   const char *map_path = NULL;
   linker_config_t cfg;
   input_set_t inputs;
   layout_t layout;
   uint8_t *image;
   uint8_t *used;
   size_t i;

   memset(&inputs, 0, sizeof(inputs));
   memset(&layout, 0, sizeof(layout));

   if (argc < 3) {
      usage(stderr);
      return 1;
   }

   if (ends_with(argv[argi], ".cfg")) {
      cfg_path = argv[argi++];
      if (argc - argi < 2) {
         usage(stderr);
         return 1;
      }
   }

   while (argi < argc && !ends_with(argv[argi], ".hex")) {
      if (ends_with(argv[argi], ".o65")) {
         inputs.cmd_objects = (object_file_t *)xrealloc(inputs.cmd_objects,
            (inputs.cmd_object_count + 1) * sizeof(*inputs.cmd_objects));
         load_object(argv[argi], &inputs.cmd_objects[inputs.cmd_object_count]);
         inputs.cmd_objects[inputs.cmd_object_count].from_cmdline = 1;
         inputs.order = (input_ref_t *)xrealloc(inputs.order,
            (inputs.order_count + 1) * sizeof(*inputs.order));
         inputs.order[inputs.order_count].kind = INPUT_REF_OBJECT;
         inputs.order[inputs.order_count].index = inputs.cmd_object_count;
         inputs.order_count++;
         inputs.cmd_object_count++;
      } else if (ends_with(argv[argi], ".a65")) {
         inputs.archives = (archive_file_t *)xrealloc(inputs.archives,
            (inputs.archive_count + 1) * sizeof(*inputs.archives));
         load_archive(argv[argi], &inputs.archives[inputs.archive_count]);
         inputs.order = (input_ref_t *)xrealloc(inputs.order,
            (inputs.order_count + 1) * sizeof(*inputs.order));
         inputs.order[inputs.order_count].kind = INPUT_REF_ARCHIVE;
         inputs.order[inputs.order_count].index = inputs.archive_count;
         inputs.order_count++;
         inputs.archive_count++;
      } else {
         fprintf(stderr, "nl: cannot classify input '%s'\n", argv[argi]);
         return 1;
      }
      argi++;
   }

   if (inputs.cmd_object_count == 0 && inputs.archive_count == 0) {
      fprintf(stderr, "nl: no input objects or archives\n");
      return 1;
   }

   if (argi >= argc || !ends_with(argv[argi], ".hex")) {
      fprintf(stderr, "nl: missing output .hex filename\n");
      return 1;
   }
   hex_path = argv[argi++];
   if (argi < argc)
      map_path = argv[argi++];
   if (argi != argc) {
      usage(stderr);
      return 1;
   }

   if (cfg_path)
      parse_cfg_file(&cfg, cfg_path);
   else
      init_default_config(&cfg);

   select_needed_objects(&inputs);
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

   return 0;
}
