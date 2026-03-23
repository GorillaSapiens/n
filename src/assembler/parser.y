%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int yylex(void);
void yyerror(const char *s);

extern int yylineno;

static void free_if(char *s)
{
   if (s)
      free(s);
}
%}

%union {
   char *str;
}

/* Tokens from the lexer */
%token <str> OPCODE
%token <str> DIRECTIVE
%token <str> IDENT
%token <str> LABEL_DEF
%token <str> NUMBER
%token <str> STRING
%token <str> CHARCONST

%token REG_A REG_X REG_Y
%token EOL

/* precedence for expression parsing */
%left '+' '-'
%left '*' '/'
%right UMINUS
%right '<' '>'

%type <str> expr primary

%start program

%%

program
   : lines
   ;

lines
   : /* empty */
   | lines line
   ;

line
   : EOL
   | statement EOL
   ;

statement
   : label_only
   | directive_stmt
   | instruction_stmt
   | label_and_directive
   | label_and_instruction
   ;

label_only
   : LABEL_DEF
     {
        /* define label here */
        free_if($1);
     }
   ;

label_and_directive
   : LABEL_DEF directive_stmt
     {
        /* define label before processing directive */
        free_if($1);
     }
   ;

label_and_instruction
   : LABEL_DEF instruction_stmt
     {
        /* define label before processing instruction */
        free_if($1);
     }
   ;

directive_stmt
   : DIRECTIVE
     {
        /* directive with no args */
        free_if($1);
     }
   | DIRECTIVE directive_args
     {
        free_if($1);
     }
   ;

directive_args
   : expr_list
   | STRING
     {
        free_if($1);
     }
   | STRING ',' expr_list
     {
        free_if($1);
     }
   ;

expr_list
   : expr
     {
        free_if($1);
     }
   | expr_list ',' expr
     {
        free_if($3);
     }
   ;

instruction_stmt
   : OPCODE
     {
        /* implied addressing */
        free_if($1);
     }
   | OPCODE operand
     {
        /* use opcode + parsed operand kind */
        free_if($1);
     }
   ;

operand
   : REG_A
     {
        /* accumulator addressing */
     }
   | '#' expr
     {
        free_if($2);
     }
   | expr
     {
        free_if($1);
     }
   | expr ',' REG_X
     {
        free_if($1);
     }
   | expr ',' REG_Y
     {
        free_if($1);
     }
   | '(' expr ')'
     {
        free_if($2);
     }
   | '(' expr ',' REG_X ')'
     {
        free_if($2);
     }
   | '(' expr ')' ',' REG_Y
     {
        free_if($2);
     }
   ;

/* -------- Expressions -------- */

expr
   : primary
     {
        $$ = $1;
     }
   | expr '+' expr
     {
        $$ = strdup("<expr>");
        free_if($1);
        free_if($3);
     }
   | expr '-' expr
     {
        $$ = strdup("<expr>");
        free_if($1);
        free_if($3);
     }
   | expr '*' expr
     {
        $$ = strdup("<expr>");
        free_if($1);
        free_if($3);
     }
   | expr '/' expr
     {
        $$ = strdup("<expr>");
        free_if($1);
        free_if($3);
     }
   | '-' expr %prec UMINUS
     {
        $$ = strdup("<expr>");
        free_if($2);
     }
   | '<' expr
     {
        $$ = strdup("<expr>");
        free_if($2);
     }
   | '>' expr
     {
        $$ = strdup("<expr>");
        free_if($2);
     }
   | '(' expr ')'
     {
        $$ = $2;
     }
   ;

primary
   : NUMBER
     {
        $$ = $1;
     }
   | IDENT
     {
        $$ = $1;
     }
   | CHARCONST
     {
        $$ = $1;
     }
   | '*'
     {
        $$ = strdup("*");
     }
   ;

%%

void yyerror(const char *s)
{
   fprintf(stderr, "parse error at line %d: %s\n", yylineno, s);
}
