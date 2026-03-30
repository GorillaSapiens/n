#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
   char **items;
   size_t count;
   size_t cap;
} strvec_t;

typedef enum {
   INPUT_N,
   INPUT_ASM,
   INPUT_OBJ,
   INPUT_ARC
} input_kind_t;

typedef struct {
   char *path;
   input_kind_t kind;
} input_t;

typedef struct {
   input_t *items;
   size_t count;
   size_t cap;
} inputvec_t;

typedef struct {
   bool compile_only;
   bool asm_only;
   bool verbose;
   bool dry_run;
   bool nostdlib;
   const char *output;
   const char *link_script;
   const char *map_path;
   strvec_t include_dirs;
   strvec_t lib_dirs;
   strvec_t libs;
   strvec_t cc_extra;
   strvec_t as_extra;
   strvec_t ld_extra;
   inputvec_t inputs;
} driver_options_t;

typedef struct {
   char path[PATH_MAX];
   bool keep;
} temp_path_t;

typedef struct {
   temp_path_t *items;
   size_t count;
   size_t cap;
   char tempdir[PATH_MAX];
   bool made_tempdir;
} temp_store_t;

static const char *arg0;

static void die(const char *fmt, ...)
{
   va_list ap;
   fprintf(stderr, "%s: ", arg0);
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
   fputc('\n', stderr);
   exit(1);
}

static void *xmalloc(size_t n)
{
   void *p = malloc(n ? n : 1);
   if (!p)
      die("out of memory");
   return p;
}

static void *xrealloc(void *p, size_t n)
{
   void *q = realloc(p, n ? n : 1);
   if (!q)
      die("out of memory");
   return q;
}

static char *xstrdup(const char *s)
{
   char *p = strdup(s);
   if (!p)
      die("out of memory");
   return p;
}

static void strvec_push_owned(strvec_t *v, char *s)
{
   if (v->count == v->cap) {
      v->cap = v->cap ? v->cap * 2 : 8;
      v->items = xrealloc(v->items, v->cap * sizeof(v->items[0]));
   }
   v->items[v->count++] = s;
}

static void strvec_push(strvec_t *v, const char *s)
{
   strvec_push_owned(v, xstrdup(s));
}

static void inputvec_push(inputvec_t *v, const char *path, input_kind_t kind)
{
   if (v->count == v->cap) {
      v->cap = v->cap ? v->cap * 2 : 8;
      v->items = xrealloc(v->items, v->cap * sizeof(v->items[0]));
   }
   v->items[v->count].path = xstrdup(path);
   v->items[v->count].kind = kind;
   v->count++;
}

static const char *path_basename(const char *path)
{
   const char *slash = strrchr(path, '/');
   const char *bslash = strrchr(path, '\\');
   const char *base = path;

   if (slash && slash + 1 > base)
      base = slash + 1;
   if (bslash && bslash + 1 > base)
      base = bslash + 1;
   return base;
}

