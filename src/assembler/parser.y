%code requires {
#include "addr_mode.h"
#include "expr.h"

typedef struct operand_info {
   addr_mode_t mode;
   expr_t *expr;
} operand_info_t;
}

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "addr_mode.h"
#include "expr.h"

int yylex(void);
void yyerror(const char *s);

typedef struct opcode_rule {
   const char *name;
   unsigned modes;
} opcode_rule_t;

#define MODEBIT(x)   (1u << (x))

static void free_if(char *s)
{
   if (s)
      free(s);
}

static const char *addr_mode_name(addr_mode_t mode)
{
   switch (mode) {
      case AM_NONE: return "none";
      case AM_IMPLIED: return "implied";
      case AM_ACCUMULATOR: return "accumulator";
      case AM_IMMEDIATE: return "immediate";
      case AM_ZP_OR_ABS: return "zp/abs";
      case AM_ZPX_OR_ABSX: return "zp,x/abs,x";
      case AM_ZPY_OR_ABSY: return "zp,y/abs,y";
      case AM_INDIRECT: return "indirect";
      case AM_INDEXED_INDIRECT: return "(zp,x)";
      case AM_INDIRECT_INDEXED: return "(zp),y";
      case AM_RELATIVE: return "relative";
   }

   return "unknown";
}

static addr_mode_t normalize_mode(const char *opcode, addr_mode_t mode)
{
   if (!opcode)
      return mode;

   if (!strcmp(opcode, "BCC") ||
       !strcmp(opcode, "BCS") ||
       !strcmp(opcode, "BEQ") ||
       !strcmp(opcode, "BMI") ||
       !strcmp(opcode, "BNE") ||
       !strcmp(opcode, "BPL") ||
       !strcmp(opcode, "BVC") ||
       !strcmp(opcode, "BVS")) {
      if (mode == AM_ZP_OR_ABS)
         return AM_RELATIVE;
   }

   return mode;
}

static const opcode_rule_t opcode_rules[] = {
   { "ADC", MODEBIT(AM_IMMEDIATE) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) | MODEBIT(AM_ZPY_OR_ABSY) | MODEBIT(AM_INDEXED_INDIRECT) | MODEBIT(AM_INDIRECT_INDEXED) },
   { "AND", MODEBIT(AM_IMMEDIATE) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) | MODEBIT(AM_ZPY_OR_ABSY) | MODEBIT(AM_INDEXED_INDIRECT) | MODEBIT(AM_INDIRECT_INDEXED) },
   { "ASL", MODEBIT(AM_ACCUMULATOR) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) },
   { "BCC", MODEBIT(AM_RELATIVE) },
   { "BCS", MODEBIT(AM_RELATIVE) },
   { "BEQ", MODEBIT(AM_RELATIVE) },
   { "BIT", MODEBIT(AM_ZP_OR_ABS) },
   { "BMI", MODEBIT(AM_RELATIVE) },
   { "BNE", MODEBIT(AM_RELATIVE) },
   { "BPL", MODEBIT(AM_RELATIVE) },
   { "BRK", MODEBIT(AM_IMPLIED) },
   { "BVC", MODEBIT(AM_RELATIVE) },
   { "BVS", MODEBIT(AM_RELATIVE) },
   { "CLC", MODEBIT(AM_IMPLIED) },
   { "CLD", MODEBIT(AM_IMPLIED) },
   { "CLI", MODEBIT(AM_IMPLIED) },
   { "CLV", MODEBIT(AM_IMPLIED) },
   { "CMP", MODEBIT(AM_IMMEDIATE) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) | MODEBIT(AM_ZPY_OR_ABSY) | MODEBIT(AM_INDEXED_INDIRECT) | MODEBIT(AM_INDIRECT_INDEXED) },
   { "CPX", MODEBIT(AM_IMMEDIATE) | MODEBIT(AM_ZP_OR_ABS) },
   { "CPY", MODEBIT(AM_IMMEDIATE) | MODEBIT(AM_ZP_OR_ABS) },
   { "DEC", MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) },
   { "DEX", MODEBIT(AM_IMPLIED) },
   { "DEY", MODEBIT(AM_IMPLIED) },
   { "EOR", MODEBIT(AM_IMMEDIATE) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) | MODEBIT(AM_ZPY_OR_ABSY) | MODEBIT(AM_INDEXED_INDIRECT) | MODEBIT(AM_INDIRECT_INDEXED) },
   { "INC", MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) },
   { "INX", MODEBIT(AM_IMPLIED) },
   { "INY", MODEBIT(AM_IMPLIED) },
   { "JMP", MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_INDIRECT) },
   { "JSR", MODEBIT(AM_ZP_OR_ABS) },
   { "LDA", MODEBIT(AM_IMMEDIATE) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) | MODEBIT(AM_ZPY_OR_ABSY) | MODEBIT(AM_INDEXED_INDIRECT) | MODEBIT(AM_INDIRECT_INDEXED) },
   { "LDX", MODEBIT(AM_IMMEDIATE) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPY_OR_ABSY) },
   { "LDY", MODEBIT(AM_IMMEDIATE) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) },
   { "LSR", MODEBIT(AM_ACCUMULATOR) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) },
   { "NOP", MODEBIT(AM_IMPLIED) },
   { "ORA", MODEBIT(AM_IMMEDIATE) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) | MODEBIT(AM_ZPY_OR_ABSY) | MODEBIT(AM_INDEXED_INDIRECT) | MODEBIT(AM_INDIRECT_INDEXED) },
   { "PHA", MODEBIT(AM_IMPLIED) },
   { "PHP", MODEBIT(AM_IMPLIED) },
   { "PLA", MODEBIT(AM_IMPLIED) },
   { "PLP", MODEBIT(AM_IMPLIED) },
   { "ROL", MODEBIT(AM_ACCUMULATOR) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) },
   { "ROR", MODEBIT(AM_ACCUMULATOR) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) },
   { "RTI", MODEBIT(AM_IMPLIED) },
   { "RTS", MODEBIT(AM_IMPLIED) },
   { "SBC", MODEBIT(AM_IMMEDIATE) | MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) | MODEBIT(AM_ZPY_OR_ABSY) | MODEBIT(AM_INDEXED_INDIRECT) | MODEBIT(AM_INDIRECT_INDEXED) },
   { "SEC", MODEBIT(AM_IMPLIED) },
   { "SED", MODEBIT(AM_IMPLIED) },
   { "SEI", MODEBIT(AM_IMPLIED) },
   { "STA", MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) | MODEBIT(AM_ZPY_OR_ABSY) | MODEBIT(AM_INDEXED_INDIRECT) | MODEBIT(AM_INDIRECT_INDEXED) },
   { "STX", MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPY_OR_ABSY) },
   { "STY", MODEBIT(AM_ZP_OR_ABS) | MODEBIT(AM_ZPX_OR_ABSX) },
   { "TAX", MODEBIT(AM_IMPLIED) },
   { "TAY", MODEBIT(AM_IMPLIED) },
   { "TSX", MODEBIT(AM_IMPLIED) },
   { "TXA", MODEBIT(AM_IMPLIED) },
   { "TXS", MODEBIT(AM_IMPLIED) },
   { "TYA", MODEBIT(AM_IMPLIED) },
   { NULL, 0 }
};

