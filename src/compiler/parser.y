/* ---------- parser.y ---------- */
%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include "ast.h"
#include "lextern.h"
#include "messages.h"
#include "typename.h"
#include "coverage.h"
#include "xray.h"

%}

%union {
    char    *str;
    ASTNode *node;
}

%token <str> STRING IDENTIFIER TYPENAME FLAG OPERATOR
%token <str> INTEGER
%token <str> FLOAT
%token <str> CONST STATIC EXTERN QUICK REF

%token IF ELSE WHILE FOR RETURN TYPE
%token ASSIGN
%token EQ NE LE GE LSHIFT RSHIFT OR AND
%token INC DEC ARROW
%token STRUCT UNION
%token GOTO SWITCH CASE DEFAULT
%token BREAK
%token CONTINUE
%token DO
%token INCLUDE
%token ADD_ASSIGN SUB_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN
%token AND_ASSIGN OR_ASSIGN XOR_ASSIGN LSHIFT_ASSIGN RSHIFT_ASSIGN

%type <node> additive_expr arg_list array_initializer array_initializer_list
%type <node> lvalue lvalue_base lvalue_suffixes
%type <node> bitwise_and_expr bitwise_or_expr bitwise_xor_expr block
%type <node> case_block case_section
%type <node> defdecl_stmt
%type <node> equality_expr expr expr_args expr_list expr_stmt
%type <node> flag flag_list
%type <node> include_stmt
%type <node> logical_and_expr logical_or_expr
%type <node> multiplicative_expr
%type <node> opt_expr opt_flags
%type <node> modifier_list modifier
%type <node> parameter parameter_list
%type <node> postfix_expr primary_expr program program_item
%type <node> relational_expr
%type <node> shift_expr statement statement_list struct_decl_stmt struct_field
%type <node> struct_fields struct_init struct_inits struct_literal
%type <node> type_decl_stmt
%type <node> unary_expr
%type <node> union_decl_stmt
%type <node> return_stmt goto_stmt break_stmt continue_stmt switch_stmt 
%type <node> if_stmt while_stmt for_stmt do_stmt label_stmt 
%type <node> decl decl_specifiers declarator pointer direct_declarator

%locations
%define parse.error verbose
%define parse.lac full
%expect 0
/* %expect-rr 0 */

%%

program:
    program_item         { COVER; if ($1) { root = $$ = MAKE_NODE($1); } }
  | program program_item { COVER; if ($2) { root = $$ = MAKE_NODE($1, $2); } else { $$ = $1; } }
  ;

program_item:
    include_stmt          { COVER; $$ = NULL; } // process an include line
  | type_decl_stmt        { COVER; $$ = $1; }
  | struct_decl_stmt      { COVER; $$ = $1; }
  | union_decl_stmt       { COVER; $$ = $1; }
  | defdecl_stmt          { COVER; $$ = $1; }
  ;

include_stmt:
    INCLUDE STRING       { COVER; if (push_file($2) != 0) { yyerror("failed to include file: %s", $2); YYABORT; } }
  ;

type_decl_stmt:
    TYPE IDENTIFIER '{' opt_flags '}' ';' { COVER; if (register_typename($2) < 0) YYABORT; $$ = MAKE_NODE(make_identifier_leaf($2), $4); }
  | TYPE '*' '{' opt_flags '}' ';'        { COVER; if (register_typename("*") < 0) YYABORT; $$ = MAKE_NODE(make_identifier_leaf("*"), $4); }
  | TYPE TYPENAME '{' opt_flags '}' ';'   { yyerror("duplicate type '%s'", $2); } // no general cover for error cases
  ;

struct_decl_stmt:
    STRUCT IDENTIFIER '{' { COVER;
        if (register_typename($2) < 0) YYABORT;  // Add early to type table
    }
    struct_fields '}' ';' { COVER;
        register_typename($2);
        $$ = MAKE_NODE(make_identifier_leaf($2), $5);
    }
  | STRUCT TYPENAME '{' { // no general cover for error cases
        yyerror("duplicate struct '%s'", $2);
    }
  ;

union_decl_stmt:
    UNION IDENTIFIER '{' { COVER;
        if (register_typename($2) < 0) YYABORT;  // Add early to type table
    }
    struct_fields '}' ';' { COVER;
        register_typename($2);
        $$ = MAKE_NODE(make_identifier_leaf($2), $5);
    }
  | UNION TYPENAME '{' { // no general cover for error cases
        yyerror("duplicate union '%s'", $2);
    }
  ;