static void path_dirname(const char *path, char *out, size_t out_sz)
{
   const char *base = path_basename(path);
   size_t len;

   if (base == path) {
      snprintf(out, out_sz, ".");
      return;
   }

   len = (size_t)(base - path);
   if (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\'))
      len--;

   if (len >= out_sz)
      die("path too long");

   memcpy(out, path, len);
   out[len] = '\0';
}

static const char *path_extension(const char *path)
{
   const char *base = path_basename(path);
   const char *dot = strrchr(base, '.');
   return dot ? dot : "";
}

static void path_stem(const char *path, char *out, size_t out_sz)
{
   const char *base = path_basename(path);
   const char *dot = strrchr(base, '.');
   size_t len = dot ? (size_t)(dot - base) : strlen(base);

   if (len + 1 > out_sz)
      die("filename stem too long for buffer");

   memcpy(out, base, len);
   out[len] = '\0';
}

static void make_suffixed_path(const char *path, const char *suffix, char *out, size_t out_sz)
{
   char dir[PATH_MAX];
   char stem[PATH_MAX];
   path_dirname(path, dir, sizeof(dir));
   path_stem(path, stem, sizeof(stem));
   if (strcmp(dir, ".") == 0)
      snprintf(out, out_sz, "%s%s", stem, suffix);
   else
      snprintf(out, out_sz, "%s/%s%s", dir, stem, suffix);
}

static bool ends_with(const char *s, const char *suffix)
{
   size_t slen = strlen(s);
   size_t tlen = strlen(suffix);
   if (slen < tlen)
      return false;
   return strcmp(s + slen - tlen, suffix) == 0;
}

static input_kind_t classify_input(const char *path)
{
   if (ends_with(path, ".n"))
      return INPUT_N;
   if (ends_with(path, ".s") || ends_with(path, ".asm"))
      return INPUT_ASM;
   if (ends_with(path, ".o65"))
      return INPUT_OBJ;
   if (ends_with(path, ".a65"))
      return INPUT_ARC;
   die("do not know how to handle input '%s'", path);
   return INPUT_N;
}

static void usage(FILE *fp)
{
   fprintf(fp,
      "Usage: %s [options] file...\n"
      "\n"
      "A small GCC-like driver for the n65 toolchain.\n"
      "It invokes n65cc, n65asm, and n65ld as needed.\n"
      "\n"
      "Overall options:\n"
      "  -c                   Compile/assemble, but do not link\n"
      "  -S                   Compile only; stop after assembly output\n"
      "  -o FILE              Write final output to FILE\n"
      "  -I DIR               Add DIR to compiler/assembler include search path\n"
      "  -L DIR               Add DIR to archive search path for -l\n"
      "  -lNAME               Link archive NAME (tries libNAME.a65 then NAME.a65)\n"
      "  -nostdlib            Do not link libraries/nlib/nlib.a65 automatically\n"
      "  -T FILE              Pass FILE to n65ld as the linker script/config\n"
      "  -Map FILE            Write linker map to FILE\n"
      "  -v                   Print subordinate commands before running them\n"
      "  -###                 Print subordinate commands but do not run them\n"
      "  -Wc,ARG,...          Pass comma-split args to n65cc\n"
      "  -Wa,ARG,...          Pass comma-split args to n65asm\n"
      "  -Wl,ARG,...          Pass comma-split args to n65ld\n"
      "  -Xcompiler ARG       Pass one extra arg to n65cc\n"
      "  -Xassembler ARG      Pass one extra arg to n65asm\n"
      "  -Xlinker ARG         Pass one extra arg to n65ld\n"
      "  -print-prog-name=TOOL  Print path to cc1/as/ld/ar/sim and exit\n"
      "  -h, --help           Show this help\n"
      "\n"
      "Notes:\n"
      "  * default linked output is a.hex\n"
      "  * -S accepts only .n inputs\n"
      "  * with -c or -S, using -o requires exactly one source input\n"
      "  * default linking adds libraries/nlib/nlib.a65 unless -nostdlib is used\n",
      arg0);
}

static void append_split_commas(strvec_t *v, const char *spec)
{
   char *copy = xstrdup(spec);
   char *p = copy;
   while (*p) {
      char *comma = strchr(p, ',');
      if (comma)
         *comma = '\0';
      if (*p)
         strvec_push(v, p);
      if (!comma)
         break;
      p = comma + 1;
   }
   free(copy);
}

static void get_self_path(char *out, size_t out_sz, const char *argv0)
{
   ssize_t n;
   if (strchr(argv0, '/')) {
      if (argv0[0] == '/') {
         snprintf(out, out_sz, "%s", argv0);
         return;
      }
      if (!getcwd(out, out_sz))
         die("getcwd failed: %s", strerror(errno));
      if (strlen(out) + 1 + strlen(argv0) + 1 > out_sz)
         die("path too long");
      strcat(out, "/");
      strcat(out, argv0);
      return;
   }
   n = readlink("/proc/self/exe", out, out_sz - 1);
   if (n >= 0) {
      out[n] = '\0';
      return;
   }
   snprintf(out, out_sz, "%s", argv0);
}

static void build_tool_path(char *out, size_t out_sz, const char *self_path, const char *subdir, const char *tool)
{
   char self_dir[PATH_MAX];
   char repo_dir[PATH_MAX];
   path_dirname(self_path, self_dir, sizeof(self_dir));
   path_dirname(self_dir, repo_dir, sizeof(repo_dir));
   snprintf(out, out_sz, "%s/%s/%s", repo_dir, subdir, tool);
}

static void temp_store_init(temp_store_t *ts)
{
   memset(ts, 0, sizeof(*ts));
}

static void temp_store_make_dir(temp_store_t *ts)
{
   if (ts->made_tempdir)
      return;
   snprintf(ts->tempdir, sizeof(ts->tempdir), "/tmp/n65driver.XXXXXX");
   if (!mkdtemp(ts->tempdir))
      die("mkdtemp failed: %s", strerror(errno));
   ts->made_tempdir = true;
}

static void temp_store_add(temp_store_t *ts, const char *path, bool keep)
{
   if (ts->count == ts->cap) {
      ts->cap = ts->cap ? ts->cap * 2 : 8;
      ts->items = xrealloc(ts->items, ts->cap * sizeof(ts->items[0]));
   }
   snprintf(ts->items[ts->count].path, sizeof(ts->items[ts->count].path), "%s", path);
   ts->items[ts->count].keep = keep;
   ts->count++;
}

static const char *temp_store_make_file(temp_store_t *ts, const char *stem, const char *suffix)
{
   static unsigned long counter;
   char path[PATH_MAX];
   int fd;

   temp_store_make_dir(ts);
   for (;;) {
      snprintf(path, sizeof(path), "%s/%s.%06lu%s", ts->tempdir, stem, counter++, suffix);
      fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
      if (fd >= 0)
         break;
      if (errno != EEXIST)
         die("temporary file create failed for %s: %s", path, strerror(errno));
   }
   close(fd);
   temp_store_add(ts, path, false);
   return ts->items[ts->count - 1].path;
}

static void temp_store_cleanup(temp_store_t *ts)
{
   size_t i;
   for (i = ts->count; i > 0; --i) {
      if (!ts->items[i - 1].keep)
         unlink(ts->items[i - 1].path);
   }
   if (ts->made_tempdir)
      rmdir(ts->tempdir);
}

static void print_cmd(char *const *argv)
{
   size_t i;
   for (i = 0; argv[i]; ++i) {
      if (i)
         putchar(' ');
      fputs(argv[i], stdout);
   }
   putchar('\n');
}

static int run_argv(char *const *argv, bool verbose, bool dry_run)
{
   pid_t pid;
   int status;

   if (verbose || dry_run)
      print_cmd(argv);
   if (dry_run)
      return 0;

   pid = fork();
   if (pid < 0)
      die("fork failed: %s", strerror(errno));
   if (pid == 0) {
      execv(argv[0], argv);
      fprintf(stderr, "%s: exec failed for %s: %s\n", arg0, argv[0], strerror(errno));
      _exit(127);
   }
   if (waitpid(pid, &status, 0) < 0)
      die("waitpid failed: %s", strerror(errno));

   if (WIFEXITED(status))
      return WEXITSTATUS(status);
   if (WIFSIGNALED(status))
      return 128 + WTERMSIG(status);
   return 1;
}

static void argv_from_vec(strvec_t *src, char ***outv)
{
   size_t i;
   char **argv = xmalloc((src->count + 1) * sizeof(argv[0]));
   for (i = 0; i < src->count; ++i)
      argv[i] = src->items[i];
   argv[src->count] = NULL;
   *outv = argv;
}

static void run_vec_or_die(strvec_t *cmd, bool verbose, bool dry_run)
{
   char **argv;
   int rc;
   argv_from_vec(cmd, &argv);
   rc = run_argv(argv, verbose, dry_run);
   free(argv);
   if (rc != 0)
      exit(rc ? rc : 1);
}

static void parse_args(int argc, char **argv, driver_options_t *opt,
   const char *cc_path, const char *as_path, const char *ld_path,
   const char *ar_path, const char *sim_path)
{
   int i;
   memset(opt, 0, sizeof(*opt));

   for (i = 1; i < argc; ++i) {
      const char *arg = argv[i];

      if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
         usage(stdout);
         exit(0);
      }
      if (strcmp(arg, "-c") == 0) {
         opt->compile_only = true;
         continue;
      }
      if (strcmp(arg, "-S") == 0) {
         opt->asm_only = true;
         continue;
      }
      if (strcmp(arg, "-v") == 0) {
         opt->verbose = true;
         continue;
      }
      if (strcmp(arg, "-###") == 0) {
         opt->dry_run = true;
         continue;
      }
      if (strcmp(arg, "-nostdlib") == 0) {
         opt->nostdlib = true;
         continue;
      }
      if (strcmp(arg, "-pipe") == 0) {
         continue;
      }
      if (strcmp(arg, "-o") == 0) {
         if (++i >= argc)
            die("missing argument for -o");
         opt->output = argv[i];
         continue;
      }
      if (strncmp(arg, "-o", 2) == 0 && arg[2] != '\0') {
         opt->output = arg + 2;
         continue;
      }
      if (strcmp(arg, "-I") == 0) {
         if (++i >= argc)
            die("missing argument for -I");
         strvec_push(&opt->include_dirs, argv[i]);
         continue;
      }
      if (strncmp(arg, "-I", 2) == 0 && arg[2] != '\0') {
         strvec_push(&opt->include_dirs, arg + 2);
         continue;
      }
      if (strcmp(arg, "-L") == 0) {
         if (++i >= argc)
            die("missing argument for -L");
         strvec_push(&opt->lib_dirs, argv[i]);
         continue;
      }
      if (strncmp(arg, "-L", 2) == 0 && arg[2] != '\0') {
         strvec_push(&opt->lib_dirs, arg + 2);
         continue;
      }
      if (strncmp(arg, "-l", 2) == 0 && arg[2] != '\0') {
         strvec_push(&opt->libs, arg + 2);
         continue;
      }
      if (strcmp(arg, "-T") == 0) {
         if (++i >= argc)
            die("missing argument for -T");
         opt->link_script = argv[i];
         continue;
      }
      if (strncmp(arg, "-T", 2) == 0 && arg[2] != '\0') {
         opt->link_script = arg + 2;
         continue;
      }
      if (strcmp(arg, "-Map") == 0) {
         if (++i >= argc)
            die("missing argument for -Map");
         opt->map_path = argv[i];
         continue;
      }
      if (strncmp(arg, "-Map=", 5) == 0) {
         opt->map_path = arg + 5;
         continue;
      }
      if (strncmp(arg, "-Wc,", 4) == 0) {
         append_split_commas(&opt->cc_extra, arg + 4);
         continue;
      }
      if (strncmp(arg, "-Wa,", 4) == 0) {
         append_split_commas(&opt->as_extra, arg + 4);
         continue;
      }
      if (strncmp(arg, "-Wl,", 4) == 0) {
         append_split_commas(&opt->ld_extra, arg + 4);
         continue;
      }
      if (strcmp(arg, "-Xcompiler") == 0) {
         if (++i >= argc)
            die("missing argument for -Xcompiler");
         strvec_push(&opt->cc_extra, argv[i]);
         continue;
      }
      if (strcmp(arg, "-Xassembler") == 0) {
         if (++i >= argc)
            die("missing argument for -Xassembler");
         strvec_push(&opt->as_extra, argv[i]);
         continue;
      }
      if (strcmp(arg, "-Xlinker") == 0) {
         if (++i >= argc)
            die("missing argument for -Xlinker");
         strvec_push(&opt->ld_extra, argv[i]);
         continue;
      }
      if (strncmp(arg, "-print-prog-name=", 17) == 0) {
         const char *name = arg + 17;
         if (strcmp(name, "cc1") == 0)
            puts(cc_path);
         else if (strcmp(name, "as") == 0)
            puts(as_path);
         else if (strcmp(name, "ld") == 0)
            puts(ld_path);
         else if (strcmp(name, "ar") == 0)
            puts(ar_path);
         else if (strcmp(name, "sim") == 0)
            puts(sim_path);
         else
            die("unknown -print-prog-name target '%s'", name);
         exit(0);
      }
      if (arg[0] == '-' && arg[1] != '\0')
         die("unsupported option '%s'", arg);

      inputvec_push(&opt->inputs, arg, classify_input(arg));
   }

   if (opt->compile_only && opt->asm_only)
      die("cannot combine -c and -S");
   if (opt->inputs.count == 0)
      die("no input files");
   if ((opt->compile_only || opt->asm_only) && opt->output && opt->inputs.count != 1)
      die("-o with -c or -S requires exactly one input file");
}

