/* ---------- parser.y ---------- */
%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "ast.h"

extern int yylex();
void yyerror(const char *fmt, ...);
void yywarn(const char *fmt, ...);

extern int yylineno;
extern int yycolumn;

/////

#define MAX_TYPES 1024
static char *type_names[MAX_TYPES];
static int type_count = 0;

int find_typename(const char* name) {
   for (int i = 0; i < type_count; i++) {
      if (strcmp(type_names[i], name) == 0) return i;
   }
   return -1;
}

int register_typename(const char* name) {
   // allows duplicates, errors come on the second pass
   if (find_typename(name) == -1) {
      if (type_count < MAX_TYPES) {
         type_names[type_count++] = strdup(name);
      }
      else {
         yyerror("type table full @%d:%d", yylineno, yycolumn);
         return -1;
      }
   }
   return 0;
}

ASTNode *make_node(const char *name, ...) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = name;
   ret->line = yylineno;
   ret->column = yycolumn;
   va_list ap;
   va_start(ap, name);
   ASTNode *child;
   while ((child = va_arg(ap, ASTNode *)) != NULL) {
      if (ret->count < 16)
         ret->children[ret->count++] = child;
   }
   va_end(ap);
   return ret;
}
#define MAKE_NODE(...) make_node(yysymbol_name(yytoken), __VA_ARGS__, NULL)

ASTNode *make_int_leaf(unsigned long long intval) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = "int";
   ret->line = yylineno;
   ret->column = yycolumn;
   ret->kind = AST_INT;
   ret->intval = intval;
   return ret;
}

ASTNode *make_str_leaf(char *strval) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = "str";
   ret->line = yylineno;
   ret->column = yycolumn;
   ret->kind = AST_STRING;
   ret->strval = strval ? strdup(strval) : NULL;
   return ret;
}

ASTNode *make_d_leaf(double dval) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = "float";
   ret->line = yylineno;
   ret->column = yycolumn;
   ret->kind = AST_FLOAT;
   ret->dval = dval;
   return ret;
}

ASTNode *make_empty_leaf(void) {
   ASTNode *ret = calloc(1, sizeof(struct ASTNode));
   ret->name = "empty";
   ret->line = yylineno;
   ret->column = yycolumn;
   ret->kind = AST_EMPTY;
   return ret;
}

void dump_ast(const ASTNode *node, const char *prefix, int is_last) {
    if (!node) return;

    // Draw the branch
    printf("%s%s%s ", prefix,
           is_last ? "└──" : "├──",
           node->name);

    // Add leaf value if applicable
    switch (node->kind) {
        case AST_INT: printf(" %llu", node->intval); break;
        case AST_FLOAT: printf(" %f", node->dval); break;
        case AST_STRING: printf(" \"%s\"", node->strval); break;
        default: break;
    }
    printf("\n");

    // New prefix for children
    char new_prefix[256];
    snprintf(new_prefix, sizeof(new_prefix), "%s%s",
             prefix, is_last ? "    " : "│   ");

    for (int i = 0; i < node->count; ++i) {
        dump_ast(node->children[i], new_prefix, i == node->count - 1);
    }
}

ASTNode *root = NULL;

void parse_dump(void) {
   if (root) {
      dump_ast(root, "", 0);
   }
}

%}

%union {
    char    *str;
    double   dval;
    int      intval;
    ASTNode *node;
}

%token <str> STRING IDENTIFIER TYPENAME FLAG OPERATOR
%token <intval> INTEGER
%token <dval> FLOAT
%token IF ELSE WHILE FOR RETURN TYPE
%token ASSIGN
%token EQ NE LE GE LSHIFT RSHIFT OR AND
%token INC DEC ARROW
%token STRUCT UNION
%token GOTO SWITCH CASE DEFAULT
%token BREAK
%token CONTINUE
%token DO

%token ADD_ASSIGN SUB_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN
%token AND_ASSIGN OR_ASSIGN XOR_ASSIGN LSHIFT_ASSIGN RSHIFT_ASSIGN

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
%right UADDR UINDIRECT
%left POSTINC POSTDEC   // post (x++, x--)
%right UMINUS UINC UDEC
%right ASSIGN

