%option noyywrap nounput noinput yylineno case-insensitive

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.tab.h"

static char *xstrdup(const char *s)
{
   size_t n = strlen(s) + 1;
   char *p = (char *)malloc(n);
   if (!p) {
      fprintf(stderr, "out of memory in lexer\n");
      exit(1);
   }
   memcpy(p, s, n);
   return p;
}

/* Start each scan at beginning-of-line state. */
#define YY_USER_INIT BEGIN(BOL);

%}

/*
   Expected token names in parser.tab.h:

      OPCODE DIRECTIVE IDENT LABEL_DEF NUMBER STRING CHARCONST
      REG_A REG_X REG_Y EOL
*/

%x BOL

WS          [ \t\r]+
HEX         \$[0-9A-Fa-f]+
BIN         %[01]+
DEC         [0-9]+
CHARCONST   \'([^\\\n]|\\[0nrt'\"\\])
STRING      \"([^\\\n]|\\.)*\"
IDSTART     [A-Za-z_@?]
IDCHAR      [A-Za-z0-9_@?]
IDENT       {IDSTART}{IDCHAR}*
DIRECTIVE   [.!][A-Za-z_][A-Za-z0-9_]*

/* Legal NMOS 6502 mnemonics */

OPC_BRANCH   BCC|BCS|BEQ|BMI|BNE|BPL|BVC|BVS
OPC_FLAGS    CLC|CLD|CLI|CLV|SEC|SED|SEI
OPC_LOADST   LDA|LDX|LDY|STA|STX|STY
OPC_ALU      ADC|AND|ASL|BIT|CMP|CPX|CPY|DEC|EOR|INC|LSR|ORA|ROL|ROR|SBC
OPC_MISC     BRK|DEX|DEY|INX|INY|JMP|JSR|NOP|PHA|PHP|PLA|PLP|RTI|RTS|TAX|TAY|TSX|TXA|TXS|TYA

OPCODE6502   {OPC_BRANCH}|{OPC_FLAGS}|{OPC_LOADST}|{OPC_ALU}|{OPC_MISC}

%%

<BOL>{WS}                  { /* keep waiting for first real token */ }
<BOL>";"[^\n]*             { /* whole-line comment */ }
<BOL>\n                    { return EOL; }

<BOL>{IDENT}:{WS}*         {
                              size_t len = yyleng;
                              while (len > 0 &&
                                     (yytext[len-1] == ' ' || yytext[len-1] == '\t' ||
                                      yytext[len-1] == '\r'))
                                 --len;
                              /* strip ':' */
                              --len;
                              yylval.str = (char *)malloc(len + 1);
                              if (!yylval.str) exit(1);
                              memcpy(yylval.str, yytext, len);
                              yylval.str[len] = '\0';
                              BEGIN(INITIAL);
                              return LABEL_DEF;
                           }

<BOL>{OPCODE6502}          { yylval.str = xstrdup(yytext); BEGIN(INITIAL); return OPCODE; }
<BOL>{DIRECTIVE}           { yylval.str = xstrdup(yytext); BEGIN(INITIAL); return DIRECTIVE; }
<BOL>{IDENT}               { yylval.str = xstrdup(yytext); BEGIN(INITIAL); return IDENT; }


";"[^\n]*                  { /* comment to end of line */ }
{WS}                       { /* skip whitespace */ }
\n                         { BEGIN(BOL); return EOL; }

","                        { return ','; }
":"                        { return ':'; }
"#"                        { return '#'; }
"("                        { return '('; }
")"                        { return ')'; }
"["                        { return '['; }
"]"                        { return ']'; }
"+"                        { return '+'; }
"-"                        { return '-'; }
"*"                        { return '*'; }   /* current PC or multiply, parser decides */
"/"                        { return '/'; }
"<"                        { return '<'; }   /* low-byte operator */
">"                        { return '>'; }   /* high-byte operator */
"="                        { return '='; }

{OPCODE6502}               { yylval.str = xstrdup(yytext); return OPCODE; }
{DIRECTIVE}                { yylval.str = xstrdup(yytext); return DIRECTIVE; }

[Aa]                       { return REG_A; }
[Xx]                       { return REG_X; }
[Yy]                       { return REG_Y; }

{HEX}                      { yylval.str = xstrdup(yytext); return NUMBER; }
{BIN}                      { yylval.str = xstrdup(yytext); return NUMBER; }
{DEC}                      { yylval.str = xstrdup(yytext); return NUMBER; }

{CHARCONST}                { yylval.str = xstrdup(yytext); return CHARCONST; }
{STRING}                   { yylval.str = xstrdup(yytext); return STRING; }

{IDENT}                    { yylval.str = xstrdup(yytext); return IDENT; }

.                          {
                              fprintf(stderr,
                                      "lexer error at line %d: unexpected character '%s'\n",
                                      yylineno, yytext);
                           }

<<EOF>>                    { return 0; }

%%
