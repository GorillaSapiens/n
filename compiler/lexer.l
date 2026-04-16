/* ---------- lexer.l ---------- */
%{
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "enumname.h"
#include "lextern.h"
#include "main.h"
#include "md5seen.h"
#include "memname.h"
#include "messages.h"
#include "typename.h"
#include "xform.h"
#include "xray.h"

#include "parser.tab.h"

static int input(void);

#define MAX_INCLUDE_DEPTH 32
#define MAX_ALIAS_DEPTH 128

char *root_filename = "<stdin>";
char *current_filename = "<stdin>";
int yycolumn = 0;

typedef struct FileContext {
   FILE *file;
   char *filename;
   char *saved_filename;
   int lineno;
   int column;
   int start;
   YY_BUFFER_STATE buffer;
} FileContext;

typedef struct AliasContext {
   int alias_index;
   int local_owner_depth;
   int local_param_index;
   char *virtual_filename;
   char *saved_filename;
   int lineno;
   int column;
   int start;
   YY_BUFFER_STATE buffer;
   const char **local_names;
   char **local_values;
   int local_count;
} AliasContext;

static FileContext file_stack[MAX_INCLUDE_DEPTH];
static int file_stack_top = -1;
static AliasContext alias_stack[MAX_ALIAS_DEPTH];
static int alias_stack_top = -1;

#define YY_USER_ACTION \
   yylloc.first_line = yylineno; \
   yylloc.first_column = yycolumn; \
   yylloc.last_line = yylineno; \
   yycolumn += yyleng; \
   yylloc.last_column = yycolumn;

int push_file(const char *filename) {
   if (file_stack_top + 1 >= MAX_INCLUDE_DEPTH) {
      fprintf(stderr, "include nesting too deep\n");
      return -1;
   }

   filename = search_includes(filename);

   FILE *f = fopen(filename, "r");
   if (!f) {
      error_user("cannot open include file: %s", filename);
      return -1;
   }

   if (md5seen(filename, f)) {
      fclose(f);
      return 0;
   }

   file_stack_top++;
   file_stack[file_stack_top].file = f;
   file_stack[file_stack_top].filename = strdup(filename);
   file_stack[file_stack_top].saved_filename = current_filename;
   file_stack[file_stack_top].lineno = yylineno;
   file_stack[file_stack_top].column = yycolumn;
   file_stack[file_stack_top].start = YY_START;
   file_stack[file_stack_top].buffer = YY_CURRENT_BUFFER;

   YY_BUFFER_STATE new_buffer = yy_create_buffer(f, YY_BUF_SIZE);
   yy_switch_to_buffer(new_buffer);

   BEGIN(0);
   yylineno = 1;
   yycolumn = 0;
   current_filename = file_stack[file_stack_top].filename;
   return 0;
}

void pop_file() {
   if (file_stack_top < 0) return;
   fclose(file_stack[file_stack_top].file);
   free(file_stack[file_stack_top].filename);
   yy_delete_buffer(YY_CURRENT_BUFFER);
   yy_switch_to_buffer(file_stack[file_stack_top].buffer);
   yylineno = file_stack[file_stack_top].lineno;
   yycolumn = file_stack[file_stack_top].column;
   current_filename = file_stack[file_stack_top].saved_filename;
   BEGIN(file_stack[file_stack_top].start);
   file_stack_top--;
}

static char *strip_underscores(const char *p) {
   if (!p) {
      return NULL;
   }

   char *ret = (char *) malloc (strlen(p) + 1);
   char *q = ret;
   
   while (*p) {
      if (*p != '_') {
         *q++ = *p++;
      }
      else {
         p++;
      }
   }
   *q = 0;

   return ret;
}

////////////////////
///// alias handling
////////////////////

typedef struct alias_entry {
   const char *name;
   const char *rhs;
   const char *sourcefile;
   const char **params;
   int param_count;
   int is_function_like;
   int sourceline;
   int sourcecol;
} alias_entry;

static char *alias_build_virtual_filename(const alias_entry *entry) {
   char buf[1024];

   snprintf(buf, sizeof(buf),
            "<alias %s from %s:%d.%d>",
            entry->name ? entry->name : "?",
            entry->sourcefile ? entry->sourcefile : "?",
            entry->sourceline,
            entry->sourcecol);

   return strdup(buf);
}

alias_entry *alias_entries = NULL;
static int alias_count = 0;

static const char *alias_name = NULL;
static const char *alias_rhs = NULL;
static const char *alias_sourcefile = NULL;
static const char **alias_params = NULL;
static int alias_param_count = 0;
static int alias_is_function_like = 0;
static int alias_sourceline;
static int alias_sourcecol;

static void alias_reset(void) {
   if (alias_name) {
      free((char *)alias_name);
      alias_name = NULL;
   }
   if (alias_rhs) {
      free((char *)alias_rhs);
      alias_rhs = NULL;
   }
   if (alias_sourcefile) {
      free((char *)alias_sourcefile);
      alias_sourcefile = NULL;
   }
   if (alias_params) {
      for (int i = 0; i < alias_param_count; i++) {
         free((char *)alias_params[i]);
      }
      free((char **)alias_params);
      alias_params = NULL;
   }
   alias_param_count = 0;
   alias_is_function_like = 0;
}

static void alias_error(const char *msg) {
   error_user(msg);
}

static void alias_set_name(const char *name) {
   if (!strcmp(name, "true") || !strcmp(name, "false")) {
      alias_error("alias 'true' and 'false' are not allowed.");
   }

   if (xform_exists(name)) {
      alias_error("alias name cannot override xform name.");
   }

   if (memname_exists(name)) {
      alias_error("alias name cannot override mem name.");
   }

   if (typename_exists(name)) {
      alias_error("alias name cannot override type name.");
   }

   if (enumname_exists(name)) {
      alias_error("alias name cannot override enum name.");
   }

   alias_name = strdup(name);
   alias_sourcefile = strdup(current_filename);
   alias_sourceline = yylloc.first_line;
   alias_sourcecol = yylloc.first_column;
}

static void alias_mark_function_like(void) {
   alias_is_function_like = 1;
}

static void alias_add_param(const char *name) {
   for (int i = 0; i < alias_param_count; i++) {
      if (!strcmp(alias_params[i], name)) {
         error_user("duplicate alias parameter '%s'", name);
      }
   }

   alias_params = realloc((char **)alias_params, sizeof(const char *) * (alias_param_count + 1));
   alias_params[alias_param_count] = strdup(name);
   alias_param_count++;
}

static void alias_finish(void) {
   alias_entries = realloc(alias_entries, sizeof(alias_entry) * (alias_count + 1));

   alias_entries[alias_count].name = alias_name;
   alias_name = NULL;

   if (!alias_rhs) {
      alias_rhs = strdup("");
   }
   alias_entries[alias_count].rhs = alias_rhs;
   alias_rhs = NULL;

   alias_entries[alias_count].sourcefile = alias_sourcefile;
   alias_sourcefile = NULL;

   alias_entries[alias_count].params = alias_params;
   alias_params = NULL;
   alias_entries[alias_count].param_count = alias_param_count;
   alias_param_count = 0;
   alias_entries[alias_count].is_function_like = alias_is_function_like;
   alias_is_function_like = 0;

   alias_entries[alias_count].sourceline = alias_sourceline;

   alias_entries[alias_count].sourcecol = alias_sourcecol;

   alias_count++;
}

static int alias_find(const char *key) {
   for (int i = 0; i < alias_count; i++) {
      if (!strcmp(alias_entries[i].name, key)) {
         return i;
      }
   }
   return -1;
}

static int alias_find_local(const char *key, int *owner_depth, int *param_index) {
   int masked_owner_depth = -1;

   if (alias_stack_top >= 0 && alias_stack[alias_stack_top].local_owner_depth >= 0) {
      masked_owner_depth = alias_stack[alias_stack_top].local_owner_depth;
   }

   for (int depth = alias_stack_top; depth >= 0; depth--) {
      if (depth == masked_owner_depth) {
         continue;
      }

      for (int i = 0; i < alias_stack[depth].local_count; i++) {
         if (!strcmp(alias_stack[depth].local_names[i], key)) {
            if (owner_depth) {
               *owner_depth = depth;
            }
            if (param_index) {
               *param_index = i;
            }
            return 1;
         }
      }
   }

   return 0;
}

static char *alias_build_local_virtual_filename(int owner_depth, int param_index) {
   char buf[1024];
   const char *param_name = alias_stack[owner_depth].local_names[param_index];
   const char *base = current_filename ? current_filename : "?";

   snprintf(buf, sizeof(buf),
            "<alias-param %s from %s>",
            param_name ? param_name : "?",
            base);

   return strdup(buf);
}

static void alias_push_text(const char *text,
                            int alias_index,
                            int local_owner_depth,
                            int local_param_index,
                            char *virtual_filename,
                            const char **local_names,
                            char **local_values,
                            int local_count) {
   if (alias_stack_top + 1 >= MAX_ALIAS_DEPTH) {
      error_user("alias nesting too deep");
   }

   alias_stack_top++;
   alias_stack[alias_stack_top].alias_index = alias_index;
   alias_stack[alias_stack_top].local_owner_depth = local_owner_depth;
   alias_stack[alias_stack_top].local_param_index = local_param_index;
   alias_stack[alias_stack_top].virtual_filename = virtual_filename;
   alias_stack[alias_stack_top].saved_filename = current_filename;
   alias_stack[alias_stack_top].lineno = yylineno;
   alias_stack[alias_stack_top].column = yycolumn;
   alias_stack[alias_stack_top].start = YY_START;
   alias_stack[alias_stack_top].buffer = YY_CURRENT_BUFFER;
   alias_stack[alias_stack_top].local_names = local_names;
   alias_stack[alias_stack_top].local_values = local_values;
   alias_stack[alias_stack_top].local_count = local_count;

   yy_scan_string(text);

   BEGIN(0);
   current_filename = alias_stack[alias_stack_top].virtual_filename;
   yylineno = 1;
   yycolumn = 0;
}

static void alias_scan_string(int index) {
   if (index < 0 || index >= alias_count) {
      error_unreachable("bad internal alias index (%d).", index);
   }

   for (int i = 0; i <= alias_stack_top; i++) {
      if (alias_stack[i].alias_index == index) {
         error_user("recursive alias expansion for '%s'", alias_entries[index].name);
      }
   }

   alias_push_text(alias_entries[index].rhs,
                   index,
                   -1,
                   -1,
                   alias_build_virtual_filename(&alias_entries[index]),
                   alias_entries[index].params,
                   NULL,
                   0);
}

static void alias_scan_local_binding(int owner_depth, int param_index) {
   if (owner_depth < 0 || owner_depth > alias_stack_top) {
      error_unreachable("bad local alias owner depth (%d).", owner_depth);
   }

   if (param_index < 0 || param_index >= alias_stack[owner_depth].local_count) {
      error_unreachable("bad local alias parameter index (%d).", param_index);
   }

   for (int i = 0; i <= alias_stack_top; i++) {
      if (alias_stack[i].local_owner_depth == owner_depth &&
          alias_stack[i].local_param_index == param_index) {
         error_user("recursive alias parameter expansion for '%s'",
                    alias_stack[owner_depth].local_names[param_index]);
      }
   }

   alias_push_text(alias_stack[owner_depth].local_values[param_index],
                   -1,
                   owner_depth,
                   param_index,
                   alias_build_local_virtual_filename(owner_depth, param_index),
                   NULL,
                   NULL,
                   0);
}

static void alias_append_char(char **buf, size_t *len, size_t *cap, int c) {
   if (*cap == 0) {
      *cap = 64;
      *buf = malloc(*cap);
   }

   if (*len + 2 > *cap) {
      *cap *= 2;
      *buf = realloc(*buf, *cap);
   }

   (*buf)[(*len)++] = (char) c;
   (*buf)[*len] = 0;
}

static int alias_buffer_is_blank(const char *buf) {
   if (!buf) {
      return 1;
   }

   while (*buf) {
      if (!isspace((unsigned char)*buf)) {
         return 0;
      }
      buf++;
   }

   return 1;
}

static void alias_add_invocation_arg(char ***argv, int *argc, char *arg) {
   *argv = realloc(*argv, sizeof(char *) * (*argc + 1));
   (*argv)[*argc] = arg ? arg : strdup("");
   (*argc)++;
}

static int alias_read_char(void) {
   int c = input();

   if (c == '\n') {
      yylineno++;
      yycolumn = 0;
   }
   else if (c != EOF) {
      yycolumn++;
   }

   return c;
}

static void alias_collect_quoted(char quote, char **buf, size_t *len, size_t *cap) {
   for (;;) {
      int c = alias_read_char();

      if (c == EOF) {
         error_user("unterminated quoted text in alias invocation");
      }

      alias_append_char(buf, len, cap, c);

      if (c == '\\') {
         c = alias_read_char();
         if (c == EOF) {
            error_user("unterminated quoted text in alias invocation");
         }
         alias_append_char(buf, len, cap, c);
         continue;
      }

      if (c == quote) {
         return;
      }
   }
}

static char **alias_parse_invocation_args(const alias_entry *entry, int *out_argc) {
   char **argv = NULL;
   int argc = 0;
   char *current = NULL;
   size_t cur_len = 0;
   size_t cur_cap = 0;
   int depth = 0;
   int c;

   c = alias_read_char();
   if (c != '(') {
      error_user("function-like alias '%s' must be invoked as %s(...)",
                 entry->name,
                 entry->name);
   }

   for (;;) {
      c = alias_read_char();
      if (c == EOF) {
         error_user("unterminated invocation of alias '%s'", entry->name);
      }

      if (c == '\'' || c == '"') {
         alias_append_char(&current, &cur_len, &cur_cap, c);
         alias_collect_quoted((char) c, &current, &cur_len, &cur_cap);
         continue;
      }

      if (c == '(') {
         depth++;
         alias_append_char(&current, &cur_len, &cur_cap, c);
         continue;
      }

      if (c == ')') {
         if (depth > 0) {
            depth--;
            alias_append_char(&current, &cur_len, &cur_cap, c);
            continue;
         }

         if (argc == 0 && alias_buffer_is_blank(current)) {
            free(current);
            current = NULL;
         }
         else {
            alias_add_invocation_arg(&argv, &argc, current ? current : strdup(""));
            current = NULL;
         }
         break;
      }

      if (c == ',' && depth == 0) {
         alias_add_invocation_arg(&argv, &argc, current ? current : strdup(""));
         current = NULL;
         cur_len = 0;
         cur_cap = 0;
         continue;
      }

      alias_append_char(&current, &cur_len, &cur_cap, c);
   }

   if (argc != entry->param_count) {
      error_user("alias '%s' expects %d argument%s but got %d",
                 entry->name,
                 entry->param_count,
                 entry->param_count == 1 ? "" : "s",
                 argc);
   }

   *out_argc = argc;
   return argv;
}

static void alias_invoke(int index) {
   char **argv = NULL;
   int argc = 0;

   if (index < 0 || index >= alias_count) {
      error_unreachable("bad internal alias index (%d).", index);
   }

   if (!alias_entries[index].is_function_like) {
      alias_scan_string(index);
      return;
   }

   argv = alias_parse_invocation_args(&alias_entries[index], &argc);

   alias_push_text(alias_entries[index].rhs,
                   index,
                   -1,
                   -1,
                   alias_build_virtual_filename(&alias_entries[index]),
                   alias_entries[index].params,
                   argv,
                   argc);
}

static void pop_alias(void) {
   if (alias_stack_top < 0) {
      return;
   }

   yy_delete_buffer(YY_CURRENT_BUFFER);
   yy_switch_to_buffer(alias_stack[alias_stack_top].buffer);

   current_filename = alias_stack[alias_stack_top].saved_filename;
   yylineno = alias_stack[alias_stack_top].lineno;
   yycolumn = alias_stack[alias_stack_top].column;
   BEGIN(alias_stack[alias_stack_top].start);

   if (alias_stack[alias_stack_top].local_values) {
      for (int i = 0; i < alias_stack[alias_stack_top].local_count; i++) {
         free(alias_stack[alias_stack_top].local_values[i]);
      }
      free(alias_stack[alias_stack_top].local_values);
   }

   free(alias_stack[alias_stack_top].virtual_filename);
   alias_stack_top--;
}

static void alias_append_rhs(const char *rhs) {
   if (alias_rhs) {
      free((char *)alias_rhs);
   }
   alias_rhs = strdup(rhs);
}

%}