%type <node> additive_expr arg_list array_initializer assignable assignment_expr
%type <node> bitwise_and_expr bitwise_or_expr bitwise_xor_expr block
%type <node> case_block case_section
%type <node> decl_stmt
%type <node> equality_expr expr expr_args expr_list expr_stmt
%type <node> flag flag_list function_decl
%type <node> logical_and_expr logical_or_expr
%type <node> multiplicative_expr
%type <node> opt_address opt_array_dim opt_expr opt_flags opt_pointer
%type <node> param_decls param_list postfix_expr primary_expr program program_item
%type <node> relational_expr
%type <node> shift_expr statement statement_list struct_decl struct_field
%type <node> struct_fields struct_init struct_inits struct_literal
%type <node> type_decl
%type <node> type_name
%type <node> unary_expr
%type <node> union_decl

%type <node> return_stmt goto_stmt break_stmt continue_stmt switch_stmt 
%type <node> if_stmt while_stmt for_stmt do_stmt label_stmt 

%define parse.error verbose
%locations

%%
program:
    program_item         { root = $$ = MAKE_NODE($1); }
  | program program_item { root = $$ = MAKE_NODE($1, $2); }
  ;

program_item:
    type_decl     { $$ = $1; }
  | function_decl { $$ = $1; }
  | decl_stmt     { $$ = $1; }
  | struct_decl   { $$ = $1; }
  | union_decl    { $$ = $1; }
  ;

type_decl:
    TYPE IDENTIFIER '{' INTEGER opt_flags '}' ';' {
        if (register_typename($2) < 0) YYABORT;
        $$ = MAKE_NODE(make_str_leaf($2), make_int_leaf($4), $5);
    }
  | TYPE '*' '{' INTEGER opt_flags '}' ';' {
        if (register_typename("*") < 0) YYABORT;
        $$ = MAKE_NODE(make_str_leaf("*"), make_int_leaf($4), $5);
    }
  | TYPE TYPENAME '{' INTEGER opt_flags '}' ';' {
        // always fails, but we want the error message
        if (register_typename($2) < 0) YYABORT;
        $$ = MAKE_NODE(make_str_leaf($2), make_int_leaf($4), $5); // TODO FIX is this needed?
    }
  ;

function_decl:
    type_name IDENTIFIER '(' param_list ')' ';'    { $$ = MAKE_NODE($1, make_str_leaf($2), $4); }
  | type_name IDENTIFIER '(' param_list ')' block  { $$ = MAKE_NODE($1, make_str_leaf($2), $4, $6); }
  | type_name OPERATOR '(' param_list ')' block    { $$ = MAKE_NODE($1, make_str_leaf($2), $4, $6); }
  ;

struct_decl:
    STRUCT IDENTIFIER '{' {
        if (register_typename($2) < 0) YYABORT;  // Add early to type table
    }
    struct_fields '}' ';' {
        register_typename($2);
        $$ = MAKE_NODE(make_str_leaf($2), $5);
    }
  | STRUCT TYPENAME '{' {
        // always fails, but we want the error message
        if (register_typename($2) < 0) YYABORT;  // Add early to type table
    }
  ;

union_decl:
    UNION IDENTIFIER '{' {
        if (register_typename($2) < 0) YYABORT;  // Add early to type table
    }
    struct_fields '}' ';' {
        register_typename($2);
        $$ = MAKE_NODE(make_str_leaf($2), $5);
    }
  | UNION TYPENAME '{' {
        // always fails, but we want the error message
        if (register_typename($2) < 0) YYABORT;  // Add early to type table
    }
  ;

struct_fields:
    struct_field                        { $$ = $1; }
  | struct_fields struct_field          { $$ = MAKE_NODE($1, $2); }
  ;

struct_field:
    type_name opt_pointer IDENTIFIER ';' { $$ = MAKE_NODE($1, $2, make_str_leaf($3)); }
  ;

opt_pointer:
    /* empty */     { $$ = make_int_leaf(0); }
  | '*' opt_pointer { $$ = $2; $$->intval++; }
  ;

param_list:
    /* empty */     { $$ = make_empty_leaf(); }
  | param_decls     { $$ = $1; }
  ;

param_decls:
    type_name IDENTIFIER                 { $$ = MAKE_NODE($1, make_str_leaf($2)); }
  | type_name                            { $$ = MAKE_NODE($1, make_empty_leaf()); } // unnamed param
  | param_decls ',' type_name IDENTIFIER { $$ = MAKE_NODE($1, MAKE_NODE($3, make_str_leaf($4))); }
  | param_decls ',' type_name            { $$ = MAKE_NODE($1, MAKE_NODE($3, make_empty_leaf())); }
;