defdecl_stmt:
    decl ';'                            { COVER; $$ = MAKE_NODE($1); }
  | decl block                          { COVER; $$ = MAKE_NODE($1, $2); }
  | decl ASSIGN expr ';'                { COVER; $$ = MAKE_NODE($1, $3); }
  | decl ASSIGN array_initializer ';'   { COVER; $$ = MAKE_NODE($1, $3); }
  ;


struct_fields:
    struct_field                        { COVER; $$ = $1; }
  | struct_fields struct_field          { COVER; $$ = MAKE_NODE($1, $2); }
  ;

struct_field:
    decl ';' { COVER; $$ = $1; }
  ;

modifier_list:
    modifier_list modifier { COVER; $$ = MAKE_NODE($2, $1); }
  | modifier               { COVER; $$ = MAKE_NODE($1); }
  ;

modifier:
    STATIC { COVER; $$ = make_identifier_leaf($1); }
  | EXTERN { COVER; $$ = make_identifier_leaf($1); }
  | CONST  { COVER; $$ = make_identifier_leaf($1); }
  | QUICK  { COVER; $$ = make_identifier_leaf($1); }
  | REF    { COVER; $$ = make_identifier_leaf($1); }
  ;

decl:
    decl_specifiers declarator                { COVER; $$ = MAKE_NODE($1, $2); }
  | decl_specifiers declarator '@' INTEGER    { COVER; $$ = MAKE_NODE($1, $2, make_integer_leaf($4)); }
  | decl_specifiers declarator '@' IDENTIFIER { COVER; $$ = MAKE_NODE($1, $2, make_identifier_leaf($4)); }
  ;

decl_specifiers:
    TYPENAME                          { COVER; $$ = MAKE_NODE(make_empty_leaf(), make_typename_leaf($1)); }
  | modifier_list TYPENAME            { COVER; $$ = MAKE_NODE($1, make_typename_leaf($2)); }
  ;

declarator:
    pointer direct_declarator         { COVER; $$ = MAKE_NODE($1, $2); }
  | direct_declarator                 { COVER; $$ = MAKE_NODE(make_integer_leaf(strdup("0")), $1); }
  ;

pointer:
    '*'                               { COVER; $$ = make_integer_leaf(strdup("1")); }
  | '*' pointer                       { COVER; $$ = $2; increment_integer_leaf($$); }
  ;

direct_declarator:
    IDENTIFIER                               { COVER; $$ = make_identifier_leaf($1); }
  | OPERATOR                                 { COVER; $$ = make_identifier_leaf($1); }
  | '(' declarator ')'                       { COVER; $$ = $2; }
  | direct_declarator '[' INTEGER ']'        { COVER; $$ = MAKE_NODE($1, make_integer_leaf($3)); }
  | direct_declarator '(' parameter_list ')' { COVER; $$ = MAKE_NODE($1, $3); }
  | direct_declarator '(' ')'                { COVER; $$ = MAKE_NODE($1, make_empty_leaf()); }
  ;

parameter_list:
    parameter                    { COVER; $$ = $1; }
  | parameter_list ',' parameter { COVER; $$ = MAKE_NODE($1, $3); }
  ;

parameter:
    decl                  { COVER; $$ = $1; }
  ;

block:
    '{' statement_list '}' { COVER; $$ = $2; }
  ;

statement_list:
    %empty                   { COVER; $$ = make_empty_leaf(); }
  | statement statement_list { COVER; $$ = ($2->kind != AST_EMPTY) ? MAKE_NODE($1,$2) : MAKE_NODE($1); }
  ;

statement:
    block                  { COVER; $$ = $1; }
  | expr_stmt              { COVER; $$ = $1; }
  | defdecl_stmt           { COVER; $$ = $1; }
  | return_stmt            { COVER; $$ = $1; }
  | goto_stmt              { COVER; $$ = $1; }
  | break_stmt             { COVER; $$ = $1; }
  | continue_stmt          { COVER; $$ = $1; }
  | switch_stmt            { COVER; $$ = $1; }
  | if_stmt                { COVER; $$ = $1; }
  | while_stmt             { COVER; $$ = $1; }
  | for_stmt               { COVER; $$ = $1; }
  | do_stmt                { COVER; $$ = $1; }
  | label_stmt             { COVER; $$ = $1; }
  ;

