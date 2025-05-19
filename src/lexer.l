%{
#include "parser.tab.h"  // only if you're using yacc/bison and need token definitions
%}

%%

"int"     return INT;
"if"      return IF;
"else"    return ELSE;
"while"   return WHILE;
"for"     return FOR;
"=="      return EQ;
"!="      return NE;
"<="      return LE;
">="      return GE;
"<<"      return LSHIFT;
">>"      return RSHIFT;
"||"      return OR;
"&&"      return AND;

[a-zA-Z_][a-zA-Z0-9_]* return IDENTIFIER;
[0-9]+   return NUMBER;
[ \t\r\n]+   ;        // skip whitespace
.        return yytext[0];  // catch = + - ; { } etc.

%%

int yywrap() { return 1; }
