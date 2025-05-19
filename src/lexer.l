%{
#include "parser.tab.h"  // only if you're using yacc/bison and need token definitions
%}

%%

"int"      return INT;
"if"       return IF;
"else"     return ELSE;
"while"    return WHILE;
"for"      return FOR;
"return"   return RETURN;

"=="       return EQ;
"!="       return NE;
"<="       return LE;
">="       return GE;
"<<"       return LSHIFT;
">>"       return RSHIFT;
"||"       return OR;
"&&"       return AND;

[0-9]+     return NUMBER;
[a-zA-Z_][a-zA-Z0-9_]*  return IDENTIFIER;
[ \t\r\n]+  ; // skip whitespace
.          return yytext[0];


%%

int yywrap() { return 1; }
