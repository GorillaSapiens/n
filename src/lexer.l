/* ---------- lexer.l ---------- */
%{
#include "parser.tab.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

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

[0-9_]+\.[0-9_]+([eE][+-]?[0-9_]+)? {
    char* clean = malloc(strlen(yytext) + 1);
    char* dst = clean;
    for (char* src = yytext; *src; ++src) {
        if (*src != '_') *dst++ = *src;
    }
    *dst = '\0';
    yylval.dval = strtod(clean, NULL);
    free(clean);
    return FLOAT;
}

0[xX][0-9a-fA-F_]+[pP][+-]?[0-9_]+ {
    char* clean = malloc(strlen(yytext) + 1);
    char* dst = clean;
    for (char* src = yytext; *src; ++src) {
        if (*src != '_') *dst++ = *src;
    }
    *dst = '\0';
    yylval.dval = strtod(clean, NULL);
    free(clean);
    return FLOAT;
}

[0-9_]+[eE][+-]?[0-9_]+ {
    char* clean = malloc(strlen(yytext) + 1);
    char* dst = clean;
    for (char* src = yytext; *src; ++src) {
        if (*src != '_') *dst++ = *src;
    }
    *dst = '\0';
    yylval.dval = strtod(clean, NULL);
    free(clean);
    return FLOAT;
}

0[xX][0-9a-fA-F_]+ {
    char* raw = yytext + 2;
    char* clean = malloc(strlen(raw) + 1);
    char* dst = clean;
    for (char* src = raw; *src; ++src) {
        if (*src != '_') *dst++ = *src;
    }
    *dst = '\0';
    yylval.intval = (int)strtol(clean, NULL, 16);
    free(clean);
    return INTEGER;
}

0[bB][01_]+ {
    char* clean = strdup(yytext + 2);
    char* p = clean;
    int val = 0;
    while (*p) {
        if (*p != '_') {
            val = (val << 1) | (*p - '0');
        }
        p++;
    }
    free(clean);
    yylval.intval = val;
    return INTEGER;
}

0[0-7_]+ {
    char* clean = strdup(yytext);
    char* dst = clean;
    for (char* src = clean; *src; ++src) {
        if (*src != '_') *dst++ = *src;
    }
    *dst = '\0';
    yylval.intval = (int)strtol(clean, NULL, 8);
    free(clean);
    return INTEGER;
}

[0-9_]+ {
    char* clean = strdup(yytext);
    char* dst = clean;
    for (char* src = yytext; *src; ++src) {
        if (*src != '_') *dst++ = *src;
    }
    *dst = '\0';
    yylval.intval = atoi(clean);
    free(clean);
    return INTEGER;
}

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
