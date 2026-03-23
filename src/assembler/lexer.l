%option noyywrap nounput noinput yylineno case-insensitive

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "addr_mode.h"
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
%}

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
OPC_BRANCH  BCC|BCS|BEQ|BMI|BNE|BPL|BVC|BVS
OPC_FLAGS   CLC|CLD|CLI|CLV|SEC|SED|SEI
OPC_LOADST  LDA|LDX|LDY|STA|STX|STY
OPC_ALU     ADC|AND|ASL|BIT|CMP|CPX|CPY|DEC|EOR|INC|LSR|ORA|ROL|ROR|SBC
OPC_MISC    BRK|DEX|DEY|INX|INY|JMP|JSR|NOP|PHA|PHP|PLA|PLP|RTI|RTS|TAX|TAY|TSX|TXA|TXS|TYA
OPCODE6502  {OPC_BRANCH}|{OPC_FLAGS}|{OPC_LOADST}|{OPC_ALU}|{OPC_MISC}

%%

";"[^\n]*                  { }
{WS}                       { }
\n                         { return EOL; }

{OPCODE6502}               { yylval.str = xstrdup(yytext); return OPCODE; }
{DIRECTIVE}                { yylval.str = xstrdup(yytext); return DIRECTIVE; }

","                        { return ','; }
":"                        { return ':'; }
"#"                        { return '#'; }
"("                        { return '('; }
")"                        { return ')'; }
"+"                        { return '+'; }
"-"                        { return '-'; }
"*"                        { return '*'; }
"/"                        { return '/'; }
"<"                        { return '<'; }
">"                        { return '>'; }
"="                        { return '='; }

[Aa]                       { return REG_A; }
[Xx]                       { return REG_X; }
[Yy]                       { return REG_Y; }

{HEX}                      { yylval.str = xstrdup(yytext); return NUMBER; }
{BIN}                      { yylval.str = xstrdup(yytext); return NUMBER; }
{DEC}                      { yylval.str = xstrdup(yytext); return NUMBER; }

{CHARCONST}                { yylval.str = xstrdup(yytext); return CHARCONST; }
{STRING}                   { yylval.str = xstrdup(yytext); return STRING; }

{IDENT}:{WS}*              {
                              size_t len = yyleng;

                              while (len > 0 &&
                                     (yytext[len - 1] == ' ' ||
                                      yytext[len - 1] == '\t' ||
                                      yytext[len - 1] == '\r'))
                                 --len;

                              --len;
                              yylval.str = (char *)malloc(len + 1);
                              if (!yylval.str)
                                 exit(1);

                              memcpy(yylval.str, yytext, len);
                              yylval.str[len] = '\0';
                              return LABEL_DEF;
                           }

{IDENT}                    { yylval.str = xstrdup(yytext); return IDENT; }

.                          {
                              fprintf(stderr,
                                      "lexer error at line %d: unexpected character '%s'\n",
                                      yylineno, yytext);
                           }

<<EOF>>                    { return 0; }

%%
