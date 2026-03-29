%option noyywrap nounput noinput case-insensitive

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "addr_mode.h"
#include "parser.tab.h"

static char g_source_file[4096] = "<input>";
static long g_source_line = 1;

static void parse_file_marker(const char *text)
{
   const char *p;
   char *end;
   long line_no;

   p = text;

   if (strncmp(p, "@@FILE ", 7) != 0)
      return;

   p += 7;
   line_no = strtol(p, &end, 10);
   if (end == p)
      return;

   while (*end == ' ' || *end == '\t')
      end++;

   strncpy(g_source_file, end, sizeof(g_source_file) - 1);
   g_source_file[sizeof(g_source_file) - 1] = '\0';

   {
      size_t n = strlen(g_source_file);
      while (n > 0 &&
             (g_source_file[n - 1] == '\n' || g_source_file[n - 1] == '\r')) {
         g_source_file[n - 1] = '\0';
         n--;
      }
   }

   g_source_line = line_no;
}

#define YY_USER_ACTION                        \
   do {                                       \
      yylloc.first_line = (int)g_source_line; \
      yylloc.last_line = (int)g_source_line;  \
      yylloc.first_column = 1;                \
      yylloc.last_column = 1;                 \
      yylloc.filename = g_source_file;        \
   } while (0);
%}

WS           [ \t\r]+
HEX          \$[0-9A-Fa-f]+
BIN          %[01]+
DEC          [0-9]+
CHARBODY     ([^\\\n']|\\[0nrt'"\\])
CHARCONST    \'{CHARBODY}\'?
STRING       \"([^\\\n]|\\.)*\"
IDSTART      [A-Za-z_@?]
IDCHAR       [A-Za-z0-9_@?$]
IDENT        {IDSTART}{IDCHAR}*
DIRECTIVE    [.!][A-Za-z_][A-Za-z0-9_]*
MODE_SUFFIX  \.(z|zx|zy|a|ax|ay|i|ix|iy)

OPC_BRANCH   BCC|BCS|BEQ|BMI|BNE|BPL|BVC|BVS
OPC_FLAGS    CLC|CLD|CLI|CLV|SEC|SED|SEI
OPC_LOADST   LDA|LDX|LDY|STA|STX|STY
OPC_ALU      ADC|AND|ASL|BIT|CMP|CPX|CPY|DEC|EOR|INC|LSR|ORA|ROL|ROR|SBC
OPC_MISC     BRK|DEX|DEY|INX|INY|JMP|JSR|NOP|PHA|PHP|PLA|PLP|RTI|RTS|TAX|TAY|TSX|TXA|TXS|TYA
OPCODE6502   {OPC_BRANCH}|{OPC_FLAGS}|{OPC_LOADST}|{OPC_ALU}|{OPC_MISC}
OPCODETOK    {OPCODE6502}{MODE_SUFFIX}?

%%

^@@FILE[^\n]*\n             { parse_file_marker(yytext); }

";"[^\n]*                  { }
{WS}                       { }
\n                         { g_source_line++; return EOL; }

{OPCODETOK}                { yylval.str = xstrdup(yytext); return OPCODE; }
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
                                      "%s:%ld: lexer error: unexpected character '%s'\n",
                                      g_source_file, g_source_line, yytext);
                           }

<<EOF>>                    { return 0; }

%%
