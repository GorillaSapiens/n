#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "source_loader.h"
#include "util.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define INCLUDE_MAX_DEPTH 64
#define LINEBUF_SIZE      8192
#define MACRO_MAX_PARAMS  32
#define MACRO_MAX_DEPTH   64

typedef struct strlist {
   char **items;
   int count;
   int cap;
} strlist_t;

typedef struct macro_def {
   char *name;
   char *def_file;
   int def_line;
   char **params;
   int param_count;
   strlist_t body_lines;
   struct macro_def *next;
} macro_def_t;

typedef struct macro_table {
   macro_def_t *head;
   long next_expansion_id;
} macro_table_t;

typedef struct expand_ctx {
   macro_table_t macros;
   int macro_depth;
} expand_ctx_t;

static void strlist_init(strlist_t *lst)
{
   lst->items = NULL;
   lst->count = 0;
   lst->cap = 0;
}

static void strlist_free(strlist_t *lst)
{
   int i;

   for (i = 0; i < lst->count; i++)
      free(lst->items[i]);

   free(lst->items);
   lst->items = NULL;
   lst->count = 0;
   lst->cap = 0;
}

static void strlist_push(strlist_t *lst, const char *s)
{
   if (lst->count == lst->cap) {
      int new_cap;
      char **new_items;

      new_cap = lst->cap ? lst->cap * 2 : 8;
      new_items = (char **)realloc(lst->items, (size_t)new_cap * sizeof(lst->items[0]));
      if (!new_items) {
         fprintf(stderr, "out of memory\n");
         exit(1);
      }

      lst->items = new_items;
      lst->cap = new_cap;
   }

   lst->items[lst->count++] = xstrdup(s);
}

static void macro_table_init(macro_table_t *tab)
{
   tab->head = NULL;
   tab->next_expansion_id = 1;
}

static void macro_table_free(macro_table_t *tab)
{
   macro_def_t *m;
   macro_def_t *next;
   int i;

   for (m = tab->head; m; m = next) {
      next = m->next;
      free(m->name);
      free(m->def_file);
      for (i = 0; i < m->param_count; i++)
         free(m->params[i]);
      free(m->params);
      strlist_free(&m->body_lines);
      free(m);
   }

   tab->head = NULL;
}

static macro_def_t *macro_find(macro_table_t *tab, const char *name)
{
   macro_def_t *m;

   for (m = tab->head; m; m = m->next) {
      if (!strcmp(m->name, name))
         return m;
   }

   return NULL;
}

static macro_def_t *macro_create(const char *name, const char *file, int line)
{
   macro_def_t *m;

   m = (macro_def_t *)calloc(1, sizeof(*m));
   if (!m) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   m->name = xstrdup(name);
   m->def_file = xstrdup(file);
   m->def_line = line;
   strlist_init(&m->body_lines);
   return m;
}

static void macro_add(macro_table_t *tab, macro_def_t *m)
{
   m->next = tab->head;
   tab->head = m;
}

static void path_dirname(const char *path, char *out_dir, size_t out_sz)
{
   const char *slash;
   size_t len;

   slash = strrchr(path, '/');
   if (!slash) {
      snprintf(out_dir, out_sz, ".");
      return;
   }

   len = (size_t)(slash - path);
   if (len == 0)
      len = 1;

   if (len >= out_sz)
      len = out_sz - 1;

   memcpy(out_dir, path, len);
   out_dir[len] = '\0';
}

static int path_is_absolute(const char *path)
{
   return path[0] == '/';
}

static void path_join(const char *base_dir, const char *child, char *out_path, size_t out_sz)
{
   if (path_is_absolute(child)) {
      snprintf(out_path, out_sz, "%s", child);
      return;
   }

   if (!strcmp(base_dir, "."))
      snprintf(out_path, out_sz, "%s", child);
   else
      snprintf(out_path, out_sz, "%s/%s", base_dir, child);
}

static const char *skip_ws(const char *p)
{
   while (*p == ' ' || *p == '\t' || *p == '\r')
      p++;
   return p;
}

static void rstrip_inplace(char *s)
{
   size_t n;

   n = strlen(s);
   while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
      s[n - 1] = '\0';
      n--;
   }
}

static int is_ident_start(int c)
{
   return isalpha(c) || c == '_' || c == '@' || c == '?';
}

static int is_ident_char(int c)
{
   return isalnum(c) || c == '_' || c == '@' || c == '?';
}

static int emit_marker(FILE *out_fp, const char *path, long line_no)
{
   return fprintf(out_fp, "@@FILE %ld %s\n", line_no, path) > 0;
}