static int opcode_mode_is_legal(const char *opcode, addr_mode_t mode)
{
   const opcode_rule_t *r;
   unsigned mask;

   if (!opcode)
      return 0;

   mask = MODEBIT(mode);

   for (r = opcode_rules; r->name; ++r) {
      if (!strcmp(r->name, opcode))
         return (r->modes & mask) != 0;
   }

   return 0;
}
%}

%locations

%union {
   char *str;
   addr_mode_t mode;
   expr_t *expr;
   operand_info_t operand;
}

%token <str> OPCODE
%token <str> DIRECTIVE
%token <str> IDENT
%token <str> LABEL_DEF
%token <str> NUMBER
%token <str> STRING
%token <str> CHARCONST

%token REG_A REG_X REG_Y
%token EOL

%type <expr> expr add_expr mul_expr unary_expr primary
%type <expr> simple_expr simple_add_expr simple_mul_expr simple_unary simple_primary
%type <operand> operand_expr

%start program

%%

program
   : lines
   ;

lines
   : %empty
   | lines line
   ;

line
   : EOL
   | statement EOL
   ;

statement
   : LABEL_DEF
     {
        free_if($1);
     }
   | LABEL_DEF directive_stmt
     {
        free_if($1);
     }
   | LABEL_DEF instruction_stmt
     {
        free_if($1);
     }
   | directive_stmt
   | instruction_stmt
   ;

directive_stmt
   : DIRECTIVE
     {
        free_if($1);
     }
   | DIRECTIVE expr_list
     {
        free_if($1);
     }
   | DIRECTIVE STRING
     {
        free_if($1);
        free_if($2);
     }
   | DIRECTIVE STRING ',' expr_list
     {
        free_if($1);
        free_if($2);
     }
   ;

expr_list
   : expr
     {
        expr_print($1);
        printf("\n");
        expr_free($1);
     }
   | expr_list ',' expr
     {
        expr_print($3);
        printf("\n");
        expr_free($3);
     }
   ;

instruction_stmt
   : OPCODE
     {
        addr_mode_t mode = AM_IMPLIED;

        if (!opcode_mode_is_legal($1, mode))
           fprintf(stderr,
                   "line %d: illegal addressing mode for %s ... %s\n",
                   @1.first_line, $1, addr_mode_name(mode));
        else
           printf("line %d: %s ... %s\n",
                  @1.first_line, $1, addr_mode_name(mode));

        free_if($1);
     }
   | OPCODE operand_expr
     {
        addr_mode_t mode = normalize_mode($1, $2.mode);

        if (!opcode_mode_is_legal($1, mode)) {
           fprintf(stderr,
                   "line %d: illegal addressing mode for %s ... %s\n",
                   @1.first_line, $1, addr_mode_name(mode));
        } else {
           printf("line %d: %s ... %s",
                  @1.first_line, $1, addr_mode_name(mode));

           if ($2.expr) {
              printf("  expr=");
              expr_print($2.expr);
           }

           printf("\n");
        }

        expr_free($2.expr);
        free_if($1);
     }
   ;

