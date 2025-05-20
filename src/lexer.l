/* ---------- lexer.l ---------- */
%{
#include "parser.tab.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// External type recognition
extern int find_typename(const char*);

int yycolumn = 0;
extern int yylineno;

extern YYLTYPE yylloc;

#define YY_USER_ACTION \
    yylloc.first_line = yylineno; \
    yylloc.first_column = yycolumn; \
    yylloc.last_line = yylineno; \
    yycolumn += yyleng; \
    yylloc.last_column = yycolumn;

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
"return"        return RETURN;

"break"         return BREAK;
"continue"      return CONTINUE;

"struct"        return STRUCT;
"union"         return UNION;

"case"          return CASE;
"switch"        return SWITCH;
"default"       return DEFAULT;

"operator"[-+*/%=!<>|&^:]+ {
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


#[A-Za-z0-9_]+(:[A-Za-z0-9_]+)? {
    yylval.str = strdup(yytext);
    return FLAG;
}

'([^'\\]|\\[abfnrtv\\'\"?]|\\x[0-9a-fA-F]+|\\u[0-9a-fA-F]{4})' {
    const char* yy = yytext;
    int val = 0;

    if (yy[1] != '\\') {
        val = yy[1];
    } else if (yy[2] == 'x') {
        val = (int)strtol(yy + 3, NULL, 16);
    } else if (yy[2] == 'u') {
        char buf[5] = { yy[3], yy[4], yy[5], yy[6], 0 };
        val = (int)strtol(buf, NULL, 16);
    } else {
        switch (yy[2]) {
            case 'n': val = '\n'; break;
            case 'r': val = '\r'; break;
            case 't': val = '\t'; break;
            case 'b': val = '\b'; break;
            case 'f': val = '\f'; break;
            case 'a': val = '\a'; break;
            case 'v': val = '\v'; break;
            case '\\': val = '\\'; break;
            case '\'': val = '\''; break;
            case '\"': val = '\"'; break;
            case '?': val = '\?'; break;
            default: val = yy[2]; break;
        }
    }

    yylval.intval = val;
    return INTEGER;
}

\"([^\"\\]|\\.)*\" {
    yylval.str = strdup(yytext);
    return STRING;
}

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
    yylval.str = strdup(yytext);
    if (find_typename(yytext) != -1) return TYPENAME;
    return IDENTIFIER;
}

[ \t\r]+                  ;   // skip whitespace
[\n]                      { yylineno++; yycolumn = 0; } 
\/\/[^\n]*                ;   // skip line comments
\/\*([^*]|\*+[^*/])*\*+\/ {
    for (char *p = yytext; *p; p++) {
        if (*p == '\n') { yylineno++; yycolumn = 0; }
        else { yycolumn++; }
    }
}

.               return yytext[0]; // return single-character tokens

%%

int yywrap(void) {
    return 1;
}