static void add_include_flags(strvec_t *cmd, const strvec_t *dirs)
{
   size_t i;
   for (i = 0; i < dirs->count; ++i) {
      strvec_push(cmd, "-I");
      strvec_push(cmd, dirs->items[i]);
   }
}

static const char *derive_output_path(const input_t *in, const char *suffix, const char *override, char *buf, size_t buf_sz)
{
   if (override)
      return override;
   make_suffixed_path(in->path, suffix, buf, buf_sz);
   return buf;
}

static void run_cc(const char *cc_path, const driver_options_t *opt, const char *input, const char *output)
{
   strvec_t cmd = {0};
   const char *dot = path_extension(input);

   strvec_push(&cmd, cc_path);
   strvec_push(&cmd, "-quiet");
   add_include_flags(&cmd, &opt->include_dirs);
   strvec_push(&cmd, input);
   strvec_push(&cmd, "-o");
   strvec_push(&cmd, output);
   strvec_push(&cmd, "-dumpbase");
   strvec_push(&cmd, path_basename(input));
   strvec_push(&cmd, "-dumpbase-ext");
   strvec_push(&cmd, *dot ? dot : ".n");
   strvec_push(&cmd, "-dumpdir");
   strvec_push(&cmd, "./");
   for (size_t i = 0; i < opt->cc_extra.count; ++i)
      strvec_push(&cmd, opt->cc_extra.items[i]);

   run_vec_or_die(&cmd, opt->verbose, opt->dry_run);
}

