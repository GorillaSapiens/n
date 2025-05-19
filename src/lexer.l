%{
#include "parser.tab.h"
#include <string.h>
#include <stdlib.h>

// External type recognition
extern int is_typename(const char*);
%}

%%

":="            return ASSIGN;
"type"          return TYPE;
"if"            return IF;
"else"          return ELSE;
"while"         return WHILE;
"for"           return FOR;
"return"        return RETURN;

"=="            return EQ;
"!="            return NE;
"<="            return LE;
">="            return GE;
"<<"            return LSHIFT;
">>"            return RSHIFT;
"||"            return OR;
"&&"            return AND;

[0-9]+          { yylval.intval = atoi(yytext); return NUMBER; }

[a-zA-Z_][a-zA-Z0-9_]* {
                    if (is_typename(yytext)) return TYPENAME;
                    yylval.str = strdup(yytext);
                    return IDENTIFIER;
                 }

[ \t\r\n]+      ;   // skip whitespace
.               return yytext[0];  // return single-character tokens

%%

int yywrap(void) {
    return 1;
}
