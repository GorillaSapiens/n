%{
#include "parser.h"
#include <string.h>
%}

%%

".byte"      { return DOTBYTE; }
".word"      { return DOTWORD; }
".org"       { return DOTORG; }
".ascii"     { return DOTASCII; }
".text"      { return DOTTEXT; }

;[^\n]*      { /* skip comment */ }
"\"([^\"]|\\.)*\"" {
    yylval.str = strdup(yytext + 1);
    yylval.str[strlen(yylval.str) - 1] = '\0';  // remove trailing "
    return STRING;
}

[0-9]+       { yylval.num = atoi(yytext); return NUMBER; }
[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.str = strdup(yytext);
    return OPCODE;
}

":"         { return COLON; }
","         { return COMMA; }
"("         { return LPAREN; }
")"         { return RPAREN; }
"#"         { return HASH; }
"\n"        { return NEWLINE; }
[ \t]+      { /* skip whitespace */ }
.           { return yytext[0]; }

%%

int yywrap(void) {
    return 1;
}