static void run_as(const char *as_path, const driver_options_t *opt, const char *nlib_inc, const char *input, const char *output)
{
   strvec_t cmd = {0};
   strvec_push(&cmd, as_path);
   strvec_push(&cmd, "-I");
   strvec_push(&cmd, nlib_inc);
   add_include_flags(&cmd, &opt->include_dirs);
   strvec_push(&cmd, "-o");
   strvec_push(&cmd, output);
   for (size_t i = 0; i < opt->as_extra.count; ++i)
      strvec_push(&cmd, opt->as_extra.items[i]);
   strvec_push(&cmd, input);
   run_vec_or_die(&cmd, opt->verbose, opt->dry_run);
}

static const char *find_library(const driver_options_t *opt, const char *name, char *buf, size_t buf_sz)
{
   size_t i;
   for (i = 0; i < opt->lib_dirs.count; ++i) {
      snprintf(buf, buf_sz, "%s/lib%s.a65", opt->lib_dirs.items[i], name);
      if (access(buf, R_OK) == 0)
         return buf;
      snprintf(buf, buf_sz, "%s/%s.a65", opt->lib_dirs.items[i], name);
      if (access(buf, R_OK) == 0)
         return buf;
   }
   die("could not find library for -l%s", name);
   return NULL;
}

static void run_ld(const char *ld_path, const driver_options_t *opt, const strvec_t *link_inputs, const char *default_nlib)
{
   strvec_t cmd = {0};
   size_t i;

   strvec_push(&cmd, ld_path);
   strvec_push(&cmd, "-o");
   strvec_push(&cmd, opt->output ? opt->output : "a.hex");
   if (opt->link_script) {
      strvec_push(&cmd, "-T");
      strvec_push(&cmd, opt->link_script);
   }
   if (opt->map_path) {
      strvec_push(&cmd, "-Map");
      strvec_push(&cmd, opt->map_path);
   }
   for (i = 0; i < opt->ld_extra.count; ++i)
      strvec_push(&cmd, opt->ld_extra.items[i]);
   for (i = 0; i < link_inputs->count; ++i)
      strvec_push(&cmd, link_inputs->items[i]);
   for (i = 0; i < opt->libs.count; ++i) {
      char libbuf[PATH_MAX];
      strvec_push(&cmd, find_library(opt, opt->libs.items[i], libbuf, sizeof(libbuf)));
   }
   if (!opt->nostdlib)
      strvec_push(&cmd, default_nlib);
   run_vec_or_die(&cmd, opt->verbose, opt->dry_run);
}