%option nounput noyywrap

HEXINT    0[xX][0-9a-fA-F]((_)?[0-9a-fA-F])*
BININT    0[bB][01]((_)?[01])*
OCTINT    0[1-7]((_)?[0-7])*
DECINT    [1-9]((_)?[0-9])*
ZERO      0
INT       {HEXINT}|{BININT}|{OCTINT}|{DECINT}|{ZERO}

DECFLOAT  [0-9]((_)?[0-9])*(\.([0-9]((_)?[0-9])*)?)?([eE][+-]?[0-9]+)?
DOTFLOAT  \.[0-9]((_)?[0-9])*([eE][+-]?[0-9]+)?
HEXFLOAT  0[xX][0-9a-fA-F]((_)?[0-9a-fA-F_])*(\.([0-9a-fA-F]((_)?[0-9a-fA-F])*)?)?[pP][+-]?[0-9_]+
HOTFLOAT  0[xX]\.[0-9a-fA-F]((_)?[0-9a-fA-F])*[pP][+-]?[0-9_]+
FLOAT     {DECFLOAT}|{DOTFLOAT}|{HEXFLOAT}|{HOTFLOAT}

ANY_CHAR  [^'\\]
OLD_ESC   \\[abefnrtv\\'\"?0]
ESC_CHAR  \\[^xu]
HEX_CHAR  \\x[0-9a-fA-F]{2}
UNI_CHAR  \\u[0-9a-fA-F]{4}
CHAR      \'({ANY_CHAR}|{ESC_CHAR}|{HEX_CHAR}|{UNI_CHAR})\'

FLAG      \$[A-Za-z0-9_]+(:[A-Za-z0-9_]+)?

ANY_STR   [^\"\\]
ESC_STR   \\.
STRING    \"({ANY_STR}|{ESC_STR})*\"

IDENT     [a-zA-Z_\xc0-\xff][a-zA-Z0-9_\x80-\xff]*

CBLOCK    \/\*([^*]|\*+[^*/])*\*+\/ 

%x ALIAS_NAME ALIAS_AFTER_NAME ALIAS_PARAMS ALIAS_PARAM_TAIL ALIAS_AFTER_PARAMS ALIAS_RHS

%%

"alias"[[:space:]]+        { alias_reset(); BEGIN(ALIAS_NAME); }

<ALIAS_NAME>[[:space:]]+   ;
<ALIAS_NAME>{IDENT}        { alias_set_name(yytext); BEGIN(ALIAS_AFTER_NAME); }
<ALIAS_NAME>.|\n           { alias_error("expected alias name"); BEGIN(INITIAL); }

<ALIAS_AFTER_NAME>"("     { alias_mark_function_like(); BEGIN(ALIAS_PARAMS); }
<ALIAS_AFTER_NAME>\n      { alias_finish(); BEGIN(INITIAL); }
<ALIAS_AFTER_NAME>[^\n]    { BEGIN(ALIAS_RHS); yyless(0); }

<ALIAS_PARAMS>[ \t\r\f\v]+ ;
<ALIAS_PARAMS>")"         { BEGIN(ALIAS_AFTER_PARAMS); }
<ALIAS_PARAMS>{IDENT}      { alias_add_param(yytext); BEGIN(ALIAS_PARAM_TAIL); }
<ALIAS_PARAMS>.|\n        { alias_error("expected alias parameter name or ')'"); BEGIN(INITIAL); }

<ALIAS_PARAM_TAIL>[ \t\r\f\v]+ ;
<ALIAS_PARAM_TAIL>","     { BEGIN(ALIAS_PARAMS); }
<ALIAS_PARAM_TAIL>")"     { BEGIN(ALIAS_AFTER_PARAMS); }
<ALIAS_PARAM_TAIL>.|\n     { alias_error("expected ',' or ')' in alias parameter list"); BEGIN(INITIAL); }

<ALIAS_AFTER_PARAMS>[ \t\r\f\v]+ ;
<ALIAS_AFTER_PARAMS>\n     { alias_finish(); BEGIN(INITIAL); }
<ALIAS_AFTER_PARAMS>[^\n]  { BEGIN(ALIAS_RHS); yyless(0); }

<ALIAS_RHS>\n              { alias_finish(); BEGIN(INITIAL); }
<ALIAS_RHS>[^\n]+          { alias_append_rhs(yytext); }

"break"         return BREAK;
"case"          return CASE;
"const"         return CONST;
"continue"      return CONTINUE;
"default"       return DEFAULT;
"do"            return DO;
"else"          return ELSE;
"enum"          return ENUM;
"extern"        return EXTERN;
"for"           return FOR;
"goto"          return GOTO;
"if"            return IF;
"include"       return INCLUDE;
"mem"           return MEM;
"ref"           return REF;
"return"        return RETURN;
"sizeof"        return SIZEOF;
"static"        return STATIC;
"struct"        return STRUCT;
"switch"        return SWITCH;
"to"            return TO;
"type"          return TYPE;
"union"         return UNION;
"while"         return WHILE;
"xform"         return XFORM;

operator(\{\}|==|!=|<=|>=|<<|>>|\+\+|--|[-+|^&<>\*/%~]) {
   yylval.str = strdup(yytext);
   return OPERATOR;
}

"+="            return ADD_ASSIGN;
"&&"            return AND;
"&="            return AND_ASSIGN;
"->"            return ARROW;
":="            return ASSIGN;
"--"            return DEC;
"/="            return DIV_ASSIGN;
"=="            return EQ;
">="            return GE;
"++"            return INC;
"<="            return LE;
"<<"            return LSHIFT;
"<<="           return LSHIFT_ASSIGN;
"%="            return MOD_ASSIGN;
"*="            return MUL_ASSIGN;
"!="            return NE;
"||"            return OR;
"|="            return OR_ASSIGN;
">>"            return RSHIFT;
">>="           return RSHIFT_ASSIGN;
"-="            return SUB_ASSIGN;
"^="            return XOR_ASSIGN;
"..."           return ELLIPSIS;

{FLAG} {
   yylval.str = strdup(yytext);
   return FLAG;
}

{CHAR} {
   yylval.str = strdup(yytext);
   return CHAR;
}

{STRING} {
   yylval.str = strdup(yytext + 1);
   yylval.str[strlen(yylval.str) - 1] = 0;
   // we do not process backslash escapes here.
   // that along with character set translation in xform.c
   return STRING;
}

{INT} {
   yylval.str = strip_underscores(yytext);
   return INTEGER;
}

{FLOAT} {
   yylval.str = strip_underscores(yytext);
   return FLOAT;
}

{IDENT} {
   int owner_depth;
   int param_index;
   int i;

   if (alias_find_local(yytext, &owner_depth, &param_index)) {
      alias_scan_local_binding(owner_depth, param_index);
   }
   else {
      i = alias_find(yytext);
      if (i != -1) {
         alias_invoke(i);
      }
      else {
         yylval.str = strdup(yytext);
         if (!strcmp(yytext, "true") || !strcmp(yytext, "false")) return INTEGER;
         if (xform_exists(yytext)) return XFORMNAME;
         if (memname_exists(yytext)) return MEMNAME;
         if (typename_exists(yytext)) return TYPENAME;
         if (enumname_exists(yytext)) return ENUMNAME;
         return IDENTIFIER;
      }
   }
}

{CBLOCK} {   // C style block comments
   for (char *p = yytext; *p; p++) {
      if (*p == '\n') {
         yylineno++;
         yycolumn = 0;
      }
      else {
         yycolumn++;
      }
   }
}

"asm "[^;\n]* {
   yylval.str = strdup(yytext + 4);
   return ASM;
}

\/\/[^\n]*                ;   // skip line comments

[ \t\r]+                  ;   // skip whitespace

[\n]                      { yylineno++; yycolumn = 0; }

.                         return yytext[0]; // return single-character tokens

<<EOF>> {
   if (alias_stack_top >= 0) {
      pop_alias();
   }
   else if (file_stack_top >= 0) {
      pop_file();
   }
   else {
      yyterminate();
   }
}

%%

/*
int yyparse_file(const char *filename) {
fprintf(stderr, "%s:%d\n", __FILE__, __LINE__);
   FILE *f = fopen(filename, "r");
   if (!f) {
      fprintf(stderr, "cannot open file: %s\n", filename);
      return 1;
   }

   YY_BUFFER_STATE buf = yy_create_buffer(f, YY_BUF_SIZE);
   yy_switch_to_buffer(buf);
   yylineno = 1;
   yycolumn = 0;
   current_filename = (char *)filename;

   int result = yyparse();

   yy_delete_buffer(buf);
   fclose(f);
   return result;
}
*/
