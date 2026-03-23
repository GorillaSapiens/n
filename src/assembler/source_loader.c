#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include "source_loader.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define INCLUDE_MAX_DEPTH 64
#define LINEBUF_SIZE      8192

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

static int parse_include_line(const char *line, char *included_path, size_t included_path_sz)
{
   const char *p;
   const char *start;
   size_t len;

   p = line;

   while (*p == ' ' || *p == '\t' || *p == '\r')
      p++;

   if (strncmp(p, ".include", 8) != 0)
      return 0;

   p += 8;

   if (!isspace((unsigned char)*p))
      return 0;

   while (*p == ' ' || *p == '\t' || *p == '\r')
      p++;

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

   while (*p == ' ' || *p == '\t' || *p == '\r')
      p++;

   if (*p == ';') {
      while (*p && *p != '\n')
         p++;
   }

   while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
      p++;

   return *p == '\0';
}

static int emit_marker(FILE *out_fp, const char *path, long line_no)
{
   /*
      These marker lines are consumed by the lexer and never seen by the parser.
      They let us preserve original filename + line number through .include
      expansion without turning the lexer into a nested include stack manager.
   */
   return fprintf(out_fp, "@@FILE %ld %s\n", line_no, path) > 0;
}

static int expand_file_recursive(const char *path, FILE *out_fp, int depth)
{
   FILE *in_fp;
   char line[LINEBUF_SIZE];
   char base_dir[PATH_MAX];
   char include_name[PATH_MAX];
   char include_path[PATH_MAX];
   long line_no;

   if (depth > INCLUDE_MAX_DEPTH) {
      fprintf(stderr, "include nesting too deep near %s\n", path);
      return 0;
   }

   in_fp = fopen(path, "r");
   if (!in_fp) {
      perror(path);
      return 0;
   }

   if (!emit_marker(out_fp, path, 1)) {
      fclose(in_fp);
      return 0;
   }

   path_dirname(path, base_dir, sizeof(base_dir));
   line_no = 1;

   while (fgets(line, sizeof(line), in_fp) != NULL) {
      if (parse_include_line(line, include_name, sizeof(include_name))) {
         path_join(base_dir, include_name, include_path, sizeof(include_path));

         if (!expand_file_recursive(include_path, out_fp, depth + 1)) {
            fclose(in_fp);
            return 0;
         }

         line_no++;
         if (!emit_marker(out_fp, path, line_no)) {
            fclose(in_fp);
            return 0;
         }
      } else {
         if (fputs(line, out_fp) == EOF) {
            fprintf(stderr, "write error while expanding includes\n");
            fclose(in_fp);
            return 0;
         }
         line_no++;
      }
   }

   fclose(in_fp);
   return 1;
}

FILE *source_loader_open_expanded(const char *root_path)
{
   FILE *tmp_fp;

   tmp_fp = tmpfile();
   if (!tmp_fp) {
      perror("tmpfile");
      return NULL;
   }

   if (!expand_file_recursive(root_path, tmp_fp, 0)) {
      fclose(tmp_fp);
      return NULL;
   }

   rewind(tmp_fp);
   return tmp_fp;
}