static int parse_include_line(const char *line, char *included_path, size_t included_path_sz)
{
   const char *p;
   const char *start;
   size_t len;

   p = skip_ws(line);

   if (strncmp(p, ".include", 8) != 0)
      return 0;

   p += 8;
   if (!isspace((unsigned char)*p))
      return 0;

   p = skip_ws(p);
   if (*p != '"')
      return 0;

   p++;
   start = p;

   while (*p && *p != '"' && *p != '\n' && *p != '\r')
      p++;

   if (*p != '"')
      return 0;

   len = (size_t)(p - start);
   if (len == 0 || len >= included_path_sz)
      return 0;

   memcpy(included_path, start, len);
   included_path[len] = '\0';

   p++;
   p = skip_ws(p);

   if (*p == ';') {
      while (*p && *p != '\n')
         p++;
   }

   p = skip_ws(p);
   return *p == '\0' || *p == '\n';
}

static int parse_macro_header(const char *line,
                              char *name_out,
                              size_t name_out_sz,
                              char ***params_out,
                              int *param_count_out)
{
   const char *p;
   char **params;
   int param_count;
   char ident[256];

   p = skip_ws(line);
   if (strncasecmp(p, "MACRO", 5) != 0)
      return 0;

   p += 5;
   if (!isspace((unsigned char)*p))
      return 0;

   p = skip_ws(p);
   if (!is_ident_start((unsigned char)*p))
      return 0;

   {
      size_t n = 0;
      while (is_ident_char((unsigned char)*p) && n + 1 < sizeof(ident))
         ident[n++] = *p++;
      ident[n] = '\0';
   }

   if (strlen(ident) >= name_out_sz)
      return 0;
   strcpy(name_out, ident);

   params = NULL;
   param_count = 0;

   p = skip_ws(p);
   if (*p == ';' || *p == '\0' || *p == '\n' || *p == '\r') {
      *params_out = NULL;
      *param_count_out = 0;
      return 1;
   }

   while (*p && *p != '\n' && *p != '\r' && *p != ';') {
      char param[256];
      size_t n = 0;
      char **new_params;

      p = skip_ws(p);
      if (!is_ident_start((unsigned char)*p))
         break;

      while (is_ident_char((unsigned char)*p) && n + 1 < sizeof(param))
         param[n++] = *p++;
      param[n] = '\0';

      new_params = (char **)realloc(params, (size_t)(param_count + 1) * sizeof(params[0]));
      if (!new_params) {
         fprintf(stderr, "out of memory\n");
         exit(1);
      }
      params = new_params;
      params[param_count++] = xstrdup(param);

      p = skip_ws(p);
      if (*p == ',') {
         p++;
         continue;
      }
      break;
   }

   *params_out = params;
   *param_count_out = param_count;
   return 1;
}

static int is_endm_line(const char *line)
{
   const char *p;

   p = skip_ws(line);
   if (strncasecmp(p, "ENDM", 4) != 0)
      return 0;

   p += 4;
   p = skip_ws(p);
   return *p == '\0' || *p == '\n' || *p == '\r' || *p == ';';
}

static char *trim_copy_no_comment(const char *line)
{
   const char *p;
   const char *end;
   char *out;
   size_t len;

   p = skip_ws(line);
   end = p;

   while (*end && *end != '\n' && *end != '\r' && *end != ';')
      end++;

   while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
      end--;

   len = (size_t)(end - p);
   out = (char *)malloc(len + 1);
   if (!out) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   memcpy(out, p, len);
   out[len] = '\0';
   return out;
}

static int parse_invocation(const char *line,
                            char *name_out,
                            size_t name_out_sz,
                            strlist_t *args_out)
{
   const char *p;
   char ident[256];

   strlist_init(args_out);

   p = skip_ws(line);
   if (!is_ident_start((unsigned char)*p))
      return 0;

   {
      size_t n = 0;
      while (is_ident_char((unsigned char)*p) && n + 1 < sizeof(ident))
         ident[n++] = *p++;
      ident[n] = '\0';
   }

   if (strlen(ident) >= name_out_sz)
      return 0;
   strcpy(name_out, ident);

   p = skip_ws(p);
   if (*p == '\0' || *p == '\n' || *p == '\r' || *p == ';')
      return 1;

   while (*p && *p != '\n' && *p != '\r') {
      const char *start;
      const char *end;
      int depth;
      size_t len;
      char *arg;

      p = skip_ws(p);
      if (*p == ';' || *p == '\0' || *p == '\n' || *p == '\r')
         break;

      start = p;
      end = p;
      depth = 0;

      while (*end && *end != '\n' && *end != '\r') {
         if (*end == ';' && depth == 0)
            break;
         if (*end == '(')
            depth++;
         else if (*end == ')' && depth > 0)
            depth--;
         else if (*end == ',' && depth == 0)
            break;
         end++;
      }

      while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
         end--;

      len = (size_t)(end - start);
      arg = (char *)malloc(len + 1);
      if (!arg) {
         fprintf(stderr, "out of memory\n");
         exit(1);
      }

      memcpy(arg, start, len);
      arg[len] = '\0';
      strlist_push(args_out, arg);
      free(arg);

      p = end;
      if (*p == ',')
         p++;
      else
         break;
   }

   return 1;
}

