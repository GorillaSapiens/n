%{
#include <stdio.h>
#include <stdlib.h>

void yyerror(const char *s);
int yylex(void);
%}

%token IF ELSE WHILE FOR RETURN INT TYPE IDENTIFIER NUMBER
%token ASSIGN
%token EQ NE LE GE LSHIFT RSHIFT OR AND

%left OR
%left AND
%left '|'
%left '^'
%left '&'
%left EQ NE
%left '<' '>' LE GE
%left LSHIFT RSHIFT
%left '+' '-'
%left '*' '/' '%'
%right UMINUS
%right ASSIGN

%%

program:
    program_item
  | program program_item
  ;

program_item:
    type_decl
  | function_decl
  | top_level_stmt
  ;

type_decl:
    TYPE IDENTIFIER '(' NUMBER ')' ';'
  ;

function_decl:
    INT IDENTIFIER '(' param_list ')' block
  ;

param_list:
    /* empty */
  | param_decls
  ;

param_decls:
    INT IDENTIFIER
  | param_decls ',' INT IDENTIFIER
  ;

top_level_stmt:
    block
  | decl_stmt
  | expr_stmt
  ;

block:
    '{' statement_list '}'
  ;

statement_list:
    /* empty */
  | statement_list statement
  ;

statement:
    matched_stmt
  | unmatched_stmt
  ;

matched_stmt:
    IF '(' expr ')' matched_stmt ELSE matched_stmt
  | WHILE '(' expr ')' matched_stmt
  | FOR '(' opt_expr ';' opt_expr ';' opt_expr ')' matched_stmt
  | block
  | decl_stmt
  | expr_stmt
  | RETURN opt_expr ';'
  ;

unmatched_stmt:
    IF '(' expr ')' statement
  | IF '(' expr ')' matched_stmt ELSE unmatched_stmt
  | WHILE '(' expr ')' unmatched_stmt
  | FOR '(' opt_expr ';' opt_expr ';' opt_expr ')' unmatched_stmt
  ;

decl_stmt:
    INT IDENTIFIER ';'
  | INT IDENTIFIER ASSIGN expr ';'
  ;

expr_stmt:
    expr ';'
  | ';'
  ;

opt_expr:
    /* empty */
  | expr
  ;

expr:
    expr OR expr
  | expr AND expr
  | expr '|' expr
  | expr '^' expr
  | expr '&' expr
  | expr EQ expr
  | expr NE expr
  | expr '<' expr
  | expr '>' expr
  | expr LE expr
  | expr GE expr
  | expr LSHIFT expr
  | expr RSHIFT expr
  | expr '+' expr
  | expr '-' expr
  | expr '*' expr
  | expr '/' expr
  | expr '%' expr
  | IDENTIFIER ASSIGN expr
  | '-' expr %prec UMINUS
  | primary_expr
  ;

primary_expr:
    IDENTIFIER
  | NUMBER
  | func_call
  | '(' expr ')'
  ;

func_call:
    IDENTIFIER '(' arg_list ')'
  ;

arg_list:
    /* empty */
  | expr_args
  ;

expr_args:
    expr
  | expr_args ',' expr
  ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Error: %s\n", s);
}
