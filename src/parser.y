%{
#include <stdio.h>
#include <stdlib.h>

void yyerror(const char *s);
int yylex(void);
%}

%token IF ELSE WHILE FOR INT IDENTIFIER NUMBER
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

%%

program:
    program_item
  | program program_item
  ;

program_item:
    statement
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
  ;

unmatched_stmt:
    IF '(' expr ')' statement
  | IF '(' expr ')' matched_stmt ELSE unmatched_stmt
  | WHILE '(' expr ')' unmatched_stmt
  | FOR '(' opt_expr ';' opt_expr ';' opt_expr ')' unmatched_stmt
  ;

block:
    '{' statement_list '}'
  ;

statement_list:
    /* empty */
  | statement_list statement
  ;

decl_stmt:
    INT IDENTIFIER ';'
  | INT IDENTIFIER '=' expr ';'
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
  | IDENTIFIER '=' expr
  | '-' expr %prec UMINUS
  | '(' expr ')'
  | IDENTIFIER
  | NUMBER
  ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Error: %s\n", s);
}
