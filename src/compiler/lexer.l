/* ---------- lexer.l ---------- */
%{
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

#include "ast.h"
#include "lextern.h"
#include "md5seen.h"
#include "messages.h"
#include "typename.h"

#include "parser.tab.h"

#define MAX_INCLUDE_DEPTH 32

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
   current_filename = (file_stack_top >= 0) ? file_stack[file_stack_top].filename : "<stdin>";
}
%}

%option noinput nounput

%%
":="            return ASSIGN;
"type"          return TYPE;
"if"            return IF;
"else"          return ELSE;
"while"         return WHILE;
"do"            return DO;
"for"           return FOR;
"goto"          return GOTO;
"return"        return RETURN;

"const"         return CONST;
"static"        return STATIC;

"break"         return BREAK;
"continue"      return CONTINUE;

"struct"        return STRUCT;
"union"         return UNION;

"case"          return CASE;
"switch"        return SWITCH;
"default"       return DEFAULT;

"include"       return INCLUDE;

operator(\{\}|==|!=|<=|>=|<<|>>|\+\+|--|[-+|^&<>\*/.%~]) {
    yylval.str = strdup(yytext);
    return OPERATOR;
}


"=="            return EQ;
"!="            return NE;
"<="            return LE;
">="            return GE;
"<<"            return LSHIFT;
">>"            return RSHIFT;
"||"            return OR;
"&&"            return AND;

"++"            return INC;
"--"            return DEC;

"->"            return ARROW;

"+="            return ADD_ASSIGN;
"-="            return SUB_ASSIGN;
"*="            return MUL_ASSIGN;
"/="            return DIV_ASSIGN;
"%="            return MOD_ASSIGN;
"&="            return AND_ASSIGN;
"|="            return OR_ASSIGN;
"^="            return XOR_ASSIGN;
"<<="           return LSHIFT_ASSIGN;
">>="           return RSHIFT_ASSIGN;


\$[A-Za-z0-9_]+(:[A-Za-z0-9_]+)? {
    yylval.str = strdup(yytext);
    return FLAG;
}

'([^'\\]|\\[abfnrtv\\'\"?]|\\x[0-9a-fA-F]+|\\u[0-9a-fA-F]{4})' {
    yylval.str = strdup(yytext);
    return INTEGER;
}

\"([^\"\\]|\\.)*\" {
    yylval.str = strdup(yytext + 1);
    yylval.str[strlen(yylval.str) - 1] = 0;
    // TODO FIX process backslash escapes somewhere
    return STRING;
}

[0-9_]+\.[0-9_]+([eE][+-]?[0-9_]+)? {
    yylval.str = strdup(yytext);
    return FLOAT;
}

0[xX][0-9a-fA-F_]+[pP][+-]?[0-9_]+ {
    yylval.str = strdup(yytext);
    return FLOAT;
}

[0-9_]+[eE][+-]?[0-9_]+ {
    yylval.str = strdup(yytext);
    return FLOAT;
}

0[xX][0-9a-fA-F_]+ {
    yylval.str = strdup(yytext);
    return INTEGER;
}

0[bB][01_]+ {
    yylval.str = strdup(yytext);
    return INTEGER;
}

0[0-7_]+ {
    yylval.str = strdup(yytext);
    return INTEGER;
}

[0-9_]+ {
    yylval.str = strdup(yytext);
    return INTEGER;
}

[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.str = strdup(yytext);
    if (find_typename(yytext) != -1) return TYPENAME;
    return IDENTIFIER;
}

[ \t\r]+                  ;   // skip whitespace
[\n]                      { yylineno++; yycolumn = 0;
    } 
\/\/[^\n]*                ;   // skip line comments
\/\*([^*]|\*+[^*/])*\*+\/ {
    for (char *p = yytext; *p; p++) {
        if (*p == '\n') { yylineno++; yycolumn = 0;
    }
        else { yycolumn++; }
    }
}

^#[^\n]*        ; // ignore preprocessor noise (for now.  bwahahahaha!)


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