int main(int argc, char **argv)
{
   driver_options_t opt;
   temp_store_t temps;
   char self_path[PATH_MAX];
   char cc_path[PATH_MAX];
   char as_path[PATH_MAX];
   char ld_path[PATH_MAX];
   char ar_path[PATH_MAX];
   char sim_path[PATH_MAX];
   char nlib_path[PATH_MAX];
   char nlib_inc[PATH_MAX];
   strvec_t link_inputs = {0};
   size_t i;

   arg0 = argv[0];
   temp_store_init(&temps);
   get_self_path(self_path, sizeof(self_path), argv[0]);
   build_tool_path(cc_path, sizeof(cc_path), self_path, "compiler", "n65cc");
   build_tool_path(as_path, sizeof(as_path), self_path, "assembler", "n65asm");
   build_tool_path(ld_path, sizeof(ld_path), self_path, "linker", "n65ld");
   build_tool_path(ar_path, sizeof(ar_path), self_path, "archiver", "n65ar");
   build_tool_path(sim_path, sizeof(sim_path), self_path, "simulator", "n65sim");
   build_tool_path(nlib_path, sizeof(nlib_path), self_path, "libraries/nlib", "nlib.a65");
   build_tool_path(nlib_inc, sizeof(nlib_inc), self_path, "libraries/nlib", "nlib.inc");
   path_dirname(nlib_inc, nlib_inc, sizeof(nlib_inc));

   parse_args(argc, argv, &opt, cc_path, as_path, ld_path, ar_path, sim_path);

   for (i = 0; i < opt.inputs.count; ++i) {
      const input_t *in = &opt.inputs.items[i];
      char derived[PATH_MAX];
      char stem[PATH_MAX];
      const char *asm_path;
      const char *obj_path;

      if (opt.asm_only) {
         if (in->kind != INPUT_N)
            die("-S only accepts .n inputs, got '%s'", in->path);
         asm_path = derive_output_path(in, ".s", opt.output, derived, sizeof(derived));
         run_cc(cc_path, &opt, in->path, asm_path);
         continue;
      }

      if (opt.compile_only) {
         if (in->kind == INPUT_N) {
            if (opt.output)
               obj_path = opt.output;
            else {
               make_suffixed_path(in->path, ".o65", derived, sizeof(derived));
               obj_path = derived;
            }
            path_stem(in->path, stem, sizeof(stem));
            asm_path = temp_store_make_file(&temps, stem, ".s");
            run_cc(cc_path, &opt, in->path, asm_path);
            run_as(as_path, &opt, nlib_inc, asm_path, obj_path);
            continue;
         }
         if (in->kind == INPUT_ASM) {
            obj_path = derive_output_path(in, ".o65", opt.output, derived, sizeof(derived));
            run_as(as_path, &opt, nlib_inc, in->path, obj_path);
            continue;
         }
         die("-c only accepts .n or assembler inputs, got '%s'", in->path);
      }

      switch (in->kind) {
         case INPUT_N:
            path_stem(in->path, stem, sizeof(stem));
            asm_path = temp_store_make_file(&temps, stem, ".s");
            obj_path = temp_store_make_file(&temps, stem, ".o65");
            run_cc(cc_path, &opt, in->path, asm_path);
            run_as(as_path, &opt, nlib_inc, asm_path, obj_path);
            strvec_push(&link_inputs, obj_path);
            break;
         case INPUT_ASM:
            path_stem(in->path, stem, sizeof(stem));
            obj_path = temp_store_make_file(&temps, stem, ".o65");
            run_as(as_path, &opt, nlib_inc, in->path, obj_path);
            strvec_push(&link_inputs, obj_path);
            break;
         case INPUT_OBJ:
         case INPUT_ARC:
            strvec_push(&link_inputs, in->path);
            break;
      }
   }

   if (!opt.asm_only && !opt.compile_only)
      run_ld(ld_path, &opt, &link_inputs, nlib_path);

   temp_store_cleanup(&temps);
   return 0;
}