return_stmt:
    RETURN opt_expr ';' { COVER; $$ = MAKE_NODE($2); }
  ;

goto_stmt:
    GOTO IDENTIFIER ';' { COVER; $$ = MAKE_NODE(make_identifier_leaf($2)); }
  ;

break_stmt:
    BREAK ';'            { COVER; $$ = MAKE_NODE(make_empty_leaf()); }
  | BREAK IDENTIFIER ';' { COVER; $$ = MAKE_NODE(make_identifier_leaf($2)); }
  ;

continue_stmt:
    CONTINUE ';'            { COVER; $$ = MAKE_NODE(make_empty_leaf()); }
  | CONTINUE IDENTIFIER ';' { COVER; $$ = MAKE_NODE(make_identifier_leaf($2)); }
  ;

switch_stmt:
    SWITCH '(' expr ')' '{' case_section '}' { COVER; $$ = MAKE_NODE($3,$6); }
  ;

if_stmt:
    IF '(' expr ')' block ELSE block { COVER; $$ = MAKE_NODE($3, $5, $7); }
  | IF '(' expr ')' block            { COVER; $$ = MAKE_NODE($3, $5); }
  ;

while_stmt:
    WHILE '(' expr ')' block { COVER; $$ = MAKE_NODE($3, $5); }
  ;

for_stmt:
    FOR '(' opt_expr ';' opt_expr ';' opt_expr ')' block { COVER; $$ = MAKE_NODE($3, $5, $7, $9); }
  ;

do_stmt:
    DO block WHILE '(' expr ')' ';' { COVER; $$ = MAKE_NODE($2, $5); }
  ;

label_stmt:
    IDENTIFIER ':' statement { COVER; $$ = MAKE_NODE(make_identifier_leaf($1), $3); }
  ;

array_initializer:
    '{' expr_list '}'                   { COVER; $$ = $2; }
  | '{' expr_list ',' '}'               { COVER; $$ = $2; }
  | '{' array_initializer_list '}'      { COVER; $$ = $2; }
  | '{' array_initializer_list ',' '}'  { COVER; $$ = $2; }
  ;

array_initializer_list:
    array_initializer                            { COVER; $$ = MAKE_NODE($1); }
  | array_initializer_list ',' array_initializer { COVER; $$ = MAKE_NODE($1, $3); }
  ;

expr_list:
    expr                        { COVER; $$ = MAKE_NODE($1); }
  | expr_list ',' expr          { COVER; $$ = MAKE_NODE($1, $3); }
;

expr_stmt:
    expr ';' { COVER; $$ = $1; }
  | ';'      { COVER; $$ = make_empty_leaf(); }
  ;

opt_expr:
    %empty       { COVER; $$ = make_empty_leaf(); }
  | expr         { COVER; $$ = $1; }
  ;


expr:
    logical_or_expr                    { COVER; $$ = MAKE_NODE($1); }
  | logical_or_expr '?' expr ':' expr  { COVER; $$ = MAKE_NODE(make_identifier_leaf("?:"), $1, $3, $5); }
  | lvalue ASSIGN expr             { COVER; $$ = MAKE_NODE(make_identifier_leaf(":="), $1, $3); }
  | lvalue ADD_ASSIGN expr         { COVER; $$ = MAKE_NODE(make_identifier_leaf("+="), $1, $3); }
  | lvalue SUB_ASSIGN expr         { COVER; $$ = MAKE_NODE(make_identifier_leaf("-="), $1, $3); }
  | lvalue MUL_ASSIGN expr         { COVER; $$ = MAKE_NODE(make_identifier_leaf("*="), $1, $3); }
  | lvalue DIV_ASSIGN expr         { COVER; $$ = MAKE_NODE(make_identifier_leaf("/="), $1, $3); }
  | lvalue MOD_ASSIGN expr         { COVER; $$ = MAKE_NODE(make_identifier_leaf("%="), $1, $3); }
  | lvalue AND_ASSIGN expr         { COVER; $$ = MAKE_NODE(make_identifier_leaf("&="), $1, $3); }
  | lvalue OR_ASSIGN expr          { COVER; $$ = MAKE_NODE(make_identifier_leaf("|="), $1, $3); }
  | lvalue XOR_ASSIGN expr         { COVER; $$ = MAKE_NODE(make_identifier_leaf("^="), $1, $3); }
  | lvalue LSHIFT_ASSIGN expr      { COVER; $$ = MAKE_NODE(make_identifier_leaf("<<="), $1, $3); }
  | lvalue RSHIFT_ASSIGN expr      { COVER; $$ = MAKE_NODE(make_identifier_leaf(">>="), $1, $3); }
  ;

