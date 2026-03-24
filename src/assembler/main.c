#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>

#include "ir.h"
#include "asm_pass.h"
#include "ihex.h"
#include "listing.h"
#include "source_loader.h"
#include "util.h"

int yyparse(void);
extern FILE *yyin;
extern program_ir_t g_program;

typedef struct
{
   const char *input_path;

   bool want_hex;
   bool want_lst;
   bool want_map;

   const char *hex_path_arg;
   const char *lst_path_arg;
   const char *map_path_arg;

   char *hex_path;
   char *lst_path;
   char *map_path;
} options_t;

static void usage(const char *argv0)
{
   fprintf(stderr,
      "usage: %s -i <input.s> [--hex[=file]] [--lst[=file]] [--map[=file]]\n"
      "\n"
      "options:\n"
      "   -i, --input <file>   input assembly source (required)\n"
      "       --hex[=file]    write Intel HEX output\n"
      "       --lst[=file]    write listing output\n"
      "       --map[=file]    write map output\n"
      "   -h, --help          show this help\n"
      "\n"
      "examples:\n"
      "   %s -i prog.s --hex\n"
      "   %s -i prog.s --hex=prog.hex --lst --map\n"
      "   %s --input prog.s --lst=custom.lst\n",
      argv0, argv0, argv0, argv0);
}

static char *make_output_path(const char *input_path, const char *ext)
{
   const char *slash;
   const char *base;
   const char *dot;
   size_t dir_len;
   size_t stem_len;
   size_t ext_len;
   char *out;

   slash = strrchr(input_path, '/');
#ifdef _WIN32
   {
      const char *bslash = strrchr(input_path, '\\');
      if (!slash || (bslash && bslash > slash))
         slash = bslash;
   }
#endif

   if (slash) {
      dir_len = (size_t)(slash - input_path + 1);
      base = slash + 1;
   } else {
      dir_len = 0;
      base = input_path;
   }

   dot = strrchr(base, '.');
   if (dot)
      stem_len = (size_t)(dot - base);
   else
      stem_len = strlen(base);

   ext_len = strlen(ext);

   out = malloc(dir_len + stem_len + 1 + ext_len + 1);
   if (!out) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   if (dir_len)
      memcpy(out, input_path, dir_len);

   memcpy(out + dir_len, base, stem_len);
   out[dir_len + stem_len] = '.';
   memcpy(out + dir_len + stem_len + 1, ext, ext_len);
   out[dir_len + stem_len + 1 + ext_len] = '\0';

   return out;
}

static bool parse_args(int argc, char **argv, options_t *opt)
{
   int ch;
   int option_index = 0;

   static struct option long_options[] = {
      { "input", required_argument, NULL, 'i' },
      { "hex",   optional_argument, NULL, 1000 },
      { "lst",   optional_argument, NULL, 1001 },
      { "map",   optional_argument, NULL, 1002 },
      { "help",  no_argument,       NULL, 'h' },
      { NULL,    0,                 NULL, 0 }
   };

   memset(opt, 0, sizeof(*opt));

   while ((ch = getopt_long(argc, argv, "hi:", long_options, &option_index)) != -1) {
      switch (ch) {
      case 'h':
         usage(argv[0]);
         exit(0);

      case 'i':
         opt->input_path = optarg;
         break;

      case 1000:
         opt->want_hex = true;
         opt->hex_path_arg = optarg;
         break;

      case 1001:
         opt->want_lst = true;
         opt->lst_path_arg = optarg;
         break;

      case 1002:
         opt->want_map = true;
         opt->map_path_arg = optarg;
         break;

      default:
         usage(argv[0]);
         return false;
      }
   }

   if (!opt->input_path) {
      fprintf(stderr, "error: input file is required\n");
      usage(argv[0]);
      return false;
   }

   if (optind != argc) {
      fprintf(stderr, "error: unexpected positional argument: %s\n", argv[optind]);
      usage(argv[0]);
      return false;
   }

   if (opt->want_hex) {
      if (opt->hex_path_arg)
         opt->hex_path = xstrdup(opt->hex_path_arg);
      else
         opt->hex_path = make_output_path(opt->input_path, "hex");
   }

   if (opt->want_lst) {
      if (opt->lst_path_arg)
         opt->lst_path = xstrdup(opt->lst_path_arg);
      else
         opt->lst_path = make_output_path(opt->input_path, "lst");
   }

   if (opt->want_map) {
      if (opt->map_path_arg)
         opt->map_path = xstrdup(opt->map_path_arg);
      else
         opt->map_path = make_output_path(opt->input_path, "map");
   }

   return true;
}

static void free_options(options_t *opt)
{
   free(opt->hex_path);
   free(opt->lst_path);
   free(opt->map_path);
}

int main(int argc, char **argv)
{
   options_t opt;
   asm_context_t ctx;
   listing_writer_t lst;
   FILE *hexfp = NULL;
   FILE *mapfp = NULL;
   bool lst_open = false;
   bool ctx_init = false;
   bool ir_init = false;
   int rc = 1;

   if (!parse_args(argc, argv, &opt))
      return 1;

   yyin = source_loader_open_expanded(opt.input_path);
   if (!yyin) {
      perror(opt.input_path);
      free_options(&opt);
      return 1;
   }

   if (opt.want_hex) {
      hexfp = fopen(opt.hex_path, "w");
      if (!hexfp) {
         perror(opt.hex_path);
         goto cleanup;
      }
   }

   if (opt.want_lst) {
      if (!listing_open(&lst, opt.lst_path)) {
         perror(opt.lst_path);
         goto cleanup;
      }
      lst_open = true;
   }

   if (opt.want_map) {
      mapfp = fopen(opt.map_path, "w");
      if (!mapfp) {
         perror(opt.map_path);
         goto cleanup;
      }
   }

   program_ir_init(&g_program);
   ir_init = true;

   rc = yyparse();
   fclose(yyin);
   yyin = NULL;

   if (rc != 0)
      goto cleanup;

   asm_context_init(&ctx, &g_program, lst_open ? &lst : NULL);
   ctx_init = true;

   asm_relax(&ctx);
   asm_pass2(&ctx);

   if (mapfp) {
      if (!asm_write_map_file(mapfp, &ctx)) {
         rc = 1;
         goto cleanup;
      }
   }

   if (ctx.error_count == 0 && hexfp) {
      if (!ihex_dump(hexfp, &ctx.image)) {
         rc = 1;
         goto cleanup;
      }
   }

   rc = ctx.error_count ? 1 : 0;

cleanup:
   if (ctx_init)
      asm_context_free(&ctx);

   if (mapfp)
      fclose(mapfp);

   if (lst_open)
      listing_close(&lst);

   if (hexfp)
      fclose(hexfp);

   if (yyin) {
      fclose(yyin);
      yyin = NULL;
   }

   if (ir_init)
      program_ir_free(&g_program);

   free_options(&opt);
   return rc;
}
