%option yylineno
%{
#include "parser.h"
#include <string.h>
#include "opcodes.h"
#include <stdio.h>

#define MAX_INCLUDE_DEPTH 16

FILE* file_stack[MAX_INCLUDE_DEPTH];
char* name_stack[MAX_INCLUDE_DEPTH];
int line_stack[MAX_INCLUDE_DEPTH];
int col_stack[MAX_INCLUDE_DEPTH];
int include_depth = 0;

FILE* yyin;
char* current_file = NULL;
int yycolumn = 1;
%}


%%

".byte"      { yycolumn += yyleng; return DOTBYTE; }
".word"      { yycolumn += yyleng; return DOTWORD; }
".org"       { yycolumn += yyleng; return DOTORG; }
".ascii"     { yycolumn += yyleng; return DOTASCII; }
".text"      { yycolumn += yyleng; return DOTTEXT; }
".import"    { yycolumn += yyleng; return DOTIMPORT; }
".export"    { yycolumn += yyleng; return DOTEXPORT; }
".include"   { yycolumn += yyleng; return DOTINCLUDE; }
".proc"      { yycolumn += yyleng; return DOTPROC; }
".endproc"   { yycolumn += yyleng; return DOTENDPROC; }

;[^\n]*      { yycolumn += yyleng; /* skip comment */ }

"\"([^\"]|\\.)*\"" {
    yylval.str = strdup(yytext + 1);
    yylval.str[strlen(yylval.str) - 1] = '\0';
    yycolumn += yyleng;
    return STRING;
}

[0-9]+       { yylval.num = atoi(yytext); yycolumn += yyleng; return NUMBER; }
[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.str = strdup(yytext);
    yycolumn += yyleng;
    if (is_opcode(yylval.str)) return OPCODE;
    else return LABEL;
}

":"         { yycolumn += yyleng; return COLON; }
","         { yycolumn += yyleng; return COMMA; }
"("         { yycolumn += yyleng; return LPAREN; }
")"         { yycolumn += yyleng; return RPAREN; }
"#"         { yycolumn += yyleng; return HASH; }
"\n"        { yycolumn = 1; return NEWLINE; }
[ \t]+      { yycolumn += yyleng; /* skip whitespace */ }
.           { yycolumn += yyleng; return yytext[0]; }

%%

int yywrap(void) {
    if (include_depth == 0) return 1;
    fclose(yyin);
    include_depth--;
    yyin = file_stack[include_depth];
    current_file = name_stack[include_depth];
    yylineno = line_stack[include_depth];
    yycolumn = col_stack[include_depth];
    return 0;
}
