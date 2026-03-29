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
#include "o65.h"
#include "util.h"

int yyparse(void);
extern FILE *yyin;
extern program_ir_t g_program;

typedef struct
{
   const char *input_path;
   char **include_dirs;
   int include_dir_count;

   bool want_hex;
   bool want_lst;
   bool want_map;
   bool want_o65;

   const char *hex_path_arg;
   const char *lst_path_arg;
   const char *map_path_arg;
   const char *o65_path_arg;

   char *hex_path;
   char *lst_path;
   char *map_path;
   char *o65_path;
} options_t;

static void usage(const char *argv0)
{
   fprintf(stderr,
      "usage: %s [options] file\n"
      "\n"
      "options:\n"
      "   -o, --output <file>  write relocatable o65 object output\n"
      "   -I, --include <dir>  add directory to include search path\n"
      "       --hex[=file]     write Intel HEX output\n"
      "       --lst[=file]     write listing output\n"
      "       --map[=file]     write map output\n"
      "   -i, --input <file>   compatibility alias for positional input file\n"
      "       --o65[=file]     compatibility alias for object output\n"
      "   -h, --help           show this help\n"
      "\n"
      "notes:\n"
      "   if no primary output is selected, relocatable o65 output is written to a.out\n"
      "   use --o65 without a filename to preserve the old derived-name behavior (.o65)\n"
      "\n"
      "examples:\n"
      "   %s prog.s\n"
      "   %s -o prog.o65 prog.s\n"
      "   %s -I include --hex=prog.hex --lst --map prog.s\n"
      "   %s --input prog.s -I common -I board --o65=prog.o65\n",
      argv0, argv0, argv0, argv0, argv0);
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

static void add_include_dir(options_t *opt, const char *dir)
{
   char **new_dirs;

   new_dirs = realloc(opt->include_dirs, sizeof(*opt->include_dirs) * (size_t)(opt->include_dir_count + 1));
   if (!new_dirs) {
      fprintf(stderr, "out of memory\n");
      exit(1);
   }

   opt->include_dirs = new_dirs;
   opt->include_dirs[opt->include_dir_count++] = xstrdup(dir);
}

static bool parse_args(int argc, char **argv, options_t *opt)
{
   int ch;
   int option_index = 0;
   const char *positional_input = NULL;
   bool default_o65_output = false;

   static struct option long_options[] = {
      { "input", required_argument, NULL, 'i' },
      { "include", required_argument, NULL, 'I' },
      { "output", required_argument, NULL, 'o' },
      { "hex",   optional_argument, NULL, 1000 },
      { "lst",   optional_argument, NULL, 1001 },
      { "map",   optional_argument, NULL, 1002 },
      { "o65",   optional_argument, NULL, 1003 },
      { "help",  no_argument,       NULL, 'h' },
      { NULL,    0,                 NULL, 0 }
   };

   memset(opt, 0, sizeof(*opt));

   while ((ch = getopt_long(argc, argv, "hI:i:o:", long_options, &option_index)) != -1) {
      switch (ch) {
      case 'h':
         usage(argv[0]);
         exit(0);

      case 'i':
         if (opt->input_path) {
            fprintf(stderr, "error: input file specified more than once\n");
            usage(argv[0]);
            return false;
         }
         opt->input_path = optarg;
         break;

      case 'I':
         add_include_dir(opt, optarg);
         break;

      case 'o':
         opt->want_o65 = true;
         opt->o65_path_arg = optarg;
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

      case 1003:
         opt->want_o65 = true;
         opt->o65_path_arg = optarg;
         break;

      default:
         usage(argv[0]);
         return false;
      }
   }

   while (optind < argc) {
      if (positional_input) {
         fprintf(stderr, "error: unexpected positional argument: %s\n", argv[optind]);
         usage(argv[0]);
         return false;
      }
      positional_input = argv[optind++];
   }

   if (positional_input) {
      if (opt->input_path) {
         fprintf(stderr, "error: input file specified both positionally and with -i/--input\n");
         usage(argv[0]);
         return false;
      }
      opt->input_path = positional_input;
   }

   if (!opt->input_path) {
      fprintf(stderr, "error: input file is required\n");
      usage(argv[0]);
      return false;
   }

   if (!opt->want_hex && !opt->want_o65) {
      opt->want_o65 = true;
      default_o65_output = true;
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

   if (opt->want_o65) {
      if (opt->o65_path_arg)
         opt->o65_path = xstrdup(opt->o65_path_arg);
      else if (default_o65_output)
         opt->o65_path = xstrdup("a.out");
      else
         opt->o65_path = make_output_path(opt->input_path, "o65");
   }

   return true;
}

static void free_options(options_t *opt)
{
   int i;

   for (i = 0; i < opt->include_dir_count; i++)
      free(opt->include_dirs[i]);
   free(opt->include_dirs);

   free(opt->hex_path);
   free(opt->lst_path);
   free(opt->map_path);
   free(opt->o65_path);
}

int main(int argc, char **argv)
{
   options_t opt;
   asm_context_t ctx;
   listing_writer_t lst;
   FILE *hexfp = NULL;
   FILE *mapfp = NULL;
   FILE *o65fp = NULL;
   bool lst_open = false;
   bool ctx_init = false;
   bool ir_init = false;
   int rc = 1;

   if (!parse_args(argc, argv, &opt))
      return 1;

   for (int i = 0; i < opt.include_dir_count; i++)
      source_loader_add_include_dir(opt.include_dirs[i]);

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

   if (opt.want_o65) {
      o65fp = fopen(opt.o65_path, "wb");
      if (!o65fp) {
         perror(opt.o65_path);
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

   asm_context_init(&ctx, &g_program, lst_open ? &lst : NULL, opt.want_o65);
   ctx_init = true;

   asm_relax(&ctx);

   if (!opt.want_o65)
      asm_pass2(&ctx);

   if (ctx.error_count == 0 && o65fp) {
      if (!o65_write_object_file(o65fp, &ctx)) {
         rc = 1;
         goto cleanup;
      }
   }

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
   if (o65fp)
      fclose(o65fp);

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