logical_or_expr:
    logical_and_expr                    { COVER; $$ = $1; }
  | logical_or_expr OR logical_and_expr { COVER; $$ = MAKE_NAMED_NODE("||", $1, $3); }
  ;

logical_and_expr:
    bitwise_or_expr                       { COVER; $$ = $1; }
  | logical_and_expr AND bitwise_or_expr  { COVER; $$ = MAKE_NAMED_NODE("&&", $1, $3); }
  ;

bitwise_or_expr:
    bitwise_xor_expr                      { COVER; $$ = $1; }
  | bitwise_or_expr '|' bitwise_xor_expr  { COVER; $$ = MAKE_NAMED_NODE("|", $1, $3); }
  ;

bitwise_xor_expr:
    bitwise_and_expr                       { COVER; $$ = $1; }
  | bitwise_xor_expr '^' bitwise_and_expr  { COVER; $$ = MAKE_NAMED_NODE("^", $1, $3); }
  ;

bitwise_and_expr:
    equality_expr                       { COVER; $$ = $1; }
  | bitwise_and_expr '&' equality_expr  { COVER; $$ = MAKE_NAMED_NODE("&", $1, $3); }
  ;

equality_expr:
    relational_expr                       { COVER; $$ = $1; }
  | equality_expr EQ relational_expr      { COVER; $$ = MAKE_NAMED_NODE("==", $1, $3); }
  | equality_expr NE relational_expr      { COVER; $$ = MAKE_NAMED_NODE("!=", $1, $3); }
  ;

relational_expr:
    shift_expr                       { COVER; $$ = $1; }
  | relational_expr '<' shift_expr   { COVER; $$ = MAKE_NAMED_NODE("<", $1, $3); }
  | relational_expr '>' shift_expr   { COVER; $$ = MAKE_NAMED_NODE(">", $1, $3); }
  | relational_expr LE shift_expr    { COVER; $$ = MAKE_NAMED_NODE("<=", $1, $3); }
  | relational_expr GE shift_expr    { COVER; $$ = MAKE_NAMED_NODE(">=", $1, $3); }
  ;

shift_expr:
    additive_expr                       { COVER; $$ = $1; }
  | shift_expr LSHIFT additive_expr     { COVER; $$ = MAKE_NAMED_NODE("<<", $1, $3); }
  | shift_expr RSHIFT additive_expr     { COVER; $$ = MAKE_NAMED_NODE(">>", $1, $3); }
  ;

additive_expr:
    multiplicative_expr                       { COVER; $$ = $1; }
  | additive_expr '+' multiplicative_expr     { COVER; $$ = MAKE_NAMED_NODE("+", $1, $3); }
  | additive_expr '-' multiplicative_expr     { COVER; $$ = MAKE_NAMED_NODE("-", $1, $3); }
  ;

multiplicative_expr:
    unary_expr                         { COVER; $$ = $1; }
  | multiplicative_expr '*' unary_expr { COVER; $$ = MAKE_NAMED_NODE("*", $1, $3); }
  | multiplicative_expr '/' unary_expr { COVER; $$ = MAKE_NAMED_NODE("/", $1, $3); }
  | multiplicative_expr '%' unary_expr { COVER; $$ = MAKE_NAMED_NODE("%", $1, $3); }
  ;

lvalue:
    lvalue_base lvalue_suffixes { COVER; $$ = MAKE_NODE($1,$2); }
  | INC lvalue_base lvalue_suffixes { COVER; $$ = MAKE_NODE($2, $3, make_identifier_leaf("pre++")); }
  | DEC lvalue_base lvalue_suffixes { COVER; $$ = MAKE_NODE($2, $3, make_identifier_leaf("pre--")); }
  | lvalue_base lvalue_suffixes INC { COVER; $$ = MAKE_NODE($1, $2, make_identifier_leaf("post++")); }
  | lvalue_base lvalue_suffixes DEC { COVER; $$ = MAKE_NODE($1, $2, make_identifier_leaf("post--")); }
  ;