type_name:
    TYPENAME { $$ = make_str_leaf($1); }
  ;

block:
    '{' statement_list '}' { $$ = $2; }
  ;

statement_list:
    /* empty */              { $$ = make_empty_leaf(); }
  | statement_list statement { $$ = ($1->kind != AST_EMPTY) ? MAKE_NODE($1, $2) : MAKE_NODE($2); }
  ;

statement:
    block          { $$ = $1; }
  | decl_stmt      { $$ = $1; }
  | expr_stmt      { $$ = $1; }
  | return_stmt    { $$ = $1; }
  | goto_stmt      { $$ = $1; }
  | break_stmt     { $$ = $1; }
  | continue_stmt  { $$ = $1; }
  | switch_stmt    { $$ = $1; }
  | if_stmt        { $$ = $1; }
  | while_stmt     { $$ = $1; }
  | for_stmt       { $$ = $1; }
  | do_stmt        { $$ = $1; }
  | label_stmt     { $$ = $1; }
  ;

return_stmt:
    RETURN opt_expr ';' { $$ = MAKE_NODE($2); }
  ;

goto_stmt:
    GOTO IDENTIFIER ';' { $$ = MAKE_NODE($2); }
  ;

break_stmt:
    BREAK ';'            { $$ = MAKE_NODE(make_empty_leaf()); }
  | BREAK IDENTIFIER ';' { $$ = MAKE_NODE(make_str_leaf($2)); }
  ;

continue_stmt:
    CONTINUE ';'            { $$ = MAKE_NODE(make_empty_leaf()); }
  | CONTINUE IDENTIFIER ';' { $$ = MAKE_NODE(make_str_leaf($2)); }
  ;

switch_stmt:
    SWITCH '(' expr ')' '{' case_section '}' { $$ = MAKE_NODE($3,$6); }
  ;

if_stmt:
    IF '(' expr ')' block ELSE block { $$ = MAKE_NODE($3, $5, $7); }
  | IF '(' expr ')' block            { $$ = MAKE_NODE($3, $5); }
  ;

while_stmt:
    WHILE '(' expr ')' block { $$ = MAKE_NODE($3, $5); }
  ;

for_stmt:
    FOR '(' opt_expr ';' opt_expr ';' opt_expr ')' block { $$ = MAKE_NODE($3, $5, $7, $9); }
  ;

do_stmt:
    DO block WHILE '(' expr ')' ';' { $$ = MAKE_NODE($2, $5); }
  ;

label_stmt:
    IDENTIFIER ':' statement { $$ = MAKE_NODE(make_str_leaf($1), $3); }
  ;

opt_address:
    /* empty */   { $$ = make_empty_leaf(); }
  | '@' INTEGER   { $$ = make_int_leaf($2); }
  ;

decl_stmt:
    type_name IDENTIFIER opt_array_dim opt_address ';'                          { $$ = MAKE_NODE($1, make_str_leaf($2), $3, $4); }
  | type_name IDENTIFIER opt_array_dim opt_address ASSIGN expr ';'              { $$ = MAKE_NODE($1, make_str_leaf($2), $3, $4, $6); }
  | type_name IDENTIFIER opt_array_dim opt_address ASSIGN array_initializer ';' { $$ = MAKE_NODE($1, make_str_leaf($2), $3, $4, $6); }
  ;

opt_array_dim:
    /* empty */  { $$ = make_empty_leaf(); }
  | '[' expr ']' { $$ = $2; }
  ;

array_initializer:
    '{' expr_list '}' { $$ = $2; }
  ;

expr_list:
    expr                        { $$ = MAKE_NODE($1); }
  | expr_list ',' expr          { $$ = MAKE_NODE($1, $3); }
;


expr_stmt:
    expr ';' { $$ = $1; }
  | ';'      { $$ = make_empty_leaf(); }
  ;

opt_expr:
    /* empty */  { $$ = make_empty_leaf(); }
  | expr         { $$ = $1; }
  ;


expr: assignment_expr ;


assignment_expr:
    logical_or_expr
  | logical_or_expr '?' expr ':' assignment_expr
  | assignable ASSIGN assignment_expr
  | assignable ADD_ASSIGN assignment_expr
  | assignable SUB_ASSIGN assignment_expr
  | assignable MUL_ASSIGN assignment_expr
  | assignable DIV_ASSIGN assignment_expr
  | assignable MOD_ASSIGN assignment_expr
  | assignable AND_ASSIGN assignment_expr
  | assignable OR_ASSIGN assignment_expr
  | assignable XOR_ASSIGN assignment_expr
  | assignable LSHIFT_ASSIGN assignment_expr
  | assignable RSHIFT_ASSIGN assignment_expr
  ;