static const char *param_lookup(const macro_def_t *m, const strlist_t *args, const char *name)
{
   int i;

   for (i = 0; i < m->param_count; i++) {
      if (!strcmp(m->params[i], name)) {
         if (i < args->count)
            return args->items[i];
         return "";
      }
   }

   return NULL;
}

static char *rewrite_macro_line(const macro_def_t *m,
                                const strlist_t *args,
                                const char *line,
                                long expansion_id)
{
   char out[LINEBUF_SIZE * 2];
   size_t oi;
   size_t i;

   oi = 0;

   for (i = 0; line[i] != '\0' && oi + 2 < sizeof(out); ) {
      if (is_ident_start((unsigned char)line[i])) {
         char tok[512];
         size_t ti;
         const char *subst;

         ti = 0;
         while (is_ident_char((unsigned char)line[i]) && ti + 1 < sizeof(tok))
            tok[ti++] = line[i++];
         tok[ti] = '\0';

         if (tok[0] == '@') {
            char local_buf[1024];
            snprintf(local_buf, sizeof(local_buf), "@__M%ld_%s", expansion_id, tok + 1);
            subst = local_buf;

            if (oi + strlen(subst) + 1 >= sizeof(out))
               break;
            memcpy(out + oi, subst, strlen(subst));
            oi += strlen(subst);
         } else {
            subst = param_lookup(m, args, tok);
            if (!subst)
               subst = tok;

            if (oi + strlen(subst) + 1 >= sizeof(out))
               break;
            memcpy(out + oi, subst, strlen(subst));
            oi += strlen(subst);
         }
      } else {
         out[oi++] = line[i++];
      }
   }

   out[oi] = '\0';
   return xstrdup(out);
}

static int expand_text_lines(expand_ctx_t *ctx,
                             const char *logical_file,
                             int logical_line,
                             const strlist_t *lines,
                             FILE *out_fp);

static int expand_macro_invocation(expand_ctx_t *ctx,
                                   const macro_def_t *m,
                                   const strlist_t *args,
                                   const char *invoke_file,
                                   int invoke_line,
                                   FILE *out_fp)
{
   strlist_t rewritten;
   int i;
   long expansion_id;
   int ok;

   if (ctx->macro_depth >= MACRO_MAX_DEPTH) {
      fprintf(stderr, "%s:%d: macro expansion too deep\n", invoke_file, invoke_line);
      return 0;
   }

   if (args->count != m->param_count) {
      fprintf(stderr,
              "%s:%d: macro '%s' expects %d args, got %d\n",
              invoke_file, invoke_line, m->name, m->param_count, args->count);
      return 0;
   }

   expansion_id = ctx->macros.next_expansion_id++;
   strlist_init(&rewritten);

   /*
      Macro expansion is done before lexing/parsing for the same reason as
      .include: it keeps the real assembler simple.

      Parameters are substituted on identifier boundaries, and macro-local
      labels beginning with '@' are renamed uniquely per expansion so repeated
      macro calls do not collide.
   */
   for (i = 0; i < m->body_lines.count; i++) {
      char *rw;

      rw = rewrite_macro_line(m, args, m->body_lines.items[i], expansion_id);
      strlist_push(&rewritten, rw);
      free(rw);
   }

   ctx->macro_depth++;
   ok = expand_text_lines(ctx, invoke_file, invoke_line, &rewritten, out_fp);
   ctx->macro_depth--;

   strlist_free(&rewritten);
   return ok;
}

static int expand_text_lines(expand_ctx_t *ctx,
                             const char *logical_file,
                             int logical_line,
                             const strlist_t *lines,
                             FILE *out_fp)
{
   int i;

   for (i = 0; i < lines->count; i++) {
      char name[256];
      strlist_t args;
      macro_def_t *m;
      int this_line;

      this_line = logical_line + i;

      if (!emit_marker(out_fp, logical_file, this_line))
         return 0;

      if (!parse_invocation(lines->items[i], name, sizeof(name), &args))
         name[0] = '\0';

      m = name[0] ? macro_find(&ctx->macros, name) : NULL;
      if (m) {
         if (!expand_macro_invocation(ctx, m, &args, logical_file, this_line, out_fp)) {
            strlist_free(&args);
            return 0;
         }
         strlist_free(&args);
         continue;
      }

      strlist_free(&args);

      if (fputs(lines->items[i], out_fp) == EOF)
         return 0;
      if (lines->items[i][0] == '\0' || lines->items[i][strlen(lines->items[i]) - 1] != '\n') {
         if (fputc('\n', out_fp) == EOF)
            return 0;
      }
   }

   return 1;
}

