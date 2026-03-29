/* ---------- lexer.l ---------- */
%{
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "lextern.h"
#include "main.h"
#include "md5seen.h"
#include "memname.h"
#include "messages.h"
#include "typename.h"
#include "xform.h"
#include "xray.h"

#include "parser.tab.h"

#define MAX_INCLUDE_DEPTH 32

char *root_filename = "<stdin>";
char *current_filename = "<stdin>";
int yycolumn = 0;

typedef struct FileContext {
   FILE *file;
   char *filename;
   int lineno;
   int column;
   YY_BUFFER_STATE buffer;
} FileContext;

static FileContext file_stack[MAX_INCLUDE_DEPTH];
static int file_stack_top = -1;

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
      error("cannot open include file: %s", filename);
      return -1;
   }

   if (md5seen(filename, f)) {
      fclose(f);
      return 0;
   }

   file_stack_top++;
   file_stack[file_stack_top].file = f;
   file_stack[file_stack_top].filename = strdup(filename);
   file_stack[file_stack_top].lineno = yylineno;
   file_stack[file_stack_top].column = yycolumn;
   file_stack[file_stack_top].buffer = YY_CURRENT_BUFFER;

   YY_BUFFER_STATE new_buffer = yy_create_buffer(f, YY_BUF_SIZE);
   yy_switch_to_buffer(new_buffer);

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
   file_stack_top--;
   current_filename = (file_stack_top >= 0) ? file_stack[file_stack_top].filename : root_filename;
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

%}

%option noinput nounput

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

%%

"break"         return BREAK;
"case"          return CASE;
"const"         return CONST;
"continue"      return CONTINUE;
"default"       return DEFAULT;
"do"            return DO;
"else"          return ELSE;
"extern"        return EXTERN;
"for"           return FOR;
"goto"          return GOTO;
"if"            return IF;
"include"       return INCLUDE;
"mem"           return MEM;
"ref"           return REF;
"return"        return RETURN;
"static"        return STATIC;
"struct"        return STRUCT;
"switch"        return SWITCH;
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
   // TODO FIX process backslash escapes somewhere
   // should not be done here, as we'll need to do character set translation
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
   yylval.str = strdup(yytext);
   if (xform_exists(yytext)) return XFORMNAME;
   if (memname_exists(yytext)) return MEMNAME;
   if (typename_exists(yytext)) return TYPENAME;
   return IDENTIFIER;
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

.               return yytext[0]; // return single-character tokens

%%

int yywrap(void) {
   if (file_stack_top >= 0) {
      pop_file();
      return 0;  // tell Flex: more input available
   }
   return 1;  // tell Flex: we're done
}

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
