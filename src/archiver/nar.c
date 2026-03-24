#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define NAR_MAGIC "NAR65\0\1"
#define NAR_MAGIC_SIZE 7
#define COPY_BUFFER_SIZE 65536

static void usage(FILE *fp)
{
   fprintf(fp,
      "Usage:\n"
      "  nar -c output.a65 obj1.o65 obj2.o65 ... objN.o65\n"
      "  nar -x input.a65\n");
}

static const char *base_name(const char *path)
{
   const char *slash;
   const char *backslash;
   const char *base;

   slash = strrchr(path, '/');
   backslash = strrchr(path, '\\');
   base = path;

   if (slash && slash + 1 > base)
      base = slash + 1;

   if (backslash && backslash + 1 > base)
      base = backslash + 1;

   return base;
}

static bool ends_with(const char *s, const char *suffix)
{
   size_t slen;
   size_t tlen;

   slen = strlen(s);
   tlen = strlen(suffix);

   if (slen < tlen)
      return false;

   return strcmp(s + slen - tlen, suffix) == 0;
}

static int write_u16_le(FILE *fp, uint16_t value)
{
   unsigned char b[2];

   b[0] = (unsigned char)(value & 0xFFu);
   b[1] = (unsigned char)((value >> 8) & 0xFFu);

   return fwrite(b, 1, sizeof(b), fp) == sizeof(b) ? 1 : 0;
}

static int write_u32_le(FILE *fp, uint32_t value)
{
   unsigned char b[4];

   b[0] = (unsigned char)(value & 0xFFu);
   b[1] = (unsigned char)((value >> 8) & 0xFFu);
   b[2] = (unsigned char)((value >> 16) & 0xFFu);
   b[3] = (unsigned char)((value >> 24) & 0xFFu);

   return fwrite(b, 1, sizeof(b), fp) == sizeof(b) ? 1 : 0;
}

static int read_u16_le(FILE *fp, uint16_t *value)
{
   unsigned char b[2];

   if (fread(b, 1, sizeof(b), fp) != sizeof(b))
      return 0;

   *value = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
   return 1;
}

static int read_u32_le(FILE *fp, uint32_t *value)
{
   unsigned char b[4];

   if (fread(b, 1, sizeof(b), fp) != sizeof(b))
      return 0;

   *value = (uint32_t)b[0]
      | ((uint32_t)b[1] << 8)
      | ((uint32_t)b[2] << 16)
      | ((uint32_t)b[3] << 24);
   return 1;
}

static int copy_n_bytes(FILE *in, FILE *out, uint32_t size)
{
   unsigned char buffer[COPY_BUFFER_SIZE];
   uint32_t remaining;

   remaining = size;
   while (remaining > 0) {
      size_t chunk;
      size_t got;

      chunk = remaining > COPY_BUFFER_SIZE ? COPY_BUFFER_SIZE : (size_t)remaining;
      got = fread(buffer, 1, chunk, in);
      if (got != chunk) {
         fprintf(stderr, "nar: unexpected end of file while copying payload\n");
         return 0;
      }

      if (fwrite(buffer, 1, got, out) != got) {
         fprintf(stderr, "nar: write failed: %s\n", strerror(errno));
         return 0;
      }

      remaining -= (uint32_t)got;
   }

   return 1;
}

static long file_size(FILE *fp)
{
   long cur;
   long end;

   cur = ftell(fp);
   if (cur < 0)
      return -1;

   if (fseek(fp, 0, SEEK_END) != 0)
      return -1;

   end = ftell(fp);
   if (end < 0)
      return -1;

   if (fseek(fp, cur, SEEK_SET) != 0)
      return -1;

   return end;
}