assignable:
    IDENTIFIER
  | postfix_expr '[' expr ']'
  | '*' unary_expr
  ;

logical_or_expr:
    logical_and_expr
  | logical_or_expr OR logical_and_expr
  ;

logical_and_expr:
    bitwise_or_expr
  | logical_and_expr AND bitwise_or_expr
  ;

bitwise_or_expr:
    bitwise_xor_expr
  | bitwise_or_expr '|' bitwise_xor_expr
  ;

bitwise_xor_expr:
    bitwise_and_expr
  | bitwise_xor_expr '^' bitwise_and_expr
  ;

bitwise_and_expr:
    equality_expr
  | bitwise_and_expr '&' equality_expr
  ;

equality_expr:
    relational_expr
  | equality_expr EQ relational_expr
  | equality_expr NE relational_expr
  ;

relational_expr:
    shift_expr
  | relational_expr '<' shift_expr
  | relational_expr '>' shift_expr
  | relational_expr LE shift_expr
  | relational_expr GE shift_expr
  ;

shift_expr:
    additive_expr
  | shift_expr LSHIFT additive_expr
  | shift_expr RSHIFT additive_expr
  ;

additive_expr:
    multiplicative_expr
  | additive_expr '+' multiplicative_expr
  | additive_expr '-' multiplicative_expr
  ;

multiplicative_expr:
    unary_expr
  | multiplicative_expr '*' unary_expr
  | multiplicative_expr '/' unary_expr
  | multiplicative_expr '%' unary_expr
  ;

unary_expr:
    postfix_expr
  | '!' unary_expr
  | '~' unary_expr
  | '-' unary_expr
  | '&' unary_expr
  | '*' unary_expr
  | INC unary_expr
  | DEC unary_expr
  ;

postfix_expr:
    primary_expr
  | postfix_expr '.' IDENTIFIER
  | postfix_expr ARROW IDENTIFIER
  | postfix_expr INC
  | postfix_expr DEC
  | postfix_expr '[' expr ']'
  | IDENTIFIER '(' arg_list ')'
  ;

primary_expr:
    IDENTIFIER  | INTEGER  | FLOAT
  | STRING
  | struct_literal
  | '(' expr ')'
  ;

arg_list:
    /* empty */
  | expr_args
  ;

expr_args:
    expr
  | expr_args ',' expr
  ;

struct_literal:
    TYPENAME '{' struct_inits ';' '}'
  | TYPENAME '{' FLOAT '}'
  | TYPENAME '{' '-' FLOAT '}'
  | TYPENAME '{' INTEGER '}'
  | TYPENAME '{' '-' INTEGER '}'
  | TYPENAME '{' STRING '}'
  ;

struct_inits:
    struct_inits ';' struct_init
  | struct_init
  ;

struct_init:
    IDENTIFIER ASSIGN expr
  ;

case_section:
    case_section case_block
  | case_block
  ;

case_block:
    CASE expr ':' statement_list
  | DEFAULT ':' statement_list
  ;

opt_flags:
    flag_list   {  }
  | /* empty */ {  }
  ;

flag_list:
    flag_list flag {
    }
  | flag {
    }
  ;

flag:
    FLAG {
    }
  ;
%%
extern char* yytext;

void yyerror(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   int size = 1 + vsnprintf(NULL, 0, fmt, ap);
   va_end(ap);

   char *str = (char *) malloc(sizeof(char) * size);

   va_start(ap, fmt);
   vsnprintf(str, size, fmt, ap);
   va_end(ap);

   fprintf(stderr, "Error at %d:%d (near '%s')\n", yylineno, yycolumn, yytext);
   fprintf(stderr, "   %s\n", str);
   free(str);
}

void yywarn(const char *fmt, ...) {
   va_list ap;
   va_start(ap, fmt);
   int size = 1 + vsnprintf(NULL, 0, fmt, ap);
   va_end(ap);

   char *str = (char *) malloc(sizeof(char) * size);

   va_start(ap, fmt);
   vsnprintf(str, size, fmt, ap);
   va_end(ap);

   fprintf(stderr, "Warning at %d:%d (near '%s')\n", yylineno, yycolumn, yytext);
   fprintf(stderr, "   %s\n", str);
   free(str);
}