operand_expr
   : REG_A
     {
        $$.mode = AM_ACCUMULATOR;
        $$.expr = NULL;
     }
   | '#' expr
     {
        $$.mode = AM_IMMEDIATE;
        $$.expr = $2;
     }
   | simple_expr
     {
        $$.mode = AM_ZP_OR_ABS;
        $$.expr = $1;
     }
   | simple_expr ',' REG_X
     {
        $$.mode = AM_ZPX_OR_ABSX;
        $$.expr = $1;
     }
   | simple_expr ',' REG_Y
     {
        $$.mode = AM_ZPY_OR_ABSY;
        $$.expr = $1;
     }
   | '(' expr ')'
     {
        $$.mode = AM_INDIRECT;
        $$.expr = $2;
     }
   | '(' expr ',' REG_X ')'
     {
        $$.mode = AM_INDEXED_INDIRECT;
        $$.expr = $2;
     }
   | '(' expr ')' ',' REG_Y
     {
        $$.mode = AM_INDIRECT_INDEXED;
        $$.expr = $2;
     }
   ;

expr
   : add_expr
     {
        $$ = $1;
     }
   ;

add_expr
   : mul_expr
     {
        $$ = $1;
     }
   | add_expr '+' mul_expr
     {
        $$ = expr_make_binary(EXPR_BOP_ADD, $1, $3);
     }
   | add_expr '-' mul_expr
     {
        $$ = expr_make_binary(EXPR_BOP_SUB, $1, $3);
     }
   ;

mul_expr
   : unary_expr
     {
        $$ = $1;
     }
   | mul_expr '*' unary_expr
     {
        $$ = expr_make_binary(EXPR_BOP_MUL, $1, $3);
     }
   | mul_expr '/' unary_expr
     {
        $$ = expr_make_binary(EXPR_BOP_DIV, $1, $3);
     }
   ;

unary_expr
   : primary
     {
        $$ = $1;
     }
   | '-' unary_expr
     {
        $$ = expr_make_unary(EXPR_UOP_NEG, $2);
     }
   | '<' unary_expr
     {
        $$ = expr_make_unary(EXPR_UOP_LO, $2);
     }
   | '>' unary_expr
     {
        $$ = expr_make_unary(EXPR_UOP_HI, $2);
     }
   ;

primary
   : NUMBER
     {
        $$ = expr_make_number(parse_number_token($1));
        free_if($1);
     }
   | IDENT
     {
        $$ = expr_make_ident($1);
        free_if($1);
     }
   | CHARCONST
     {
        $$ = expr_make_char(parse_charconst_token($1));
        free_if($1);
     }
   | '*'
     {
        $$ = expr_make_pc();
     }
   | '(' expr ')'
     {
        $$ = $2;
     }
   ;

simple_expr
   : simple_add_expr
     {
        $$ = $1;
     }
   ;

simple_add_expr
   : simple_mul_expr
     {
        $$ = $1;
     }
   | simple_add_expr '+' mul_expr
     {
        $$ = expr_make_binary(EXPR_BOP_ADD, $1, $3);
     }
   | simple_add_expr '-' mul_expr
     {
        $$ = expr_make_binary(EXPR_BOP_SUB, $1, $3);
     }
   ;

simple_mul_expr
   : simple_unary
     {
        $$ = $1;
     }
   | simple_mul_expr '*' unary_expr
     {
        $$ = expr_make_binary(EXPR_BOP_MUL, $1, $3);
     }
   | simple_mul_expr '/' unary_expr
     {
        $$ = expr_make_binary(EXPR_BOP_DIV, $1, $3);
     }
   ;

simple_unary
   : simple_primary
     {
        $$ = $1;
     }
   | '-' unary_expr
     {
        $$ = expr_make_unary(EXPR_UOP_NEG, $2);
     }
   | '<' unary_expr
     {
        $$ = expr_make_unary(EXPR_UOP_LO, $2);
     }
   | '>' unary_expr
     {
        $$ = expr_make_unary(EXPR_UOP_HI, $2);
     }
   ;

simple_primary
   : NUMBER
     {
        $$ = expr_make_number(parse_number_token($1));
        free_if($1);
     }
   | IDENT
     {
        $$ = expr_make_ident($1);
        free_if($1);
     }
   | CHARCONST
     {
        $$ = expr_make_char(parse_charconst_token($1));
        free_if($1);
     }
   | '*'
     {
        $$ = expr_make_pc();
     }
   ;

%%

void yyerror(const char *s)
{
   fprintf(stderr, "parse error at line %d: %s\n", yylloc.first_line, s);
}