static int create_archive(const char *archive_name, int input_count, char **inputs)
{
   FILE *archive;
   int i;

   archive = fopen(archive_name, "wb");
   if (!archive) {
      fprintf(stderr, "nar: cannot create '%s': %s\n", archive_name, strerror(errno));
      return 1;
   }

   if (fwrite(NAR_MAGIC, 1, NAR_MAGIC_SIZE, archive) != NAR_MAGIC_SIZE) {
      fprintf(stderr, "nar: failed to write archive header: %s\n", strerror(errno));
      fclose(archive);
      return 1;
   }

   for (i = 0; i < input_count; ++i) {
      const char *input_name;
      const char *stored_name;
      size_t stored_name_len;
      FILE *in;
      long size;

      input_name = inputs[i];
      stored_name = base_name(input_name);
      stored_name_len = strlen(stored_name);

      if (!ends_with(stored_name, ".o65")) {
         fprintf(stderr, "nar: warning: '%s' does not end with .o65\n", input_name);
      }

      if (stored_name_len == 0 || stored_name_len > 65535u) {
         fprintf(stderr, "nar: invalid member name '%s'\n", input_name);
         fclose(archive);
         return 1;
      }

      in = fopen(input_name, "rb");
      if (!in) {
         fprintf(stderr, "nar: cannot open '%s': %s\n", input_name, strerror(errno));
         fclose(archive);
         return 1;
      }

      size = file_size(in);
      if (size < 0) {
         fprintf(stderr, "nar: cannot determine size of '%s'\n", input_name);
         fclose(in);
         fclose(archive);
         return 1;
      }

      if ((uint64_t)size > UINT32_MAX) {
         fprintf(stderr, "nar: '%s' is too large for this archive format\n", input_name);
         fclose(in);
         fclose(archive);
         return 1;
      }

      if (write_u16_le(archive, (uint16_t)stored_name_len) == 0
            || write_u32_le(archive, (uint32_t)size) == 0
            || fwrite(stored_name, 1, stored_name_len, archive) != stored_name_len) {
         fprintf(stderr, "nar: failed writing member header for '%s': %s\n", input_name, strerror(errno));
         fclose(in);
         fclose(archive);
         return 1;
      }

      if (fseek(in, 0, SEEK_SET) != 0) {
         fprintf(stderr, "nar: seek failed on '%s'\n", input_name);
         fclose(in);
         fclose(archive);
         return 1;
      }

      if (!copy_n_bytes(in, archive, (uint32_t)size)) {
         fclose(in);
         fclose(archive);
         return 1;
      }

      fclose(in);
   }

   fclose(archive);
   return 0;
}

static int extract_archive(const char *archive_name)
{
   FILE *archive;
   unsigned char magic[NAR_MAGIC_SIZE];

   archive = fopen(archive_name, "rb");
   if (!archive) {
      fprintf(stderr, "nar: cannot open '%s': %s\n", archive_name, strerror(errno));
      return 1;
   }

   if (fread(magic, 1, NAR_MAGIC_SIZE, archive) != NAR_MAGIC_SIZE) {
      fprintf(stderr, "nar: '%s' is too short to be a nar archive\n", archive_name);
      fclose(archive);
      return 1;
   }

   if (memcmp(magic, NAR_MAGIC, NAR_MAGIC_SIZE) != 0) {
      fprintf(stderr, "nar: '%s' is not a nar archive\n", archive_name);
      fclose(archive);
      return 1;
   }

   for (;;) {
      uint16_t name_len;
      uint32_t size;
      char *name;
      FILE *out;
      int c;

      c = fgetc(archive);
      if (c == EOF) {
         if (feof(archive))
            break;
         fprintf(stderr, "nar: read error on '%s'\n", archive_name);
         fclose(archive);
         return 1;
      }
      if (ungetc(c, archive) == EOF) {
         fprintf(stderr, "nar: internal read error on '%s'\n", archive_name);
         fclose(archive);
         return 1;
      }

      if (!read_u16_le(archive, &name_len) || !read_u32_le(archive, &size)) {
         fprintf(stderr, "nar: truncated member header in '%s'\n", archive_name);
         fclose(archive);
         return 1;
      }

      if (name_len == 0) {
         fprintf(stderr, "nar: invalid zero-length member name in '%s'\n", archive_name);
         fclose(archive);
         return 1;
      }

      name = (char *)malloc((size_t)name_len + 1u);
      if (!name) {
         fprintf(stderr, "nar: out of memory\n");
         fclose(archive);
         return 1;
      }

      if (fread(name, 1, name_len, archive) != name_len) {
         fprintf(stderr, "nar: truncated member name in '%s'\n", archive_name);
         free(name);
         fclose(archive);
         return 1;
      }
      name[name_len] = '\0';

      if (strchr(name, '/') || strchr(name, '\\')) {
         fprintf(stderr, "nar: refusing suspicious member name '%s'\n", name);
         free(name);
         fclose(archive);
         return 1;
      }

      out = fopen(name, "wb");
      if (!out) {
         fprintf(stderr, "nar: cannot create '%s': %s\n", name, strerror(errno));
         free(name);
         fclose(archive);
         return 1;
      }

      if (!copy_n_bytes(archive, out, size)) {
         fclose(out);
         free(name);
         fclose(archive);
         return 1;
      }

      fclose(out);
      free(name);
   }

   fclose(archive);
   return 0;
}

int main(int argc, char **argv)
{
   if (argc < 3) {
      usage(stderr);
      return 1;
   }

   if (strcmp(argv[1], "-c") == 0) {
      if (argc < 4) {
         usage(stderr);
         return 1;
      }
      return create_archive(argv[2], argc - 3, &argv[3]);
   }

   if (strcmp(argv[1], "-x") == 0) {
      if (argc != 3) {
         usage(stderr);
         return 1;
      }
      return extract_archive(argv[2]);
   }

   usage(stderr);
   return 1;
}