static int read_macro_definition(FILE *in_fp,
                                 expand_ctx_t *ctx,
                                 const char *cur_file,
                                 int *line_no_io,
                                 const char *macro_name,
                                 char **params,
                                 int param_count)
{
   char line[LINEBUF_SIZE];
   macro_def_t *m;

   if (macro_find(&ctx->macros, macro_name)) {
      fprintf(stderr, "%s:%d: duplicate macro '%s'\n", cur_file, *line_no_io, macro_name);
      for (int i = 0; i < param_count; i++)
         free(params[i]);
      free(params);
      return 0;
   }

   m = macro_create(macro_name, cur_file, *line_no_io);
   m->params = params;
   m->param_count = param_count;

   while (fgets(line, sizeof(line), in_fp) != NULL) {
      (*line_no_io)++;

      if (is_endm_line(line)) {
         macro_add(&ctx->macros, m);
         return 1;
      }

      strlist_push(&m->body_lines, line);
   }

   fprintf(stderr, "%s:%d: unterminated MACRO '%s'\n", cur_file, m->def_line, m->name);
   return 0;
}

static int expand_file_recursive(expand_ctx_t *ctx,
                                 const char *path,
                                 FILE *out_fp,
                                 int depth)
{
   FILE *in_fp;
   char line[LINEBUF_SIZE];
   char base_dir[PATH_MAX];
   char include_name[PATH_MAX];
   int line_no;

   if (depth > INCLUDE_MAX_DEPTH) {
      fprintf(stderr, "include nesting too deep near %s\n", path);
      return 0;
   }

   in_fp = fopen(path, "r");
   if (!in_fp) {
      perror(path);
      return 0;
   }

   path_dirname(path, base_dir, sizeof(base_dir));
   line_no = 0;

   while (fgets(line, sizeof(line), in_fp) != NULL) {
      char macro_name[256];
      char **params;
      int param_count;
      char invoke_name[256];
      strlist_t invoke_args;
      macro_def_t *m;

      line_no++;

      if (parse_include_line(line, include_name, sizeof(include_name))) {
         char include_path[PATH_MAX];

         path_join(base_dir, include_name, include_path, sizeof(include_path));
         if (!expand_file_recursive(ctx, include_path, out_fp, depth + 1)) {
            fclose(in_fp);
            return 0;
         }
         continue;
      }

      params = NULL;
      param_count = 0;
      if (parse_macro_header(line, macro_name, sizeof(macro_name), &params, &param_count)) {
         if (!read_macro_definition(in_fp, ctx, path, &line_no, macro_name, params, param_count)) {
            fclose(in_fp);
            return 0;
         }
         continue;
      }

      if (parse_invocation(line, invoke_name, sizeof(invoke_name), &invoke_args)) {
         m = macro_find(&ctx->macros, invoke_name);
         if (m) {
            if (!emit_marker(out_fp, path, line_no)) {
               strlist_free(&invoke_args);
               fclose(in_fp);
               return 0;
            }

            if (!expand_macro_invocation(ctx, m, &invoke_args, path, line_no, out_fp)) {
               strlist_free(&invoke_args);
               fclose(in_fp);
               return 0;
            }

            strlist_free(&invoke_args);
            continue;
         }
         strlist_free(&invoke_args);
      }

      if (!emit_marker(out_fp, path, line_no)) {
         fclose(in_fp);
         return 0;
      }

      if (fputs(line, out_fp) == EOF) {
         fprintf(stderr, "write error while expanding input\n");
         fclose(in_fp);
         return 0;
      }
   }

   fclose(in_fp);
   return 1;
}

FILE *source_loader_open_expanded(const char *root_path)
{
   FILE *tmp_fp;
   expand_ctx_t ctx;

   tmp_fp = tmpfile();
   if (!tmp_fp) {
      perror("tmpfile");
      return NULL;
   }

   macro_table_init(&ctx.macros);
   ctx.macro_depth = 0;

   if (!expand_file_recursive(&ctx, root_path, tmp_fp, 0)) {
      macro_table_free(&ctx.macros);
      fclose(tmp_fp);
      return NULL;
   }

   macro_table_free(&ctx.macros);
   rewind(tmp_fp);
   return tmp_fp;
}
