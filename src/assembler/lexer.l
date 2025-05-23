%option yylineno
%{
#include "parser.h"
#include <string.h>
#include "opcodes.h"

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

\"([^\"]|\\.)*\" {
    yylval.str = strdup(yytext + 1);
    yylval.str[strlen(yylval.str) - 1] = '\0';
    yycolumn += yyleng;
    return STRING;
}

[0-9]+       { yylval.num = atoi(yytext); yycolumn += yyleng; return NUMBER; }
[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.str = strdup(yytext);
    yycolumn += yyleng;
    if (is_opcode(yylval.str)) { printf("%s opcode\n", yylval.str); return OPCODE; }
    else { printf("%s label\n", yylval.str); return LABEL; }
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
    return 1;
}
