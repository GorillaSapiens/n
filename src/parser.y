%{
#include <stdio.h>
#include <stdlib.h>

void yyerror(const char *s);
int yylex(void);
%}

%token IF ELSE IDENTIFIER NUMBER
%token INT

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
    /* empty */
  | program statement
  ;

statement:
    matched_stmt
  | unmatched_stmt
  ;

matched_stmt:
    IF '(' expr ')' matched_stmt ELSE matched_stmt
  | other_stmt
  | block
  ;

unmatched_stmt:
    IF '(' expr ')' statement
  | IF '(' expr ')' matched_stmt ELSE unmatched_stmt
  ;

block:
    '{' statement_list '}'
  ;

statement_list:
    /* empty */
  | statement_list statement
  ;

other_stmt:
    expr ';'
  | IDENTIFIER '=' expr ';'
  | INT IDENTIFIER ';'
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
  | '-' expr %prec UMINUS
  | '(' expr ')'
  | IDENTIFIER
  | NUMBER
  ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Error: %s\n", s);
}
