/* ---------- parser.y ---------- */
%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int yylex();
void yyerror(const char *s);

// Type table
#define MAXTYPES 100

typedef struct {
    char* name;
    int size;
} TypeEntry;

TypeEntry typetable[MAXTYPES];
int typecount = 0;

void register_typename(const char* id, int size) {
    for (int i = 0; i < typecount; i++) {
        if (strcmp(typetable[i].name, id) == 0) {
            typetable[i].size = size;
            return;
        }
    }
    if (typecount < MAXTYPES) {
        typetable[typecount].name = strdup(id);
        typetable[typecount].size = size;
        typecount++;
    }
}

int is_typename(const char* id) {
    for (int i = 0; i < typecount; i++) {
        if (strcmp(typetable[i].name, id) == 0)
            return 1;
    }
    return 0;
}
%}

%union {
    char* str;
    double dval;
    int   intval;
}

%token <str> IDENTIFIER TYPENAME
%token <intval> INTEGER
%token <dval> FLOAT
%token IF ELSE WHILE FOR RETURN TYPE
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

%type <str> type_name

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
    TYPE IDENTIFIER '(' INTEGER ')' ';' {
        register_typename($2, $4);
    }
  ;

function_decl:
    type_name IDENTIFIER '(' param_list ')' block
  ;

param_list:
    /* empty */
  | param_decls
  ;

param_decls:
    type_name IDENTIFIER
  | param_decls ',' type_name IDENTIFIER
  ;

type_name:
    TYPENAME
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

opt_address:
    /* empty */
  | '@' INTEGER
  ;

decl_stmt:
    type_name IDENTIFIER opt_array_dim opt_address ';'
  | type_name IDENTIFIER opt_array_dim opt_address ASSIGN expr ';'
  | type_name IDENTIFIER opt_array_dim opt_address ASSIGN array_initializer ';'
  ;

opt_array_dim:
    /* empty */
  | '[' expr ']'
  ;

array_initializer:
    '{' expr_list '}'
  ;

expr_list:
    expr
  | expr_list ',' expr
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
  | INTEGER
  | FLOAT
  | func_call
  | IDENTIFIER '[' expr ']'
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