lvalue_base:
    IDENTIFIER      { COVER; $$ = MAKE_NODE(make_identifier_leaf($1)); }
  | '*' lvalue_base { COVER; $$ = MAKE_NAMED_NODE("*", $2); }
  ;

lvalue_suffixes:
    %empty                                { COVER; $$ = make_empty_leaf(); }
  | lvalue_suffixes '[' expr ']'      { COVER; $$ = MAKE_NAMED_NODE("[", $1, $3); }
  | lvalue_suffixes '.' IDENTIFIER    { COVER; $$ = MAKE_NAMED_NODE(".", $1, make_identifier_leaf($3)); }
  | lvalue_suffixes ARROW IDENTIFIER  { COVER; $$ = MAKE_NAMED_NODE("->", $1, make_identifier_leaf($3)); }
  ;

unary_expr:
    postfix_expr   { COVER; $$ = $1; }
  | '!' unary_expr { COVER; $$ = MAKE_NAMED_NODE("!", $2); }
  | '~' unary_expr { COVER; $$ = MAKE_NAMED_NODE("~", $2); }
  | '-' unary_expr { COVER; $$ = MAKE_NAMED_NODE("-", $2); }
  | '&' unary_expr { COVER; $$ = MAKE_NAMED_NODE("&", $2); }
  ;

postfix_expr:
    IDENTIFIER '(' arg_list ')'    { COVER; $$ = MAKE_NAMED_NODE("()", make_identifier_leaf($1), $3); }
  | primary_expr                   { COVER; $$ = $1; }
  ;

primary_expr:
    INTEGER        { COVER; $$ = make_integer_leaf($1); }
  | FLOAT          { COVER; $$ = make_float_leaf($1); }
  | STRING         { COVER; $$ = make_string_leaf($1); }
  | struct_literal { COVER; $$ = $1; }
  | lvalue         { COVER; $$ = $1; }
  | '(' expr ')'   { COVER; $$ = $2; }
  ;

arg_list:
    %empty       { COVER; $$ = make_empty_leaf(); }
  | expr_args    { COVER; $$ = $1; }
  ;

expr_args:
    expr                       { COVER; $$ = MAKE_NODE($1); }
  | expr_args ',' expr         { COVER; $$ = MAKE_NODE($1, $3); }
  ;


struct_literal:
    TYPENAME '{' struct_inits ';' '}' { COVER; $$ = MAKE_NODE(make_identifier_leaf($1), $3); }
  | TYPENAME '{' FLOAT '}'            { COVER; $$ = MAKE_NODE(make_identifier_leaf($1), make_float_leaf($3)); }
  | TYPENAME '{' '-' FLOAT '}'        { COVER; $$ = MAKE_NODE(make_identifier_leaf($1),
                                                       make_float_leaf(make_negative($4))); }
  | TYPENAME '{' INTEGER '}'          { COVER; $$ = MAKE_NODE(make_identifier_leaf($1), make_integer_leaf($3)); }
  | TYPENAME '{' '-' INTEGER '}'      { COVER; $$ = MAKE_NODE(make_identifier_leaf($1),
                                                       make_integer_leaf(make_negative($4))); }
  | TYPENAME '{' STRING '}'           { COVER; $$ = MAKE_NODE(make_identifier_leaf($1), make_string_leaf($3)); }
  ;

struct_inits:
    struct_inits ';' struct_init { COVER; $$ = MAKE_NODE($1, $3); }
  | struct_init                  { COVER; $$ = MAKE_NODE($1); }
  ;

struct_init:
    IDENTIFIER ASSIGN expr { COVER; $$ = MAKE_NODE(make_identifier_leaf($1), $3); }
  ;

case_section:
    case_section case_block { COVER; $$ = MAKE_NODE($1, $2); }
  | case_block              { COVER; $$ = MAKE_NODE($1); }
  ;

case_block:
    CASE expr ':' statement_list { COVER; $$ = MAKE_NODE($2, $4); }
  | DEFAULT ':' statement_list   { COVER; $$ = MAKE_NODE(make_empty_leaf(), $3); }
  ;

opt_flags:
    flag_list   { COVER; $$ = $1; }
  | %empty      { COVER; $$ = make_empty_leaf(); }
  ;

flag_list:
    flag_list flag { COVER; $$ = MAKE_NODE($2, $1); }
  | flag           { COVER; $$ = MAKE_NODE($1); }
  ;

flag:
    FLAG { COVER; $$ = make_identifier_leaf($1); }
  ;

%%
